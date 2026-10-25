// WS2812 ("NeoPixel") encoding for the PIXEL port.
//
// The board's 5 V level shifter is a single 2N7002 stage, which INVERTS the data: the MCU pin must idle
// high and every bit is sent inverted. Encoder produces physical pin pulses with that inversion applied
// (idle level and latch included), so a driver only copies Pulses to the pin.
// The PIXEL terminal's rating is in hardware/README.md; the firmware does not estimate strip current (SM-S3),
// the pixel_brightness setting scales every channel.
#pragma once
#include "types.h"
#include <vector>

namespace pt {

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
  Rgb() {}
  Rgb(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const Rgb& o) const { return !(*this == o); }
  // Scale by num / 255.
  Rgb scale(uint8_t num) const;
};

// One RMT-style item: level0 for ns0, then level1 for ns1 (physical pin levels).
struct Pulse {
  bool level0;
  uint32_t ns0;
  bool level1;
  uint32_t ns1;
};

struct Timing {
  uint32_t t0h, t0l, t1h, t1l, reset;
};
// WS2812B timing with a 300 us latch (newer parts need >= 280 us).
static const Timing WS2812B = {400, 850, 800, 450, 300000};

struct Encoder {
  bool inverted;
  Timing timing;
  bool idleLevel() const { return inverted; }
  Pulse bitPulse(bool one) const;
  Pulse latch() const;
  // 24 pulses per pixel (G, R, B, MSB first) and one latch pulse.
  void pulses(const Rgb* px, size_t n, std::vector<Pulse>& out) const;
  static size_t pulseCount(size_t n) { return n * 24 + 1; }
  // Decode pulses back into pixels (tests, simulator). Returns false if a pulse is not a valid bit.
  bool decode(const std::vector<Pulse>& p, std::vector<Rgb>& out) const;

 private:
  bool phys(bool logical) const { return logical != inverted; }
};

// The Proptroller PIXEL port: inverted, WS2812B timing.
static const Encoder PROPTROLLER_PIXEL = {true, WS2812B};


// The time a frame of n pixels takes on the wire, then its latch: 24 bits of 1.25 us a pixel (30 us), 300 us.
static const uint32_t WS2812_PIXEL_US = 30;
static const uint32_t WS2812_LATCH_US = 300;
// What a driver adds for what it cannot see exactly: the interrupt latency of the RMT refills, the 8 words a
// PIO FIFO still holds when its DMA reports done (8 pixels, 240 us).
static const uint32_t WS2812_MARGIN_US = 300;
inline uint32_t ws2812FrameUs(size_t n) { return (uint32_t)n * WS2812_PIXEL_US + WS2812_LATCH_US + WS2812_MARGIN_US; }

// Paces frames that are sent in the background (ESP32 RMT, RP2040 PIO + DMA): a new frame starts only after the
// last one has left the pin and its latch has passed, so the step never waits for the wire. Times in us, from a
// free-running 32-bit counter (wraps after 71 minutes; the subtraction is wrap-safe).
struct FramePacer {
  bool sending = false;
  uint32_t started_us = 0, length_us = 0;
  bool ready(uint32_t now_us) const { return !sending || (uint32_t)(now_us - started_us) >= length_us; }
  void start(uint32_t now_us, size_t n) {
    sending = true;
    started_us = now_us;
    length_us = ws2812FrameUs(n);
  }
};

// Anything that can show a frame on the PIXEL port.
class PixelOut {
 public:
  virtual ~PixelOut() {}
  // Starts sending the frame and returns at once (the driver keeps its own copy). False: the port failed.
  virtual bool write(const Rgb* px, size_t n) = 0;
  // The last frame is still on the wire (or in its latch): a write now would be refused, so the caller keeps the
  // frame for a later tick.
  virtual bool busy() { return false; }
};

}  // namespace pt
