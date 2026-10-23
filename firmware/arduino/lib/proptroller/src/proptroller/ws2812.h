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


// Anything that can show a frame on the PIXEL port.
class PixelOut {
 public:
  virtual ~PixelOut() {}
  virtual bool write(const Rgb* px, size_t n) = 0;
};

}  // namespace pt
