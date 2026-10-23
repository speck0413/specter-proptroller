#include "ProptrollerAdvanced.h"
#include <stdio.h>
#include <string.h>

#if defined(ARDUINO) && (defined(ESP32) || defined(ARDUINO_ARCH_RP2040))
#define PT_BOARD 1
#include "proptroller/arduino/hal.h"
#if (defined(ESP32) || defined(PICO_CYW43_SUPPORTED)) && !defined(PROPTROLLER_NO_WIFI)  // NO_WIFI: test builds (QEMU)
#define PT_WIFI 1
#include "proptroller/arduino/net.h"
#endif
#include "proptroller/arduino/ota.h"
#include "proptroller/serial_line.h"
#if defined(ARDUINO_ARCH_RP2040)
#include <hardware/watchdog.h>
#endif
#if defined(ESP32)
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#endif
#elif !defined(ARDUINO)
#define PT_HOST 1
#include "proptroller/sim/sim.h"
#endif

using namespace pt;

// The ESP32's USB serial receive buffer (start()): 4 KB holds what arrives in 0.35 s at 115200 baud while loop() is
// busy (a flash write takes about 0.1 s, a state line about 0.12 s on the wire, and none starts while a request
// line comes in). Every KB here is a KB less for the largest backup the board takes over the network.
#ifndef PT_SERIAL_RX_BYTES
#define PT_SERIAL_RX_BYTES 4096
#endif

// Board support (pins.h "Another board"): a board support package, a folder with proptroller_board.h on the include
// path (build_flags = -I path/to/bsp), or -D PROPTROLLER_BOARD_HEADER='"my_board.h"', defines constexpr pt::PinMap PROPTROLLER_BOARD_MAP; it is checked
// here as the built-in maps are. Without one, the Proptroller board's own map.
#if PT_BOARD && !defined(PROPTROLLER_BOARD_HEADER) && defined(__has_include)
#if __has_include(<proptroller_board.h>)
#define PROPTROLLER_BOARD_HEADER <proptroller_board.h>
#endif
#endif
#if PT_BOARD && defined(PROPTROLLER_BOARD_HEADER)
#include PROPTROLLER_BOARD_HEADER
#if __cplusplus >= 201402L  // the checks need C++14 (every PlatformIO env of this project builds as C++17)
static_assert(pinsDistinct(PROPTROLLER_BOARD_MAP), "board profile: a GPIO serves two board functions");
static_assert(outputsCanDrive(PROPTROLLER_BOARD_MAP), "board profile: an ESP32 output on an input-only GPIO (34-39)");
static_assert(internalPullsPossible(PROPTROLLER_BOARD_MAP), "board profile: an ESP32 input on GPIO34-39 needs external_pull");
#endif
#endif

static Proptroller* g_prop = nullptr;  // for proptrollerMemoryReport()
#if PT_BOARD
static uint64_t g_ready_ms = 0;        // millis at the end of begin()
static volatile uint64_t g_step_at = 0;  // the end of the latest step or input sample (any core)
// RP2040: one hardware watchdog (8 s) for both cores, fed only while the step is alive (stepAlive), from either
// core: a hung core 1 restarts the board, a stalled network on core 0 does not.
static const uint32_t RP2040_WATCHDOG_MS = 8000;
static void feedWatchdog() {
#if defined(ARDUINO_ARCH_RP2040)
  if (stepAlive(millis64(), g_step_at)) watchdog_update();
#endif
}
#endif
#if PT_BOARD && defined(ESP32)
static TaskHandle_t g_tick_task = nullptr;
static const uint32_t TICK_TASK_STACK = 8192;  // bytes, the step task
// The loop task (requests, the network, the serial port): the worst static path is about 3.9 KB
// (tools/stack_check.py); 12 KB leaves room for what the static walk cannot see (LittleFS's internals, the C
// library). Instead of the core's 8 KB; a sketch that sets SET_LOOP_TASK_STACK_SIZE itself cannot also link this.
static const uint32_t PROPTROLLER_LOOP_STACK = 12 * 1024;
size_t getArduinoLoopTaskStackSize(void) { return PROPTROLLER_LOOP_STACK; }
#endif

static Channel ch(PropOutput o) { return ALL_CHANNELS[(int)o]; }
static Input in(PropInput i) { return ALL_INPUTS[(int)i]; }
static Rgb rgbOf(Color c) { return Rgb(c.red, c.green, c.blue); }

static void logLine(const char* s) {
#if defined(ARDUINO)
  Serial.print("[proptroller] ");
  Serial.println(s);
#else
  printf("[proptroller] %s\n", s);
#endif
}

// ------------------------------------------------------------------ the firmware as the network sees it

// Forwards to the controller and adds the sketch's own MQTT topics.
class PropApp : public NetApp {
 public:
  PropApp(NetApp& c, std::vector<std::pair<std::string, MessageHook>>& subs) : c_(&c), subs_(subs) {}
  void rebind(NetApp& c) { c_ = &c; }  // the simulator's controller after a restart
  std::string call(const char* b, size_t n) override { return c_->call(b, n); }
  // the USB serial port: the console and local JSON (no page lock)
  std::string consoleLine(const std::string& l) override { return c_->consoleLine(l); }
  std::string callSerial(const char* b, size_t n) override { return c_->callSerial(b, n); }
  std::string callFrom(uint32_t ss, const char* b, size_t n) override { return c_->callFrom(ss, b, n); }
  void sessionClosed(uint32_t ss) override { c_->sessionClosed(ss); }
  uint32_t wsHeartbeatMs() override { return c_->wsHeartbeatMs(); }
  std::string infoJson() override { return c_->infoJson(); }
  std::string stateJson() override { return c_->stateJson(); }
  uint32_t eventsSince(uint32_t s, std::vector<std::string>& o) override { return c_->eventsSince(s, o); }
  std::string announceJson(uint16_t port) override { return c_->announceJson(port); }
  std::vector<std::pair<std::string, std::string>> txt() override { return c_->txt(); }
  void netChanged(const NetState& n) override { c_->netChanged(n); }
  NetConfig netConfig() override { return c_->netConfig(); }
  std::vector<std::string> takeCuesOut() override { return c_->takeCuesOut(); }
  void cueIn(const std::string& n) override { c_->cueIn(n); }
  void wifiScanReady(bool now) override { c_->wifiScanReady(now); }
  bool wifiScanWanted() override { return c_->wifiScanWanted(); }
  void wifiScanDone(const std::vector<WifiNetwork>& f, bool ok) override { c_->wifiScanDone(f, ok); }
  bool mqttTarget(std::string& h, uint16_t& p) override { return c_->mqttTarget(h, p); }
  uint32_t mqttEpoch() override { return c_->mqttEpoch(); }
  std::string mqttClientId() override { return c_->mqttClientId(); }
  MqttMsg mqttWill() override { return c_->mqttWill(); }
  std::vector<MqttMsg> mqttOnConnect() override { return c_->mqttOnConnect(); }
  std::vector<std::string> mqttSubscriptions() override {
    auto v = c_->mqttSubscriptions();
    for (auto& s : subs_) v.push_back(s.first);
    return v;
  }
  std::vector<MqttMsg> mqttOnMessage(const std::string& t, const std::string& p) override {
    for (auto& s : subs_)
      if (s.first == t) { s.second(t.c_str(), p.c_str()); return {}; }
    return c_->mqttOnMessage(t, p);
  }
  std::vector<MqttMsg> mqttPoll() override { return c_->mqttPoll(); }
  const WebAsset* asset(const char* p) override { return c_->asset(p); }
  bool updateBegin(const UpdateRequest& q, std::string& a) override { return c_->updateBegin(q, a); }
  bool updateWrite(const uint8_t* d, size_t n, std::string& a) override { return c_->updateWrite(d, n, a); }
  std::string updateFinish() override { return c_->updateFinish(); }
  void updateAbort() override { c_->updateAbort(); }

 private:
  NetApp* c_;
  std::vector<std::pair<std::string, MessageHook>>& subs_;
};

// ------------------------------------------------------------------ platforms

#if PT_BOARD
struct Proptroller::Platform {
#if defined(PROPTROLLER_BOARD_HEADER)
  const PinMap& map = PROPTROLLER_BOARD_MAP;
  int led = PROPTROLLER_BOARD_MAP.status_led == NO_PIN ? -1 : PROPTROLLER_BOARD_MAP.status_led;
#elif defined(ESP32)
  const PinMap& map = ESP32_DEVKIT;
  int led = ESP32_DEVKIT.status_led;
#elif defined(PICO_CYW43_SUPPORTED)
  const PinMap& map = PICO;
  int led = LED_BUILTIN;  // on the Pico W the LED is on the radio chip
#else
  const PinMap& map = PICO;
  int led = PICO.status_led;
#endif
  ArduinoPins pins{map, led};
  SerialStream* uart = nullptr;
  BoardPixels pixels{map.pixel};
  LittleFsIo fs;
  ABStorage store{fs};
  ABStorage script_store{fs, "/script-a.bin", "/script-b.bin", SCRIPT_STORE_CAPACITY};
  ABStorage run_store{fs, "/runs-a.bin", "/runs-b.bin", RUNS_STORE_CAPACITY};
  BoardDmx dmx{map.exp[0]};  // expansion S4
#if defined(PT_HAS_OTA)
  BoardOta ota;
#endif
  struct Analog : AnalogIn {
    uint8_t pin;
    explicit Analog(uint8_t p) : pin(p) {}
    bool readRaw(uint16_t& out) override {
      if (pin == NO_PIN) return false;
      out = (uint16_t)analogRead(pin);
      return true;
    }
  } analog{map.analog_in};
  Lock lk;
  PropApp* app = nullptr;
#if PT_WIFI
  ArduinoNet* net = nullptr;
#endif
  // JSON lines over the USB serial port (the Pico's protocol; harmless elsewhere)
  SerialLine line;
  bool serial_client = false;
  uint32_t serial_seq = 0;
  uint64_t serial_state_hash = 0;
  Millis serial_state_at = 0;       // the last poll
  Millis serial_state_sent_at = 0;  // the last state line written
};
#elif PT_HOST
struct Proptroller::Platform {
  Sim sim{"host"};
  // a firmware of your own runs on this simulated board instead
  SimWorld world;
  SimPins pins{world};
  SimUart uart{world};
  SimPixels pixels{world};
  MemStorage store;
  struct Analog : AnalogIn {
    SimWorld& w;
    explicit Analog(SimWorld& x) : w(x) {}
    bool readRaw(uint16_t& out) override { out = w.analog; return true; }
  } analog{world};
  bool custom = false;
  PropApp* app = nullptr;
};
#endif

#if PT_BOARD
// One wake of the step (every 1 ms): a tick every tickPeriodMs, an input sample in between (SM-T1); after a
// stall, no burst of steps. True when it ticked.
bool proptrollerStep(Proptroller& p, uint64_t now, uint64_t& next) {
  if (now < next) {
    p.sample(now);
    return false;
  }
  uint32_t period = p.fw_->tickPeriodMs();
  next = (next == 0 ? now : next) + period;
  if (next + 50 < now) next = now + period;
  p.tick(now);
  return true;
}
#endif

#if PT_BOARD && defined(ESP32)
#if defined(PROPTROLLER_QEMU_ETH)
// QEMU only (QEMU.md "Network"): the time from one step to the next, reported and cleared by `mem`
static volatile uint32_t g_gap_max = 0, g_gap_over15 = 0;
#endif
static void tickTask(void* arg) {
  Proptroller& p = *(Proptroller*)arg;
  TickType_t last = xTaskGetTickCount();
  TickType_t period = pdMS_TO_TICKS(1);
  if (period < 1) period = 1;
  Millis next = 0;
  for (;;) {
    vTaskDelayUntil(&last, period);
    esp_task_wdt_reset();
    if (!proptrollerStep(p, millis64(), next)) continue;
#if defined(PROPTROLLER_QEMU_ETH)
    static Millis prev = 0;
    Millis t = millis64();
    if (prev) {
      uint32_t g = (uint32_t)(t - prev);
      if (g > g_gap_max) g_gap_max = g;
      if (g > 15) g_gap_over15 = g_gap_over15 + 1;
    }
    prev = t;
#endif
  }
}
#endif

#if PT_BOARD && defined(ARDUINO_ARCH_RP2040) && !defined(PROPTROLLER_NO_CORE1)
// The 10 ms step runs on core 1, the network and loop() on core 0.
static Proptroller* g_core1 = nullptr;
// Core 0's stack (setup, loop: requests, the network, the serial port) is the 8 KB of scratch X + Y below
// __StackTop (core 1 has its own on the heap) and has no hardware guard: it is painted at boot, `mem` reports its
// high-water mark, and loop() checks a guard band at its bottom: when that is written the stack has run into
// whatever lies below, and the board restarts (outputs off) instead of running on with corrupted memory.
extern "C" char __StackTop, __scratch_x_end__;
static const uint32_t STACK_PAINT = 0x5354434Bu;  // "STCK"
static const unsigned STACK_GUARD_WORDS = 8;
static uint32_t* stackBottom() { return (uint32_t*)(((uintptr_t)&__scratch_x_end__ + 3) & ~(uintptr_t)3); }
static void stackPaint() {
  uint32_t here;
  uint32_t* top = (uint32_t*)((uintptr_t)&here - 256);  // well below this frame
  for (uint32_t* p = stackBottom(); p < top; p++) *p = STACK_PAINT;
}
static unsigned stackFree() {
  uint32_t* p = stackBottom();
  while (p < (uint32_t*)&__StackTop && *p == STACK_PAINT) p++;
  return (unsigned)((uintptr_t)p - (uintptr_t)stackBottom());
}
static bool stackGuardHit() {
  uint32_t* p = stackBottom();
  for (unsigned i = 0; i < STACK_GUARD_WORDS; i++)
    if (p[i] != STACK_PAINT) return true;
  return false;
}
// The step on core 1 gets its own 8 KB stack instead of the 4 KB in scratch memory (the show engine, the effect
// and the JSON events run there).
bool core1_separate_stack = true;
void setup1() {}
void loop1() {
  static Millis next = 0, sampled = 0;
  if (!g_core1) return;
  Millis now = millis64();
  if (now == sampled) return;  // once per millisecond
  sampled = now;
  proptrollerStep(*g_core1, now, next);
  feedWatchdog();
}
#endif

#if PT_HOST
Sim& Proptroller::hostSim() { return plat_->sim; }
NetApp& Proptroller::hostApp() { return *plat_->app; }
SimWorld& Proptroller::hostWorld() { return plat_->world; }
#endif

Proptroller::Proptroller() {}
Proptroller::~Proptroller() {
  ProptrollerAuto::stop(*this);
  // (on a board the Proptroller lives as long as the power; on the host, tests make and drop many)
#if PT_BOARD || PT_HOST
  if (plat_) {
    delete plat_->app;
#if PT_BOARD
    delete fw_;  // made by the maker (the generic controller or the sketch's own)
#else
    if (plat_->custom) delete fw_;  // the generic one is the simulator's
#endif
    delete plat_;
  }
#endif
  delete adv_;
}

PropAdvanced& Proptroller::advanced() {
  if (!adv_) adv_ = new PropAdvanced(*this);
  return *adv_;
}

// The generic controller is made only here, so a sketch with a firmware of its own does not carry it.
static Firmware* makeGeneric(BoardParts& p) {
#if PT_HOST
  (void)p;
  return nullptr;  // host builds use the simulator's controller (see start)
#else
  Controller* c = new Controller(*p.pins, *p.uart, *p.pixels, p.store, p.identity);
  c->setScriptStore(p.script_store);
  c->setRunStore(p.run_store);
  c->setDmx(p.dmx);
  if (p.update) c->setUpdateTarget(p.update);
  return c;
#endif
}

void Proptroller::begin() {
  generic_ = true;
  start(makeGeneric);
}
void Proptroller::begin(FirmwareMaker make) {
  if (started_) logLine("prop.advanced().begin(maker) must come first in setup(): the generic controller already runs");
  start(make);
}

void Proptroller::start(FirmwareMaker make) {
  if (started_) return;
  advanced();  // made here, before the step task or core 1 can ask for it
#if PT_BOARD
  plat_ = new Platform();
  // 1. outputs off, before anything slow
  outputsOffNow(plat_->map);
#if defined(ARDUINO_ARCH_RP2040) && !defined(PROPTROLLER_NO_CORE1)
  stackPaint();
#endif
  plat_->pins.begin();
#if defined(ESP32)
  // USB serial (a UART behind a USB bridge on the DevKit): a request of several KB (a backup, a script) arrives at
  // 11.5 KB/s while loop() may be busy for a while (a flash write, a state line going out). The core's 256 B
  // receive buffer lost bytes from the middle of such lines (first hardware run, 2026-10-22). Before begin().
  Serial.setRxBufferSize(PT_SERIAL_RX_BYTES);
#endif
  Serial.begin(115200);
#if defined(ARDUINO_ARCH_RP2040)
  analogReadResolution(12);
#endif
  // 2. storage, sound port, pixels
  MountResult fsr = plat_->fs.begin();
  if (fsr == MountResult::Formatted) logLine("the flash file system was empty and has been formatted");
  if (fsr == MountResult::Refused)
    logLine("could not open the flash file system and left it as it is: settings are not saved (a factory reset formats it)");
  plat_->uart = new SerialStream(openDfSerial(plat_->map));
  if (!plat_->pixels.begin()) logLine("pixel driver not available");
  BoardParts parts;
  parts.pins = &plat_->pins;
  parts.uart = plat_->uart;
  parts.pixels = &plat_->pixels;
  parts.store = &plat_->store;
  parts.script_store = &plat_->script_store;
  parts.run_store = &plat_->run_store;
  parts.dmx = &plat_->dmx;
  parts.analog = &plat_->analog;
#if defined(PT_HAS_OTA)
  parts.update = &plat_->ota;
#endif
  parts.identity.id = deviceId();
  parts.identity.mcu = mcuName();
#elif PT_HOST
  plat_ = new Platform();
  BoardParts parts;
  parts.pins = &plat_->pins;
  parts.uart = &plat_->uart;
  parts.pixels = &plat_->pixels;
  parts.store = &plat_->store;
  parts.analog = &plat_->analog;
  parts.identity.id = "host";
  parts.identity.mcu = "sim";
  parts.identity.extra_caps = {"sim"};
#endif
  if (!generic_) {
    fw_ = make(parts);
#if PT_BOARD && defined(ESP32)
    // a firmware of its own has no self-check (verifyRollbackLater() is in the library): an image that came
    // over the air is marked valid at boot, the core's default
    esp_ota_mark_app_valid_cancel_rollback();
#endif
#if PT_HOST
    plat_->custom = true;
#endif
  } else {
#if PT_BOARD
    ctl_ = (Controller*)make(parts);
#else
    (void)make;
    ctl_ = &plat_->sim.ctl();
#endif
    fw_ = ctl_;
  }
  plat_->app = new PropApp(*fw_, subs_);
#if PT_BOARD
  char b[160];
  snprintf(b, sizeof b, "Proptroller %s on %s, id %s", PROPTROLLER_FW_VERSION_STR, parts.identity.mcu.c_str(), parts.identity.id.c_str());
  logLine(b);
  // 3. the 10 ms step
  {
#if defined(ESP32)
    xTaskCreatePinnedToCore(tickTask, "proptroller", TICK_TASK_STACK, this, 5, &g_tick_task, 1);
    // a hung step restarts the board (task watchdog, 5 s, panic on: sdkconfig CONFIG_ESP_TASK_WDT_PANIC)
    esp_task_wdt_add(g_tick_task);
#elif !defined(PROPTROLLER_NO_CORE1)
    g_core1 = this;
#endif
  }
#if defined(ARDUINO_ARCH_RP2040) && !defined(PT_PICOBOOT)
  watchdog_enable(RP2040_WATCHDOG_MS, true);  // behind the Pico bootloader it runs from power-on (arduino/ota.cpp)
#endif
#endif
  if (ctl_) ctl_->board().hook_user = this;
#if PT_BOARD
  if (ctl_) ctl_->setMemProbe(proptrollerMemoryReport);  // the console's `mem`
#if defined(ESP32)
  if (ctl_) ctl_->setHeapProbe([]() -> size_t { return ESP.getFreeHeap(); });
  if (ctl_) ctl_->setBlockProbe([]() -> size_t { return ESP.getMaxAllocHeap(); });
  plat_->line.setBlockProbe([]() -> size_t { return ESP.getMaxAllocHeap(); });
#else
  if (ctl_) ctl_->setHeapProbe([]() -> size_t { return rp2040.getFreeHeap(); });
#endif
  // long request work (parsing, script checks) gives the lock back, so the step keeps its 10 ms
  if (ctl_)
    ctl_->setLockHooks([](void* l) { return ((Lock*)l)->releaseForWindow(); }, [](void* l, int n) { ((Lock*)l)->retakeAfterWindow(n); }, &plat_->lk);
#endif
  started_ = true;  // the step runs (and feeds the watchdog) from here, also while Wi-Fi starts
#if PT_BOARD && PT_WIFI
  // 4. Wi-Fi, web UI, discovery, mDNS, MQTT
  plat_->net = new ArduinoNet(*plat_->app, plat_->lk);
  plat_->net->begin();
#endif
  g_prop = this;
#if PT_BOARD
  g_ready_ms = millis64();
  logLine(("ready after " + std::to_string((unsigned long)g_ready_ms) + " ms; " + memoryReport()).c_str());
#endif
}

// Free heap and stack high-water marks, for the bench: "free heap 182344 B (min 179020, largest block 110580);
// stack free: loop 5412 B, step 6120 B; ready after 812 ms".
std::string Proptroller::memoryReport() {
  char b[200];
#if PT_BOARD && defined(ESP32)
  unsigned loop_free = (unsigned)uxTaskGetStackHighWaterMark(xTaskGetHandle("loopTask"));
  unsigned step_free = g_tick_task ? (unsigned)uxTaskGetStackHighWaterMark(g_tick_task) : 0;
  snprintf(b, sizeof b, "free heap %u B (min %u, largest block %u); stack free: loop %u B, step %u B; ready after %lu ms",
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), loop_free, step_free,
           (unsigned long)g_ready_ms);
#if defined(PROPTROLLER_QEMU_ETH)
  size_t k = strlen(b);
  snprintf(b + k, sizeof b - k, "; step gap max %u ms, %u over 15 ms", (unsigned)g_gap_max, (unsigned)g_gap_over15);
  g_gap_max = 0;
  g_gap_over15 = 0;
#endif
#elif PT_BOARD && defined(ARDUINO_ARCH_RP2040) && !defined(PROPTROLLER_NO_CORE1)
  snprintf(b, sizeof b, "free heap %u B of %u (used %u); stack free: core 0 %u of %u B (core 1 has its own 8 KB); ready after %lu ms",
           (unsigned)rp2040.getFreeHeap(), (unsigned)rp2040.getTotalHeap(), (unsigned)rp2040.getUsedHeap(), stackFree(),
           (unsigned)((uintptr_t)&__StackTop - (uintptr_t)stackBottom()), (unsigned long)g_ready_ms);
#elif PT_BOARD && defined(ARDUINO_ARCH_RP2040)
  snprintf(b, sizeof b, "free heap %u B of %u (used %u); ready after %lu ms", (unsigned)rp2040.getFreeHeap(),
           (unsigned)rp2040.getTotalHeap(), (unsigned)rp2040.getUsedHeap(), (unsigned long)g_ready_ms);
#else
  (void)snprintf(b, sizeof b, "memory report: on the boards only");
#endif
  return b;
}

std::string proptrollerMemoryReport() { return g_prop ? g_prop->advanced().memoryReport() : std::string("not started"); }

bool Proptroller::needController(const char* what) {
  if (!started_) ProptrollerAuto::start(*this);
  if (ctl_) return true;
  std::string m = std::string(what) + " needs the generic controller (this sketch runs its own firmware)";
  say(m.c_str());
  return false;
}

// The host: the mutex the background thread steps a controller under (ProptrollerAuto), so a sketch's calls on
// the test's thread are ordered with it as on a board (recursive, and free when nothing runs in the background).
void Proptroller::lock() {
#if PT_BOARD
  plat_->lk.take();
#elif PT_HOST
  proptrollerHostMutex().lock();
#endif
}
// The step's take: it runs while a request has the lock given back (window_lock.h); everything else waits.
void Proptroller::stepLock() {
#if PT_BOARD
  plat_->lk.takeStep();
#elif PT_HOST
  proptrollerHostMutex().lock();
#endif
}
void Proptroller::unlock() {
#if PT_BOARD
  plat_->lk.give();
#elif PT_HOST
  proptrollerHostMutex().unlock();
#endif
}

void Proptroller::runTasks(uint64_t now) {
  for (auto t : tasks_) t->update(advanced(), now);
}

void Proptroller::tick(uint64_t now) {
  if (!started_) return;
  stepLock();
#if PT_HOST
  (void)now;
  if (plat_->custom) {
    Millis up = plat_->world.now + fw_->tickPeriodMs();
    plat_->world.now = up;
    auto bytes = plat_->world.df.tick(up);
    plat_->world.uart_rx.insert(plat_->world.uart_rx.end(), bytes.begin(), bytes.end());
    fw_->tick(up);
    runTasks(up);
  } else {
    plat_->sim.tick();
    // a simulated restart builds a new controller: follow it
    if (ctl_ != &plat_->sim.ctl()) {
      ctl_ = &plat_->sim.ctl();
      fw_ = ctl_;
      plat_->app->rebind(*fw_);
    }
    runTasks(plat_->sim.uptime());
  }
#else
  bool reboot;
  {
    Controller::StepScope in_step(ctl_);  // the sketch's tasks run in the step too: never a window there
    fw_->tick(now);
    runTasks(now);
    reboot = fw_->rebootRequested();
  }
  g_step_at = millis64();
#endif
  unlock();
#if PT_BOARD
  if (reboot) {
#if PT_WIFI
    if (plat_->net) plat_->net->stop();  // the Wi-Fi going down now is no failed join (no "cannot join" in the log)
#endif
    logLine("restarting");
    delay(200);
    restartNow();
  }
#endif
}

void Proptroller::sample(uint64_t now) {
  if (!started_) return;
#if PT_BOARD
  stepLock();
  {
    Controller::StepScope in_step(ctl_);
    fw_->sample(now);
  }
  unlock();
  g_step_at = millis64();
#else
  (void)now;
#endif
}

void Proptroller::update() {
  if (ProptrollerAuto::skipUpdate(*this)) return;
  if (!started_) return;
#if PT_BOARD
  // the watchdog watches the step, not loop(): ESP32 the step task (task watchdog, 5 s), RP2040 feedWatchdog
  feedWatchdog();
#if defined(ARDUINO_ARCH_RP2040) && !defined(PROPTROLLER_NO_CORE1)
  if (stackGuardHit()) {
    logLine("core 0 stack overflow: restarting");
    outputsOffNow(plat_->map);
    delay(100);
    restartNow();
  }
#endif
  Millis now = millis64();
#if defined(ARDUINO_ARCH_RP2040) && defined(PROPTROLLER_NO_CORE1)
  // core 1 is the sketch's: the step runs here
  static Millis next = 0;
  static Millis sampled = 0;
  if (now >= next) { next = now + fw_->tickPeriodMs(); sampled = now; tick(now); }
  else if (now != sampled) { sampled = now; sample(now); }
#endif
  // the flash writes the step decided on: here, so the step never waits on the flash (the lock is given back
  // while it is written)
  if (ctl_) { Guard g(plat_->lk); ctl_->background(); }
#if PT_WIFI
  plat_->net->poll();
#endif
  // a firmware with its own console gets every byte
  if (Serial.available() > 0) {
    uint8_t buf[64];
    int n = 0;
    while (n < (int)sizeof buf && Serial.available() > 0) buf[n++] = (uint8_t)Serial.read();
    std::string out;
    bool taken;
    { Guard g(plat_->lk); taken = fw_->serialIn(buf, (size_t)n, out); }
    if (taken) {
      if (!out.empty()) Serial.write((const uint8_t*)out.data(), out.size());
      return;
    }
    for (int i = 0; i < n; i++) serialByte(buf[i]);
  }
  serialPush(now);
#elif PT_HOST
  tick(0);
#endif
}

#if PT_BOARD
// JSON lines on the USB serial port: requests in, answers, events and the state out
void Proptroller::serialByte(uint8_t c) {
  SerialLine& line = plat_->line;
  SerialLine::Result r = line.push(c);
  if (r == SerialLine::TOO_LONG) {
    // the line over the limit was dropped as it came in: answered once, never handled as a shorter request
    std::string why = "the line is longer than " + std::to_string(line.limit()) + " bytes: nothing was done";
    Serial.println(Response::err(false, 0, ErrorCode::BadRequest, why).json().c_str());
  } else if (r == SerialLine::NO_MEMORY) {
    Serial.println(Response::err(false, 0, ErrorCode::Refused, "not enough memory for a line this long: nothing was done; restart the board and try again").json().c_str());
  } else if (r == SerialLine::LINE) {
    if (line.data()[0] == '{') {
      std::string resp;
      { Guard g(plat_->lk); resp = plat_->app->callSerial(line.data(), line.size()); }
      line.done();  // the long line's memory goes back before the answer is written
      Serial.println(resp.c_str());
      if (!plat_->serial_client) {
        plat_->serial_client = true;
        std::vector<std::string> ignore;
        Guard g(plat_->lk);
        plat_->serial_seq = plat_->app->eventsSince(0, ignore);
      }
    } else if (line.equals("mem")) {
      line.done();
      Serial.println(memoryReport().c_str());
    } else {
      // the console: "key=value", "key", "trigger", "recover", "cancel", "restart"
      std::string cmd(line.data(), line.size());
      line.done();
      std::string resp;
      { Guard g(plat_->lk); resp = plat_->app->consoleLine(cmd); }
      if (!resp.empty()) Serial.println(resp.c_str());
    }
  }
}

// Events and the state for a JSON client, every 250 ms at most. The state line (about 1.3 KB) is written when it
// changed other than in uptime_ms (stateHash), or every 5 s; nothing is written while a request line is coming in,
// so the client's request is read as it arrives.
void Proptroller::serialPush(uint64_t now) {
  if (!plat_->serial_client || now - plat_->serial_state_at < 250 || plat_->line.receiving()) return;
  std::vector<std::string> evs;
  std::string st;
  {
    Guard g(plat_->lk);
    plat_->serial_seq = plat_->app->eventsSince(plat_->serial_seq, evs);
    st = stateEvent(plat_->app->stateJson());
  }
  for (auto& e : evs) Serial.println(e.c_str());
  uint64_t h = stateHash(st);
  if (h != plat_->serial_state_hash || now - plat_->serial_state_sent_at >= 5000) {
    Serial.println(st.c_str());
    plat_->serial_state_hash = h;
    plat_->serial_state_sent_at = now;
  }
  plat_->serial_state_at = now;
}
#endif

uint64_t Proptroller::timeMs() {
#if PT_HOST
  if (plat_ && plat_->custom) return plat_->world.now;
  return plat_ ? plat_->sim.uptime() : 0;
#elif PT_BOARD
  return millis64();
#else
  return 0;
#endif
}

// ------------------------------------------------------------------ direct control

void Proptroller::turnOn(PropOutput o) { if (!needController("this")) return;
  lock(); ctl_->engine().setManual(ch(o), true, FULL); unlock(); }
void Proptroller::turnOff(PropOutput o) { if (!needController("this")) return;
  lock(); ctl_->engine().setManual(ch(o), true, 0); unlock(); }
void Proptroller::dim(PropOutput o, Percent l) {
  if (!needController("dim()")) return;
  if (!isPwm(ch(o)) && l.permille > 0 && l.permille < FULL) {
    say("only OUT1-OUT4 can dim: relays and contacts are on or off");
    return;
  }
  lock();
  ctl_->engine().setManual(ch(o), true, l.permille);
  unlock();
}
void Proptroller::release(PropOutput o) { if (!needController("this")) return;
  lock(); ctl_->engine().setManual(ch(o), false, 0); unlock(); }
bool Proptroller::isPressed(PropInput i) {
  if (!needController("isPressed()")) return false;
  lock(); bool a = ctl_->isActive(in(i)); unlock(); return a; }
bool Proptroller::wasPressed(PropInput i) {
  if (!needController("wasPressed()")) return false;
  lock();
  uint32_t n = ctl_->presses[(int)i];
  unlock();
  bool yes = n != seen_[(int)i];
  seen_[(int)i] = n;
  return yes;
}
void Proptroller::playSound(int number) { playSound(2, number); }
void Proptroller::playSound(int folder, int number) {
  if (!needController("playSound()")) return;
  lock();
  ctl_->engine().sound(SoundCommand::playFolder((uint8_t)folder, (uint8_t)number));
  unlock();
}
void Proptroller::stopSound() { if (!needController("this")) return;
  lock(); ctl_->engine().sound(SoundCommand::simple(SoundCommand::Kind::Stop)); unlock(); }
void Proptroller::setVolume(int v) { if (!needController("this")) return;
  lock(); ctl_->engine().sound(SoundCommand::volume((uint8_t)v)); unlock(); }
void Proptroller::sendCue(const char* name) { if (!needController("this")) return;
  lock(); ctl_->engine().cue(ctl_->now(), name); unlock(); }
void Proptroller::stopEverything() { if (!needController("this")) return;
  lock(); ctl_->engine().stopAll(ctl_->now()); unlock(); }

static void applySettings(Proptroller& p, Controller& c, const std::vector<ParamValue>& v, const char* what);

// as setting(): written only when the sketch changed it since the last boot (no flash write at every boot, and a
// value set from the phone meanwhile stays)
void Proptroller::triggerHoldTime(Duration d) {
  if (!needController("triggerHoldTime()")) return;
  lock();
  applySettings(*this, *ctl_, {ParamValue{"trig_debounce_ms", Value::integer(d.ms)}, ParamValue{"trig_release_ms", Value::integer(d.ms)}},
                "triggerHoldTime");
  unlock();
}

void Proptroller::addTask(PropTask& t) { lock(); tasks_.push_back(&t); unlock(); }

void Proptroller::onMessage(const char* topic, MessageHook h) { subs_.push_back({topic, h}); }

bool Proptroller::publish(const char* topic, const char* payload, bool retain) {
#if PT_WIFI
  return plat_ && plat_->net && plat_->net->publish(topic, payload, retain);
#else
  (void)topic; (void)payload; (void)retain;
  return false;
#endif
}

void Proptroller::allowTriggerHeldAtPowerOn(PropInput i) {
  if (!needController("allowTriggerHeldAtPowerOn()")) return;
  lock(); ctl_->armNow(in(i)); unlock(); }

void Proptroller::say(const char* m) {
  logLine(m);
  if (ctl_) { lock(); ctl_->log(m); unlock(); }
}

void Proptroller::afterOutputChange(OutputChangeHook h) {
  after_ = h;
  if (!needController("afterOutputChange()")) return;
  lock();
  ctl_->board().after_change = h ? hookAfter : nullptr;
  unlock();
}

void Proptroller::hookAfter(Channel c, Level& l, void* user) {
  Proptroller* p = (Proptroller*)user;
  if (p && p->after_) p->after_((PropOutput)index(c), l);
}

void Proptroller::reportProblem(const std::string& what, const ShowError& e, const std::string& extra) {
  std::string m = "Problem with " + what + ": ";
  if (!extra.empty()) m += extra;
  else {
    m += problemHelp(e.problem);
    if (e.track >= 0) m += " (step on " + std::string(name(show_target_hint_)) + ")";
  }
  say(m.c_str());
}

// ------------------------------------------------------------------ sequences

// ---------------------------------------------------------------- the built-in effect

PulseAndRecover::PulseAndRecover() {
  set("effect", Value::text("pulse_and_recover"));
  set("trig_debounce_ms", Value::integer(TRIGGER_DEBOUNCE_MS));
  set("trig_release_ms", Value::integer(TRIGGER_DEBOUNCE_MS));
}
PulseAndRecover& PulseAndRecover::set(const char* key, const Value& v) {
  for (size_t i = 0; i < values_.size(); i++)
    if (values_[i].key == key) { values_.erase(values_.begin() + i); break; }
  values_.push_back(ParamValue{key, v});
  return *this;
}
PulseAndRecover& PulseAndRecover::pulseFor(Duration t) { return set("out1_on_ms", Value::integer(t.ms)); }
PulseAndRecover& PulseAndRecover::recoverFor(Duration t) { return set("out2_on_ms", Value::integer(t.ms)); }
PulseAndRecover& PulseAndRecover::runsBeforeRest(int n) { return set("runs_per_cooldown", Value::integer(n)); }
PulseAndRecover& PulseAndRecover::restFor(Duration t) { return set("cooldown_ms", Value::integer(t.ms)); }
PulseAndRecover& PulseAndRecover::afterPulseFor(Duration t) {
  set("out3_enabled", Value::boolean(true));
  return set("out3_on_ms", Value::integer(t.ms));
}
PulseAndRecover& PulseAndRecover::readyLampOnOut4() { return set("out4_ready_lamp", Value::boolean(true)); }
PulseAndRecover& PulseAndRecover::safetyOnAtPowerOn() { return set("auto_arm", Value::boolean(false)); }
PulseAndRecover& PulseAndRecover::holdToStart(Duration t) { return set("trig_debounce_ms", Value::integer(t.ms)); }
PulseAndRecover& PulseAndRecover::recoverRunsACycle() { return set("recover_mode", Value::text("cycle")); }
PulseAndRecover& PulseAndRecover::recoverRunsFor(Duration t) {
  set("recover_mode", Value::text("timed"));
  return set("recover_time_ms", Value::integer(t.ms));
}
PulseAndRecover& PulseAndRecover::recoverWhileHeld() { return set("recover_mode", Value::text("hold")); }
PulseAndRecover& PulseAndRecover::triggerOutPulse(Duration t) { return set("trigger_out_pulse_ms", Value::integer(t.ms)); }
PulseAndRecover& PulseAndRecover::releaseTime(Duration t) { return set("trig_release_ms", Value::integer(t.ms)); }

// setup() runs at every boot: a value is applied only when the sketch changed it since the last boot, so what the
// board was given meanwhile (the phone, MQTT, a config script) stays (SM-S2).
static void applySettings(Proptroller& p, Controller& c, const std::vector<ParamValue>& v, const char* what) {
  std::vector<ParamValue> changed;
  for (auto& pv : v)
    if (c.sketchChanged("set:" + pv.key, pv.value.plain())) changed.push_back(pv);
  if (changed.empty()) return;
  Request r;
  Response resp = c.setParams(r, changed);
  if (!resp.isOk()) { p.advanced().say((std::string(what) + ": " + resp.errorMessage()).c_str()); return; }
  for (auto& pv : changed) c.sketchApplied("set:" + pv.key, pv.value.plain());
}

void Proptroller::withPulseAndRecover(const PulseAndRecover& e) {
  if (!needController("withPulseAndRecover()")) return;
  lock();
  applySettings(*this, *ctl_, e.values(), "withPulseAndRecover");
  unlock();
}

void Proptroller::setting(const char* key, long v) {
  if (!needController("setting()")) return;
  lock();
  applySettings(*this, *ctl_, {ParamValue{key, Value::integer(v)}}, (std::string("setting ") + key).c_str());
  unlock();
}
void Proptroller::setting(const char* key, bool v) {
  if (!needController("setting()")) return;
  lock();
  applySettings(*this, *ctl_, {ParamValue{key, Value::boolean(v)}}, (std::string("setting ") + key).c_str());
  unlock();
}
void Proptroller::setting(const char* key, const char* v) {
  if (!needController("setting()")) return;
  lock();
  applySettings(*this, *ctl_, {ParamValue{key, Value::text(v)}}, (std::string("setting ") + key).c_str());
  unlock();
}

static void nameTerminal(Proptroller& p, Controller& c, const std::string& terminal, const char* name, const char* label) {
  std::string slot = "name:" + terminal, value = std::string(name) + "|" + (label ? label : "");
  if (!c.sketchChanged(slot, value)) return;
  JsonDocument d;
  d["op"] = "set_profile";
  JsonObject data = d["data"].to<JsonObject>();
  c.settings().profile.toJson(data);
  JsonArray a = data["aliases"];
  for (size_t i = a.size(); i-- > 0;)
    if (a[i]["terminal"] == terminal) a.remove(i);
  JsonObject x = a.add<JsonObject>();
  x["alias"] = name;
  x["terminal"] = terminal;
  if (label && *label) x["label"] = label;
  std::string body;
  serializeJson(d, body);
  JsonDocument r;
  deserializeJson(r, c.call(body.data(), body.size()));
  if (!(r["ok"] | false)) p.advanced().say((std::string("name for ") + terminal + ": " + (r["error"]["message"] | "refused")).c_str());
  else c.sketchApplied(slot, value);
}

void Proptroller::nameInput(PropInput i, const char* name, const char* label) {
  if (!needController("nameInput()")) return;
  lock();
  nameTerminal(*this, *ctl_, pt::name(in(i)), name, label);
  unlock();
}
void Proptroller::nameOutput(PropOutput o, const char* name, const char* label) {
  if (!needController("nameOutput()")) return;
  lock();
  nameTerminal(*this, *ctl_, pt::name(ch(o)), name, label);
  unlock();
}

PropInput Proptroller::input(const char* n) {
  if (!started_) ProptrollerAuto::start(*this);
  std::string board = n;
  if (ctl_) { lock(); board = ctl_->terminalName(n); unlock(); }  // the profile can change on another task
  for (auto& c : board) c = (char)toupper((unsigned char)c);
  const char* const names[] = {"TRIG1", "TRIG2", "USER1", "USER2", "USER3"};
  for (int i = 0; i < 5; i++)
    if (board == names[i]) return (PropInput)i;
  say((std::string("there is no input called \"") + n + "\": use TRIG1, TRIG2, USER1-USER3 or an alias from the profile").c_str());
  return TRIG1;
}

bool Proptroller::loadProfile(const char* json) {
  if (!needController("loadProfile()")) return false;
  JsonDocument d;
  d["op"] = "set_profile";
  if (deserializeJson(d["data"].to<JsonVariant>(), json)) {
    say("loadProfile: that is not JSON");
    return false;
  }
  std::string body, table;
  serializeJson(d, body);
  serializeJson(d["data"], table);
  std::string resp;
  lock();
  if (!ctl_->sketchChanged("profile", table)) { unlock(); return true; }  // as loaded at an earlier boot
  resp = ctl_->call(body.data(), body.size());
  JsonDocument r;
  deserializeJson(r, resp);
  bool ok = r["ok"] | false;
  if (ok) {
    ctl_->sketchApplied("profile", table);
    ctl_->sketchForget("name:");  // the new table replaced any names the sketch gave
  }
  unlock();
  if (!ok) {
    say((std::string("loadProfile: ") + (r["error"]["message"] | "refused")).c_str());
    return false;
  }
  return true;
}

Sequence Proptroller::when(PropInput i) {
  Sequence s(this, nextShowId(), (std::string("when(") + name(in(i)) + ")").c_str());
  ShowTrigger t;
  t.on = ShowTrigger::Kind::OnInput;
  t.input = in(i);
  s.show_.triggers.push_back(t);
  return s;
}
Sequence Proptroller::every(Duration p) {
  Sequence s(this, nextShowId(), "every(...)");
  ShowTrigger t;
  t.on = ShowTrigger::Kind::Every;
  t.ms = p.ms;
  s.show_.triggers.push_back(t);
  return s;
}
Sequence Proptroller::atPowerOn(Duration d) {
  Sequence s(this, nextShowId(), "atPowerOn()");
  ShowTrigger t;
  t.on = ShowTrigger::Kind::Boot;
  t.ms = d.ms;
  s.show_.triggers.push_back(t);
  return s;
}
Sequence Proptroller::onCue(const char* n) {
  Sequence s(this, nextShowId(), (std::string("onCue(\"") + n + "\")").c_str());
  ShowTrigger t;
  t.on = ShowTrigger::Kind::Cue;
  t.name = n;
  s.show_.triggers.push_back(t);
  return s;
}

Sequence::Sequence(Proptroller* p, uint16_t id, const char* what) : prop_(p), what_(what) {
  show_.id = id;
  char b[48];
  (void)snprintf(b, sizeof b, "Sketch %u", (unsigned)(id - 100));
  show_.name = b;
}

Sequence::Sequence(Sequence&& o) noexcept : prop_(o.prop_), show_(std::move(o.show_)), at_(o.at_), end_(o.end_), what_(std::move(o.what_)), done_(o.done_) {
  o.done_ = true;
}

Sequence::~Sequence() {
  if (!done_) commit();
}

void Sequence::cue(Target t, const Action& a, uint32_t length) {
  Cue c;
  c.at = at_;
  c.action = a;
  track(t).cues.push_back(c);
  if (at_ + length > end_) end_ = at_ + length;
}

static Action act(Action::Kind k) {
  Action a;
  a.kind = k;
  return a;
}

Sequence& Sequence::turnOn(PropOutput o) { cue(Target::of(ch(o)), act(Action::Kind::On), 0); return *this; }
Sequence& Sequence::turnOn(PropOutput o, Duration d) {
  Action a = act(Action::Kind::Pulse);
  a.ms = d.ms;
  cue(Target::of(ch(o)), a, d.ms);
  return *this;
}
Sequence& Sequence::turnOff(PropOutput o) { cue(Target::of(ch(o)), act(Action::Kind::Off), 0); return *this; }
Sequence& Sequence::dim(PropOutput o, Percent l) {
  Action a = act(Action::Kind::SetLevel);
  a.level = l.permille;
  cue(Target::of(ch(o)), a, 0);
  return *this;
}
Sequence& Sequence::fadeTo(PropOutput o, Percent l, Duration d) {
  Action a = act(Action::Kind::Ramp);
  a.level = l.permille;
  a.ms = d.ms;
  cue(Target::of(ch(o)), a, d.ms);
  return *this;
}
Sequence& Sequence::blink(PropOutput o, int times, Duration on_for, Duration off_for) {
  Action a = act(Action::Kind::Blink);
  a.on_ms = on_for.ms;
  a.off_ms = off_for.ms;
  a.count = (uint16_t)(times < 0 ? 0 : times);
  cue(Target::of(ch(o)), a, a.duration());
  return *this;
}
Sequence& Sequence::playSound(int number) { return playSound(2, number); }
Sequence& Sequence::playSound(int folder, int number) {
  Action a = act(Action::Kind::Play);
  a.folder = (uint8_t)folder;
  a.track = (uint8_t)number;
  cue(Target::sound(), a, 0);
  return *this;
}
Sequence& Sequence::loopSoundsInFolder(int folder) {
  Action a = act(Action::Kind::LoopFolder);
  a.folder = (uint8_t)folder;
  cue(Target::sound(), a, 0);
  return *this;
}
Sequence& Sequence::setVolume(int v) {
  Action a = act(Action::Kind::Volume);
  a.volume = (uint8_t)v;
  cue(Target::sound(), a, 0);
  return *this;
}
Sequence& Sequence::stopSound() { cue(Target::sound(), act(Action::Kind::Off), 0); return *this; }
Sequence& Sequence::setPixels(Color c) { Action a = act(Action::Kind::Fill); a.color = rgbOf(c); cue(Target::pixel(), a, 0); return *this; }
Sequence& Sequence::flickerPixels(Color c) { Action a = act(Action::Kind::Flicker); a.color = rgbOf(c); cue(Target::pixel(), a, 0); return *this; }
Sequence& Sequence::flashPixels(Color c, Duration p) {
  Action a = act(Action::Kind::Flash);
  a.color = rgbOf(c);
  a.period_ms = p.ms;
  cue(Target::pixel(), a, 0);
  return *this;
}
Sequence& Sequence::chasePixels(Color c, Duration step) {
  Action a = act(Action::Kind::Chase);
  a.color = rgbOf(c);
  a.step_ms = step.ms;
  cue(Target::pixel(), a, 0);
  return *this;
}
Sequence& Sequence::rainbowPixels(Duration p) { Action a = act(Action::Kind::Rainbow); a.period_ms = p.ms; cue(Target::pixel(), a, 0); return *this; }
Sequence& Sequence::fadeInPixels(Color c, Duration d) {
  Action a = act(Action::Kind::Fade);
  a.color = rgbOf(c);
  a.ms = d.ms;
  cue(Target::pixel(), a, d.ms);
  return *this;
}
Sequence& Sequence::pixelsOff() { cue(Target::pixel(), act(Action::Kind::Off), 0); return *this; }
Sequence& Sequence::sendCue(const char* n) { Action a = act(Action::Kind::Emit); a.cue = n; cue(Target::net(), a, 0); return *this; }
Sequence& Sequence::then() { at_ = end_; return *this; }
Sequence& Sequence::wait(Duration d) {
  at_ += d.ms;
  if (at_ > end_) end_ = at_;
  return *this;
}
Sequence& Sequence::named(const char* n) { show_.name = n; return *this; }
Sequence& Sequence::cooldown(Duration d) { show_.cooldown_ms = d.ms; return *this; }
Sequence& Sequence::allowRunsPerCooldown(int n) { show_.runs_per_cooldown = (uint8_t)(n < 0 ? 0 : n > 255 ? 255 : n); return *this; }
Sequence& Sequence::lastsFor(Duration d) { show_.has_length = true; show_.length_ms = d.ms; return *this; }
Sequence& Sequence::repeatForever() { show_.looping = true; return *this; }
Sequence& Sequence::restartWhenTriggeredAgain() { show_.retrigger = Retrigger::Restart; return *this; }
Sequence& Sequence::stopWhenTriggeredAgain() { show_.retrigger = Retrigger::Stop; return *this; }
Sequence& Sequence::priority(int p) { show_.priority = (uint8_t)p; return *this; }
Sequence& Sequence::alsoWhen(PropInput i) {
  ShowTrigger t;
  t.on = ShowTrigger::Kind::OnInput;
  t.input = in(i);
  show_.triggers.push_back(t);
  return *this;
}
Sequence& Sequence::alsoOnCue(const char* n) {
  ShowTrigger t;
  t.on = ShowTrigger::Kind::Cue;
  t.name = n;
  show_.triggers.push_back(t);
  return *this;
}

bool Sequence::commit() {
  done_ = true;
  if (prop_ && !prop_->started_) ProptrollerAuto::start(*prop_);
  if (!prop_ || !prop_->fw_) {
    logLine("call prop.begin() before prop.when(...)");
    return false;
  }
  if (!prop_->ctl_) {
    logLine("prop.when(...) needs the generic controller (this sketch runs its own firmware)");
    return false;
  }
  // a show ends when its last step ends; a repeating show at least 100 ms
  if (!show_.has_length && end_ > show_.naturalLength()) { show_.has_length = true; show_.length_ms = end_; }  // a final wait(...)
  if (!show_.has_length && show_.looping && end_ < MIN_LOOP_MS) { show_.has_length = true; show_.length_ms = MIN_LOOP_MS; }
  std::string extra;
  for (auto& t : show_.tracks)
    if (t.target.isChannel() && prop_->ctl_->settings().role(t.target.channel) != OutputRole::Shows)
      extra = std::string(name(t.target.channel)) + " has another job (role '" + roleLabel(prop_->ctl_->settings().role(t.target.channel)) +
              "'). Give it the role 'shows' in Settings -> Output roles, or use a different output.";
  if (show_.runs_per_cooldown < 1 || show_.runs_per_cooldown > MAX_RUNS_PER_COOLDOWN) extra = "allowRunsPerCooldown takes 1 to 20.";
  ShowError e;
  if (extra.empty()) {
    prop_->lock();
    e = prop_->ctl_->addCodeShow(show_);
    prop_->unlock();
    if (e.ok()) return true;
    if (e.track >= 0 && e.track < (int)show_.tracks.size()) {
      prop_->show_target_hint_ = show_.tracks[e.track].target;
    }
  }
  prop_->reportProblem(what_, e, extra);
  return false;
}
