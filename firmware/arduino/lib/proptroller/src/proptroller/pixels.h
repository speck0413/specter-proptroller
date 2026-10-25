// Pixel effects for the PIXEL port, rendered as a pure function of the time since the effect started.
#pragma once
#include "ws2812.h"
#include <string>

namespace pt {

// "#rrggbb" (or "rrggbb"); false if malformed.
[[nodiscard]] bool parseColor(const char* s, Rgb& out);
std::string colorText(const Rgb& c);  // "#rrggbb", lower case

struct Effect {
  // Comet, Sparkle, Breathe and Fire come from show scripts only (pixelEffect); new kinds go at the end.
  // Pattern: a stored LED pattern (pattern_vm.h), played by the controller's pixel side, never by render().
  enum class Kind : uint8_t { Off, Fill, Chase, Rainbow, Flash, Fade, Flicker, Comet, Sparkle, Breathe, Fire, Pattern };
  Kind kind = Kind::Off;
  Rgb color;
  uint32_t step_ms = 0;    // Chase, Comet: ms a pixel
  uint16_t width = 3;      // Chase: lit pixels; Comet: the tail
  uint32_t period_ms = 0;  // Rainbow, Flash, Breathe
  uint32_t ms = 0;         // Fade
  uint16_t speed = 100;    // % of the effect's own time (pixel_speed): 200 runs it twice as fast
  uint8_t pattern = 0;     // Pattern: the stored pattern's place in the controller's list
  uint32_t pattern_gen = 0;  // Pattern: that pattern's number (a new one each time it is sent)
  bool operator==(const Effect& o) const;
  bool operator!=(const Effect& o) const { return !(*this == o); }
  bool animated() const { return kind != Kind::Off && kind != Kind::Fill; }
  // Short name for the state ("off", "fill #ff0000", "rainbow", ...).
  std::string describe() const;
};

Rgb wheel(uint8_t pos);
void render(const Effect& e, uint64_t t, Rgb* out, size_t n);

// A show script's pixel effect (`pixels = chase red`) with its built-in times at speed 100 %.
static const uint32_t PIXEL_CHASE_STEP_MS = 60, PIXEL_CHASE_WIDTH = 3;
static const uint32_t PIXEL_RAINBOW_MS = 5000;
static const uint32_t PIXEL_FLASH_MS = 600;
static const uint32_t PIXEL_FADE_MS = 2000;   // fade: in from dark, then stays
static const uint32_t PIXEL_COMET_STEP_MS = 30, PIXEL_COMET_TAIL = 10;
static const uint32_t PIXEL_BREATHE_MS = 4000;
static const uint32_t PIXEL_SPARKLE_MS = 700;  // each pixel's turn; a sparkle fades in PIXEL_SPARKLE_FADE_MS
static const uint32_t PIXEL_SPARKLE_FADE_MS = 300;
static const uint32_t PIXEL_FIRE_MS = 90;      // a new flame level every 90 ms, blended in between
Effect pixelEffect(Effect::Kind kind, Rgb color, uint16_t speed);

}  // namespace pt
