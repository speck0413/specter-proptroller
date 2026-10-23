// Pixel effects for the PIXEL port, rendered as a pure function of the time since the effect started.
#pragma once
#include "ws2812.h"
#include <string>

namespace pt {

// "#rrggbb" (or "rrggbb"); false if malformed.
[[nodiscard]] bool parseColor(const char* s, Rgb& out);
std::string colorText(const Rgb& c);  // "#rrggbb", lower case

struct Effect {
  enum class Kind : uint8_t { Off, Fill, Chase, Rainbow, Flash, Fade, Flicker };
  Kind kind = Kind::Off;
  Rgb color;
  uint32_t step_ms = 0;    // Chase
  uint16_t width = 3;      // Chase
  uint32_t period_ms = 0;  // Rainbow, Flash
  uint32_t ms = 0;         // Fade
  bool operator==(const Effect& o) const;
  bool operator!=(const Effect& o) const { return !(*this == o); }
  bool animated() const { return kind != Kind::Off && kind != Kind::Fill; }
  // Short name for the state ("off", "fill #ff0000", "rainbow", ...).
  std::string describe() const;
};

Rgb wheel(uint8_t pos);
void render(const Effect& e, uint64_t t, Rgb* out, size_t n);

}  // namespace pt
