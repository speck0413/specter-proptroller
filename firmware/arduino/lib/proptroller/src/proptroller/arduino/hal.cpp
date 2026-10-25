#if defined(ARDUINO) && (defined(ESP32) || defined(ARDUINO_ARCH_RP2040))
#include "hal.h"
#include "../flash_map.h"
#include <LittleFS.h>
#include <stdio.h>

#if defined(ESP32)
#include <driver/rmt.h>
#if SOC_CPU_CORES_NUM > 1
#include <esp_ipc.h>
#endif
#include <soc/gpio_reg.h>
#include <soc/soc.h>
#include <driver/uart.h>
#include <esp_mac.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <pico/mutex.h>
#include <pico/time.h>
#include <pico/unique_id.h>
extern uint8_t _FS_start;  // the LittleFS region (arduino-pico's linker script)
extern uint8_t _FS_end;
#endif

namespace pt {

#if defined(ESP32)
Millis millis64() { return (Millis)(esp_timer_get_time() / 1000); }
#else
Millis millis64() { return (Millis)(time_us_64() / 1000); }
#endif

void outputsOffNow(const PinMap& m, bool pixel_inverted) {
  for (int ch = 0; ch < 8; ch++) {
    uint8_t p = channelPin(m, ch);
    if (p == NO_PIN) continue;  // not on this board: never touched
    digitalWrite(p, LOW);
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  // the PIXEL data inverted by the board: idle is HIGH on the MCU pin (LOW without the inverter)
  if (m.pixel != NO_PIN) {
    pinMode(m.pixel, OUTPUT);
    digitalWrite(m.pixel, pixel_inverted ? HIGH : LOW);
  }
}

// ---------------------------------------------------------------- pins

static const uint32_t PWM_HZ = 1000;

ArduinoPins::ArduinoPins(const PinMap& m, int led) : m_(m), led_(led) {
  for (int k = 0; k < 5; k++) {
    in_pin_[k] = inputPin(m_, k);
    in_high_[k] = inputActiveHigh(m_, k);
  }
}

void ArduinoPins::begin() {
#if defined(ESP32)
  for (uint8_t i = 0; i < 4; i++) {
    if (m_.out[i] == NO_PIN) continue;
    ledcSetup(i, PWM_HZ, 10);
    ledcAttachPin((uint8_t)m_.out[i], i);
    ledcWrite(i, 0);
  }
#else
  analogWriteFreq(PWM_HZ);
  analogWriteRange(1000);
  for (int i = 0; i < 4; i++)
    if (m_.out[i] != NO_PIN) analogWrite(m_.out[i], 0);
#endif
  // inputs: the board's own resistor (TRIG1-2 on this board: 4.7k pull-ups; ESP32 GPIO34-39 have no internal
  // pulls), else the internal pull-up (active low) or pull-down (active high)
  for (int k = 0; k < 5; k++) {
    uint8_t p = inputPin(m_, k);
    if (p == NO_PIN) continue;
    pinMode(p, inputExternalPull(m_, k) ? INPUT : inputActiveHigh(m_, k) ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
  if (led_ >= 0) { pinMode((uint8_t)led_, OUTPUT); digitalWrite((uint8_t)led_, LOW); }
}

bool ArduinoPins::setOut(int i, Level l) {
#if defined(ESP32)
  uint32_t duty = l >= FULL ? 1024 : ((uint32_t)1024 * l + 500) / 1000;
  ledcWrite((uint8_t)i, duty);
#else
  analogWrite(m_.out[i], l >= FULL ? 1000 : l);
#endif
  return true;
}

bool ArduinoPins::setDigital(int which, bool on) {
  uint8_t p = which < 2 ? m_.relay[which] : m_.contact[which - 2];
  digitalWrite(p, on ? HIGH : LOW);
  return true;
}

bool ArduinoPins::readLow(int input, bool& low) {
  const uint8_t p = in_pin_[input];
  if (p == NO_PIN) { low = false; return true; }
  low = (digitalRead(p) == HIGH) == in_high_[input];  // active
  return true;
}

static bool g_latched = false;
static uint64_t g_latched_in = 0;
void ArduinoPins::useLatched(bool on, uint64_t gpio_in) {
  g_latched = on;
  g_latched_in = gpio_in;
}

bool ArduinoPins::readAllLow(bool low[INPUT_COUNT]) {
#if defined(ESP32)
  const uint64_t in = g_latched ? g_latched_in : PT_GPIO_IN_ALL();
#else
  const uint64_t in = gpio_get_all();
#endif
  for (int k = 0; k < INPUT_COUNT; k++) {
    const uint8_t p = in_pin_[k];
    low[k] = p != NO_PIN && (((in >> p) & 1) != 0) == in_high_[k];
  }
  return true;
}

bool ArduinoPins::setStatusLed(bool on) {
  if (led_ >= 0) digitalWrite((uint8_t)led_, on ? HIGH : LOW);
  return true;
}

// ---------------------------------------------------------------- DFPlayer serial

// A board profile without the sound port: nothing is opened (a default UART pin could be driven otherwise).
class NoStream : public Stream {
 public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 1; }
  void flush() override {}
};

Stream& openDfSerial(const PinMap& m) {
  if (m.df_tx == NO_PIN || m.df_rx == NO_PIN) {
    static NoStream none;
    return none;
  }
#if defined(ESP32) && SOC_UART_NUM > 2
  Serial2.begin(9600, SERIAL_8N1, m.df_rx, m.df_tx);
  return Serial2;
#elif defined(ESP32)
  // the C3 has two UARTs: UART0 (its console is the USB port's own CDC) and UART1, the sound port's (no DMX port)
  Serial1.begin(9600, SERIAL_8N1, m.df_rx, m.df_tx);
  return Serial1;
#else
  // GP7 is not a hardware UART RX pin: a PIO UART on GP8 (TX) / GP7 (RX)
  static SerialPIO df(m.df_tx, m.df_rx, 64);
  df.begin(9600);
  return df;
#endif
}

// ---------------------------------------------------------------- pixels

#if defined(ESP32)
static const rmt_channel_t PIX_CH = RMT_CHANNEL_0;
// 40 MHz RMT clock (25 ns ticks). The board inverts, so every level is flipped: a "1" bit is LOW for
// 800 ns then HIGH for 450 ns on the pin, and the pin idles HIGH.
static const rmt_item32_t WS_BIT0 = {{{400 / 25, 0, 850 / 25, 1}}};
static const rmt_item32_t WS_BIT1 = {{{800 / 25, 0, 450 / 25, 1}}};
// without the board's inverter: a "1" bit is HIGH for 800 ns then LOW for 450 ns, and the pin idles LOW
static const rmt_item32_t WS_BIT0_PLAIN = {{{400 / 25, 1, 850 / 25, 0}}};
static const rmt_item32_t WS_BIT1_PLAIN = {{{800 / 25, 1, 450 / 25, 0}}};

static void IRAM_ATTR toRmtPlain(const void* src, rmt_item32_t* dest, size_t src_size, size_t wanted, size_t* translated, size_t* item_num) {
  size_t size = 0, num = 0;
  const uint8_t* p = (const uint8_t*)src;
  while (size < src_size && num + 8 <= wanted) {
    for (int b = 7; b >= 0; b--) dest[num++] = (p[size] >> b) & 1 ? WS_BIT1_PLAIN : WS_BIT0_PLAIN;
    size++;
  }
  *translated = size;
  *item_num = num;
}

static void IRAM_ATTR toRmt(const void* src, rmt_item32_t* dest, size_t src_size, size_t wanted, size_t* translated, size_t* item_num) {
  size_t size = 0, num = 0;
  const uint8_t* p = (const uint8_t*)src;
  while (size < src_size && num + 8 <= wanted) {
    for (int b = 7; b >= 0; b--) dest[num++] = (p[size] >> b) & 1 ? WS_BIT1 : WS_BIT0;
    size++;
  }
  *translated = size;
  *item_num = num;
}

// The channel takes the memory of channels 0-3 (256 items, RMT RAM, not heap; nothing else here uses the RMT):
// the refill interrupt comes every 128 items (160 us), so a late one (Wi-Fi's interrupts share core 0) still
// keeps the wire fed. The S3 and the C3 have 48-item blocks and only their transmit channels' memory to take:
// the S3 4 blocks (192 items, a refill every 96: 120 us), the C3 2 (96 items, a refill every 48: 60 us; with Wi-Fi
// on the one core a late refill can garble a frame, which the next frame 20 ms later replaces: unmeasured).
#if CONFIG_IDF_TARGET_ESP32
static const uint8_t PIX_MEM_BLOCKS = 4;
#else
static const uint8_t PIX_MEM_BLOCKS = SOC_RMT_TX_CANDIDATES_PER_GROUP;
#endif
struct RmtInstall {
  gpio_num_t pin;
  bool inverted;
  bool ok;
};
// on core 0: the driver's interrupt is allocated on the core that installs it, and core 1 runs the step
static void rmtInstall(void* arg) {
  RmtInstall& a = *(RmtInstall*)arg;
  rmt_config_t c = RMT_DEFAULT_CONFIG_TX(a.pin, PIX_CH);
  c.clk_div = 2;
  c.mem_block_num = PIX_MEM_BLOCKS;
  c.tx_config.idle_output_en = true;
  c.tx_config.idle_level = a.inverted ? RMT_IDLE_LEVEL_HIGH : RMT_IDLE_LEVEL_LOW;
  c.tx_config.carrier_en = false;
  a.ok = rmt_config(&c) == ESP_OK && rmt_driver_install(PIX_CH, 0, 0) == ESP_OK &&
         rmt_translator_init(PIX_CH, a.inverted ? toRmt : toRmtPlain) == ESP_OK;
}

bool BoardPixels::begin() {
  if (pin_ == NO_PIN) return false;  // the board profile has no pixel port
  RmtInstall a{(gpio_num_t)pin_, inverted_, false};
#if SOC_CPU_CORES_NUM > 1
  ok_ = esp_ipc_call_blocking(0, rmtInstall, &a) == ESP_OK && a.ok;
#else
  rmtInstall(&a);  // one core: installed here
  ok_ = a.ok;
#endif
  return ok_;
}

// The legacy driver's rmt_write_sample takes the channel's transmit semaphore with no time limit, so it is only
// called once the last frame is done (rmt_wait_tx_done with no wait) and its latch has passed (the pacer).
bool BoardPixels::busy() {
  if (!ok_ || !pacer_.sending) return false;
  if (!pacer_.ready((uint32_t)micros())) return true;
  return rmt_wait_tx_done(PIX_CH, 0) != ESP_OK;
}

bool BoardPixels::write(const Rgb* px, size_t n) {
  if (pin_ == NO_PIN) return false;
  if (!ok_ || busy()) return false;  // the caller asks busy() first: this never waits for the wire
  buf_.resize(n * 3);  // not sending: the translator is done with the old buffer
  for (size_t i = 0; i < n; i++) { buf_[3 * i] = px[i].g; buf_[3 * i + 1] = px[i].r; buf_[3 * i + 2] = px[i].b; }
  if (n == 0) return true;
  pacer_.start((uint32_t)micros(), n);
  // returns at once: the RMT interrupt translates buf_ into items as it sends; the idle level after it is the latch
  return rmt_write_sample(PIX_CH, buf_.data(), buf_.size(), false) == ESP_OK;
}
#else
// WS2812 PIO program (side-set 1, 10 cycles per bit at 8 MHz): out x,1 side 0 [2]; jmp !x 3 side 1 [1];
// jmp 0 side 1 [4]; nop side 0 [4]. The pad output is inverted for the board's level shifter.
static const uint16_t WS2812_PROG[] = {0x6221, 0x1123, 0x1400, 0xa442};
// every field of the SDK's pio_program (the RP2350 SDK adds some): zero, then ours
static pio_program_t pioProgram(const uint16_t* code, uint8_t length) {
  pio_program_t p;
  memset(&p, 0, sizeof p);
  p.instructions = code;
  p.length = length;
  p.origin = -1;
  return p;
}
static const pio_program_t WS2812_PIO = pioProgram(WS2812_PROG, 4);

bool BoardPixels::begin() {
  if (pin_ == NO_PIN) return false;  // the board profile has no pixel port
  PIO pios[] = {pio0, pio1};
  for (PIO pio : pios) {
    if (!pio_can_add_program(pio, &WS2812_PIO)) continue;
    int sm = pio_claim_unused_sm(pio, false);
    if (sm < 0) continue;
    uint offset = pio_add_program(pio, &WS2812_PIO);
    pio_gpio_init(pio, pin_);
    gpio_set_outover(pin_, inverted_ ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
    pio_sm_set_consecutive_pindirs(pio, sm, pin_, 1, true);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 3);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, pin_);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (800000.0f * 10));
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
    pio_ = pio;
    sm_ = (unsigned)sm;
    dma_ = dma_claim_unused_channel(false);  // the frame goes from buf_ into the FIFO by DMA
    ok_ = dma_ >= 0;
    return ok_;
  }
  return false;
}

// Done when the DMA has moved the last word and the pacer's time (the FIFO's last 8 pixels and the latch) is over.
bool BoardPixels::busy() {
  if (!ok_ || !pacer_.sending) return false;
  if (!pacer_.ready(time_us_32())) return true;
  return dma_channel_is_busy((uint)dma_);
}

bool BoardPixels::write(const Rgb* px, size_t n) {
  if (pin_ == NO_PIN) return false;
  if (!ok_ || busy()) return false;  // the caller asks busy() first: this never waits for the wire
  buf_.resize(n);  // not sending: the DMA is done with the old buffer
  for (size_t i = 0; i < n; i++) buf_[i] = ((uint32_t)px[i].g << 24) | ((uint32_t)px[i].r << 16) | ((uint32_t)px[i].b << 8);
  if (n == 0) return true;
  PIO pio = (PIO)pio_;
  dma_channel_config c = dma_channel_get_default_config((uint)dma_);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm_, true));
  pacer_.start(time_us_32(), n);
  dma_channel_configure((uint)dma_, &c, &pio->txf[sm_], buf_.data(), (uint)n, true);
  return true;
}
#endif

// ---------------------------------------------------------------- DMX on expansion S4

static const uint32_t DMX_BAUD = 250000;
static const uint32_t DMX_BREAK_US = 100;  // at least 92
static const uint32_t DMX_MAB_US = 16;     // at least 12
static const uint32_t DMX_TX_BUFFER = 1024;

#if defined(ESP32)
static const int DMX_BREAK_BITS = (int)(DMX_BREAK_US * DMX_BAUD / 1000000 + 1);  // 26 bits = 104 us

void BoardDmx::start() {
  if (on_ || pin_ == NO_PIN) return;
  Serial1.setTxBufferSize(DMX_TX_BUFFER);
  Serial1.begin(DMX_BAUD, SERIAL_8N2, -1, pin_);
  on_ = true;  // the first frame has no break before it; receivers skip it, the next ones are framed
}

void BoardDmx::stop() {
  if (!on_) return;
  Serial1.end();
  pinMode(pin_, INPUT);
  on_ = false;
}

void BoardDmx::send(const uint8_t* slots, size_t n) {
  if (!on_) return;
  if (n > DMX_CHANNELS) n = DMX_CHANNELS;
  // never wait in the step: no room in the driver's buffer (the last frames still going out) skips this one
  size_t room = 0;
  if (uart_get_tx_buffer_free_size(UART_NUM_1, &room) != ESP_OK || room < n + 1) return;
  frame_[0] = 0;
  memcpy(frame_ + 1, slots, n);
  // into the driver's ring buffer: the data, then the break (the mark after it lasts until the next frame)
  uart_write_bytes_with_break(UART_NUM_1, frame_, n + 1, DMX_BREAK_BITS);
}
#else
// UART TX, 8 cycles a bit, two stop bits: pull side 1 [7]; set x,7 side 0 [7]; out pins,1; jmp x-- 2 [6];
// nop side 1 [7] (the second stop bit).
static const uint16_t DMX_PROG[] = {0x9fa0, 0xf727, 0x6001, 0x0642, 0xbf42};
static const pio_program_t DMX_PIO = pioProgram(DMX_PROG, 5);
static const uint32_t DMX_DRAIN_US = 60;  // the last byte leaves the shift register (11 bits of 4 us)

void BoardDmx::start() {
  if (on_ || pin_ == NO_PIN) return;
  PIO pios[] = {pio0, pio1};
  for (PIO pio : pios) {
    if (!pio_can_add_program(pio, &DMX_PIO)) continue;
    int sm = pio_claim_unused_sm(pio, false);
    if (sm < 0) continue;
    offset_ = pio_add_program(pio, &DMX_PIO);
    pio_sm_set_pins_with_mask(pio, sm, 1u << pin_, 1u << pin_);  // idle high (mark)
    pio_sm_set_pindirs_with_mask(pio, sm, 1u << pin_, 1u << pin_);
    pio_gpio_init(pio, pin_);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset_, offset_ + 4);
    sm_config_set_sideset(&c, 2, true, false);
    sm_config_set_sideset_pins(&c, pin_);
    sm_config_set_out_pins(&c, pin_, 1);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (8.0f * DMX_BAUD));
    pio_sm_init(pio, sm, offset_, &c);
    pio_sm_set_enabled(pio, sm, true);
    pio_ = pio;
    sm_ = sm;
    dma_ = dma_claim_unused_channel(false);
    on_ = dma_ >= 0;
    return;
  }
}

void BoardDmx::stop() {
  if (!on_) return;
  PIO pio = (PIO)pio_;
  if (dma_ >= 0) { dma_channel_abort(dma_); dma_channel_unclaim(dma_); dma_ = -1; }
  pio_sm_set_enabled(pio, sm_, false);
  pio_remove_program(pio, &DMX_PIO, offset_);
  pio_sm_unclaim(pio, sm_);
  sm_ = -1;
  gpio_init(pin_);  // an input again
  on_ = false;
}

void BoardDmx::send(const uint8_t* slots, size_t n) {
  if (!on_ || dma_channel_is_busy(dma_)) return;  // still sending the last frame: skip this one
  PIO pio = (PIO)pio_;
  while (!pio_sm_is_tx_fifo_empty(pio, sm_)) tight_loop_contents();
  busy_wait_us_32(DMX_DRAIN_US);
  // break and mark after break on the pin itself, then the pin back to the state machine
  gpio_set_function(pin_, GPIO_FUNC_SIO);
  gpio_set_dir(pin_, true);
  gpio_put(pin_, 0);
  busy_wait_us_32(DMX_BREAK_US);
  gpio_put(pin_, 1);
  busy_wait_us_32(DMX_MAB_US);
  pio_gpio_init(pio, pin_);
  if (n > DMX_CHANNELS) n = DMX_CHANNELS;
  frame_[0] = 0;
  memcpy(frame_ + 1, slots, n);
  dma_channel_config c = dma_channel_get_default_config(dma_);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm_, true));
  dma_channel_configure(dma_, &c, &pio->txf[sm_], frame_, n + 1, true);
}
#endif

// ---------------------------------------------------------------- storage

namespace {
// the platform's LittleFS calls, without formatting on a failed mount (mountOrFormatBlank decides)
struct LittleFsMount : FsMount {
#if defined(ESP32)
  // the partition labelled spiffs in the shared table (FLASH-MAP.md)
  bool mount() override { return LittleFS.begin(false, "/littlefs", 10, ESP32_LITTLEFS_LABEL); }
  bool blank() override {
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, ESP32_LITTLEFS_LABEL);
    if (!p) return false;
    uint8_t buf[256];
    for (size_t at = 0; at < p->size; at += sizeof buf) {
      size_t n = p->size - at < sizeof buf ? p->size - at : sizeof buf;
      if (esp_partition_read(p, at, buf, n) != ESP_OK) return false;
      for (size_t i = 0; i < n; i++)
        if (buf[i] != 0xFF) return false;
    }
    return true;
  }
  bool format() override { return LittleFS.format(); }
#else
  bool mount() override {
    (void)LittleFS.setConfig(LittleFSConfig(false));  // arduino-pico formats on a failed mount by default
    return LittleFS.begin();
  }
  bool blank() override {
    // the file system's flash, read through the XIP window
    for (const volatile uint8_t* p = &_FS_start; p < &_FS_end; p++)
      if (*p != 0xFF) return false;
    return &_FS_end > &_FS_start;
  }
  bool format() override { return LittleFS.format(); }
#endif
};
}  // namespace

MountResult LittleFsIo::begin() {
  LittleFsMount m;
  MountResult r = mountOrFormatBlank(m);
  mounted_ = r != MountResult::Refused;
  return r;
}

// a factory reset of a file system that could not be mounted
bool LittleFsIo::reformat() {
  if (mounted_) return true;
  LittleFsMount m;
  mounted_ = m.format() && m.mount();
  return mounted_;
}

bool LittleFsIo::read(const char* path, std::string& out) {
  if (!mounted_ || !LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t want = f.size();
  out.clear();
  out.reserve(want);
  uint8_t buf[256];
  int n;
  while ((n = f.read(buf, sizeof buf)) > 0) out.append((const char*)buf, (size_t)n);
  f.close();
  return out.size() == want;  // a read cut short is a damaged record
}

bool LittleFsIo::write(const char* path, const std::string& data) { return writeRecord(path, nullptr, 0, data); }

bool LittleFsIo::writeRecord(const char* path, const uint8_t* head, size_t head_n, const std::string& data) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  size_t n = head_n ? f.write(head, head_n) : 0;
  n += f.write((const uint8_t*)data.data(), data.size());
  f.flush();
  f.close();  // reports nothing: ABStorage reads the file back
  return n == head_n + data.size();
}

bool LittleFsIo::scan(const char* path, ByteSink& sink) {
  if (!mounted_ || !LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t want = f.size(), got = 0;
  uint8_t buf[256];
  int n;
  while ((n = f.read(buf, sizeof buf)) > 0) {
    sink.piece(buf, (size_t)n);
    got += (size_t)n;
  }
  f.close();
  return got == want;
}

bool LittleFsIo::exists(const char* path) { return mounted_ && LittleFS.exists(path); }

bool LittleFsIo::remove(const char* path) { return LittleFS.exists(path) && LittleFS.remove(path); }

// ---------------------------------------------------------------- lock, id, restart

#if defined(ESP32)
RawLock::RawLock() { h_ = xSemaphoreCreateRecursiveMutex(); }
void RawLock::take() { xSemaphoreTakeRecursive((SemaphoreHandle_t)h_, portMAX_DELAY); }
void RawLock::give() { xSemaphoreGiveRecursive((SemaphoreHandle_t)h_); }
bool RawLock::mine() const { return xSemaphoreGetMutexHolder((SemaphoreHandle_t)h_) == xTaskGetCurrentTaskHandle(); }
RawLock::Id RawLock::self() const { return (Id)xTaskGetCurrentTaskHandle(); }
void RawLock::pause() { vTaskDelay(1); }
int RawLock::releaseAll() {
  int n = 0;
  while (xSemaphoreGetMutexHolder((SemaphoreHandle_t)h_) == xTaskGetCurrentTaskHandle()) {
    xSemaphoreGiveRecursive((SemaphoreHandle_t)h_);
    n++;
  }
  return n;
}

std::string deviceId() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char b[13];
  snprintf(b, sizeof b, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return b;
}
#if CONFIG_IDF_TARGET_ESP32S3
const char* mcuName() { return "esp32-s3"; }
#elif CONFIG_IDF_TARGET_ESP32C3
const char* mcuName() { return "esp32-c3"; }
#else
const char* mcuName() { return "esp32-devkit"; }
#endif
void restartNow() { ESP.restart(); }
#else
RawLock::RawLock() {
  recursive_mutex_t* m = new recursive_mutex_t;
  recursive_mutex_init(m);
  h_ = m;
}
void RawLock::take() { recursive_mutex_enter_blocking((recursive_mutex_t*)h_); }
void RawLock::give() { recursive_mutex_exit((recursive_mutex_t*)h_); }
// only this thread changes the owner while it is the owner
bool RawLock::mine() const { return ((recursive_mutex_t*)h_)->owner == lock_get_caller_owner_id(); }
RawLock::Id RawLock::self() const { return (Id)(intptr_t)lock_get_caller_owner_id(); }
void RawLock::pause() { sleep_ms(1); }
int RawLock::releaseAll() {
  recursive_mutex_t* m = (recursive_mutex_t*)h_;
  if (m->owner != lock_get_caller_owner_id()) return 0;  // only this thread changes it while it is the owner
  int n = m->enter_count;
  for (int i = 0; i < n; i++) recursive_mutex_exit(m);
  return n;
}

std::string deviceId() {
  pico_unique_board_id_t id;
  pico_get_unique_board_id(&id);
  std::string s;
  char b[3];
  for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) { snprintf(b, sizeof b, "%02x", id.id[i]); s += b; }
  return s;
}
const char* mcuName() {
#if defined(PICO_RP2350) && defined(PICO_CYW43_SUPPORTED)
  return "pico2-w";
#elif defined(PICO_RP2350)
  return "pico2";
#elif defined(PICO_CYW43_SUPPORTED)
  return "pico-w";
#else
  return "pico";
#endif
}
void restartNow() { rp2040.reboot(); }
#endif

}  // namespace pt
#endif
