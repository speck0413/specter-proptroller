// The board on an Arduino core: pins, PWM, the DFPlayer serial line, the inverted PIXEL port, LittleFS
// storage, 64-bit time, a lock for the controller, and the device id. ESP32 (arduino-esp32 2.x) and
// RP2040 / RP2350 (arduino-pico).
#pragma once
#if defined(ARDUINO) && (defined(ESP32) || defined(ARDUINO_ARCH_RP2040))
#include <Arduino.h>
#include "../board.h"
#include "../dfplayer.h"
#include "../pins.h"
#include "../script.h"
#include "../storage_ab.h"
#include "../window_lock.h"
#include "../ws2812.h"

namespace pt {

// Milliseconds since power-on, 64 bits (never wraps).
Millis millis64();

// Drive every output pin low and the PIXEL pin to its idle level. The very first thing begin() does.
void outputsOffNow(const PinMap& m);

class ArduinoPins : public BoardPins {
 public:
  explicit ArduinoPins(const PinMap& m, int status_led_pin);
  void begin();  // PWM channels and input modes (outputs are already off)
  bool setOut(int out, Level level) override;
  bool setDigital(int which, bool on) override;
  bool readLow(int input, bool& low) override;
  bool hasOutput(int ch) override { return channelPin(m_, ch) != NO_PIN; }
  bool hasInput(int k) override { return inputPin(m_, k) != NO_PIN; }
  bool hasSound() override { return m_.df_tx != NO_PIN && m_.df_rx != NO_PIN; }
  bool hasPixels() override { return m_.pixel != NO_PIN; }
  bool hasStatusLed() override { return led_ >= 0; }
  bool setStatusLed(bool on) override;
  const PinMap& map() const { return m_; }

 private:
  PinMap m_;
  int led_;
};

// The DFPlayer serial port (9600 8N1).
class SerialStream : public ByteStream {
 public:
  explicit SerialStream(Stream& s) : s_(s) {}
  int available() override { return s_.available(); }
  int read() override { return s_.read(); }
  size_t write(const uint8_t* b, size_t n) override { return s_.write(b, n); }

 private:
  Stream& s_;
};
// Opens the DFPlayer port on the board's pins and returns it.
Stream& openDfSerial(const PinMap& m);

// The PIXEL port with the board's inversion (ESP32: RMT idle high and inverted items; RP2040: PIO with the
// pad output inverted).
class BoardPixels : public PixelOut {
 public:
  explicit BoardPixels(uint8_t pin) : pin_(pin) {}
  bool begin();
  bool write(const Rgb* px, size_t n) override;

 private:
  uint8_t pin_;
  bool ok_ = false;
#if defined(ARDUINO_ARCH_RP2040)
  void* pio_ = nullptr;
  unsigned sm_ = 0;
#endif
};

// The show script's DMX port on expansion S4 (SHOW-SCRIPT.md 7.4): 250 kbaud 8N2 into a MAX485 module.
// ESP32: UART1 on GPIO15 (a strapping pin: it is touched only after start(), long after boot), the break sent
// by the UART driver after each frame. Pico: a PIO UART on GP3, the break and mark-after-break driven on the
// pin, the 513 bytes fed by DMA. send() never blocks for the frame.
class BoardDmx : public DmxOut {
 public:
  explicit BoardDmx(uint8_t pin) : pin_(pin) {}
  void start() override;
  void stop() override;
  void send(const uint8_t* slots, size_t n) override;

 private:
  uint8_t pin_;
  bool on_ = false;
  uint8_t frame_[1 + DMX_CHANNELS];  // start code 0, then the slots
#if defined(ARDUINO_ARCH_RP2040)
  void* pio_ = nullptr;
  int sm_ = -1, dma_ = -1;
  unsigned offset_ = 0;
#endif
};

// LittleFS files for ABStorage.
class LittleFsIo : public FileIo {
 public:
  // Mounts the file system; formats it only when the partition was never written (mountOrFormatBlank).
  MountResult begin();
  bool read(const char* path, std::string& out) override;
  bool write(const char* path, const std::string& data) override;
  bool remove(const char* path) override;
  bool exists(const char* path) override;
  bool scan(const char* path, ByteSink& sink) override;
  bool writeRecord(const char* path, const uint8_t* head, size_t head_n, const std::string& data) override;
  bool usable() override { return mounted_; }
  bool reformat() override;

 private:
  bool mounted_ = false;
};

// A recursive lock around the controller (the tick runs on its own task / core): the platform's mutex, with the
// rule for the work a request does without it (window_lock.h).
class RawLock {
 public:
  using Id = uintptr_t;
  RawLock();
  void take();
  void give();
  // Give up every level this task holds (0 when it holds none).
  int releaseAll();
  bool mine() const;
  Id self() const;
  void pause();  // about 1 ms for the other tasks

 private:
  void* h_ = nullptr;
};
using Lock = WindowLock<RawLock>;

class Guard {
 public:
  explicit Guard(Lock& l) : l_(l) { l_.take(); }
  ~Guard() { l_.give(); }

 private:
  Lock& l_;
};

// MAC address (ESP32) or flash unique id (RP2040), lower-case hex.
std::string deviceId();
const char* mcuName();
void restartNow();

}  // namespace pt
#endif
