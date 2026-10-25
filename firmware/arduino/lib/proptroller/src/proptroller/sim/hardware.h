// Virtual hardware for the simulator and the host tests: pins, the DFPlayer serial line with a module model
// on the other end, the pixel strip, and flash that survives a power cycle.
#pragma once
#include "../board.h"
#include "../pins.h"
#include "../script.h"
#include "../storage.h"
#include "../ws2812.h"
#include "dfmodel.h"

namespace pt {

struct SimWorld {
  Millis now = 0;
  Level out[4] = {0, 0, 0, 0};  // OUT1-4 duty, permille
  bool digital[4] = {false, false, false, false};
  bool led = false;
  bool input_high[INPUT_COUNT] = {true, true, true, true, true};  // the pin levels (idle: pulled up, or down)
  uint32_t writes[CHANNEL_COUNT] = {0};  // pin writes per output channel ...
  uint32_t reads[INPUT_COUNT] = {0};     // ... and reads per input
  uint16_t analog = 0;
  DfModel df;
  std::vector<uint8_t> uart_rx;  // bytes from the module to the MCU
  std::vector<Rgb> pixels;
  uint32_t pixel_frames = 0;
  uint32_t pixel_errors = 0;
  // the wire's time as a board's background send has it (ws2812FrameUs): off by default (a frame is there at
  // once); on, the port is busy() until the frame and its latch are over, and a write while busy is counted
  bool pixel_wire = false;
  uint64_t pixel_busy_until_us = 0;
  uint32_t pixel_writes_while_busy = 0;
  // the DMX port: on while started, every frame sent (time, the 512 slots)
  bool dmx_on = false;
  std::vector<std::pair<Millis, std::vector<uint8_t>>> dmx_frames;
  void levels(Level out8[CHANNEL_COUNT]) const {
    for (int i = 0; i < 4; i++) out8[i] = out[i];
    for (int i = 0; i < 4; i++) out8[4 + i] = digital[i] ? FULL : 0;
  }
};

class SimDmx : public DmxOut {
 public:
  explicit SimDmx(SimWorld& w) : w_(w) {}
  void start() override { w_.dmx_on = true; }
  void stop() override { w_.dmx_on = false; }
  void send(const uint8_t* slots, size_t n) override { w_.dmx_frames.push_back({w_.now, std::vector<uint8_t>(slots, slots + n)}); }

 private:
  SimWorld& w_;
};

class SimPins : public BoardPins {
 public:
  explicit SimPins(SimWorld& w) : w_(w) {}
  // a board profile (pins.h): absent terminals and input polarity; none = every terminal, active low
  void setMap(const PinMap& m) { map_ = m; has_map_ = true; }
  const PinMap& map() const { return map_; }
  bool activeHigh(int k) const { return has_map_ && inputActiveHigh(map_, k); }
  bool hasOutput(int ch) override { return !has_map_ || channelPin(map_, ch) != NO_PIN; }
  bool hasInput(int k) override { return !has_map_ || inputPin(map_, k) != NO_PIN; }
  bool hasSound() override { return !has_map_ || (map_.df_tx != NO_PIN && map_.df_rx != NO_PIN); }
  bool hasPixels() override { return !has_map_ || map_.pixel != NO_PIN; }
  bool setOut(int i, Level l) override { w_.out[i] = l; w_.writes[i]++; return true; }
  bool setDigital(int i, bool on) override { w_.digital[i] = on; w_.writes[4 + i]++; return true; }
  bool readLow(int i, bool& low) override { w_.reads[i]++; low = w_.input_high[i] == activeHigh(i); return true; }
  bool hasStatusLed() override { return true; }
  bool setStatusLed(bool on) override { w_.led = on; return true; }

 private:
  SimWorld& w_;
  PinMap map_{};
  bool has_map_ = false;
};

class SimUart : public ByteStream {
 public:
  explicit SimUart(SimWorld& w) : w_(w) {}
  int available() override { return (int)(w_.uart_rx.size() - pos_); }
  int read() override {
    if (pos_ >= w_.uart_rx.size()) { w_.uart_rx.clear(); pos_ = 0; return -1; }
    int b = w_.uart_rx[pos_++];
    if (pos_ == w_.uart_rx.size()) { w_.uart_rx.clear(); pos_ = 0; }
    return b;
  }
  size_t write(const uint8_t* b, size_t n) override { w_.df.receive(b, n, w_.now); return n; }

 private:
  SimWorld& w_;
  size_t pos_ = 0;
};

class SimPixels : public PixelOut {
 public:
  explicit SimPixels(SimWorld& w) : w_(w) {}
  bool busy() override { return w_.pixel_wire && w_.now * 1000 < w_.pixel_busy_until_us; }
  bool write(const Rgb* px, size_t n) override {
    if (busy()) { w_.pixel_writes_while_busy++; return false; }  // a board would wait here (or refuse)
    if (w_.pixel_wire) w_.pixel_busy_until_us = w_.now * 1000 + ws2812FrameUs(n);
    // through the real encoder and back, as the strip would see it
    std::vector<Pulse> p;
    PROPTROLLER_PIXEL.pulses(px, n, p);
    std::vector<Rgb> out;
    if (!PROPTROLLER_PIXEL.decode(p, out) || out.size() != n) { w_.pixel_errors++; return false; }
    w_.pixels = out;
    w_.pixel_frames++;
    return true;
  }

 private:
  SimWorld& w_;
};

}  // namespace pt
