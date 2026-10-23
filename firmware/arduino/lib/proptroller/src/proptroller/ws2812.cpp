#include "ws2812.h"

namespace pt {

Rgb Rgb::scale(uint8_t num) const {
  auto s = [num](uint8_t c) { return (uint8_t)(((uint16_t)c * num + 127) / 255); };
  return Rgb(s(r), s(g), s(b));
}

Pulse Encoder::bitPulse(bool one) const {
  uint32_t h = one ? timing.t1h : timing.t0h, l = one ? timing.t1l : timing.t0l;
  return Pulse{phys(true), h, phys(false), l};
}

Pulse Encoder::latch() const {
  uint32_t half = timing.reset / 2;
  return Pulse{phys(false), half, phys(false), timing.reset - half};
}

void Encoder::pulses(const Rgb* px, size_t n, std::vector<Pulse>& out) const {
  out.clear();
  out.reserve(pulseCount(n));
  for (size_t k = 0; k < n; k++) {
    uint32_t word = ((uint32_t)px[k].g << 16) | ((uint32_t)px[k].r << 8) | px[k].b;
    for (int i = 23; i >= 0; i--) out.push_back(bitPulse((word >> i) & 1));
  }
  out.push_back(latch());
}

bool Encoder::decode(const std::vector<Pulse>& ps, std::vector<Rgb>& out) const {
  out.clear();
  uint32_t word = 0;
  int bits = 0;
  uint32_t mid = (timing.t0h + timing.t1h) / 2;
  for (const Pulse& p : ps) {
    if (p.level0 == phys(false)) return p.ns0 + p.ns1 >= 50000 && bits == 0;  // latch
    if (p.level1 != phys(false)) return false;
    word = (word << 1) | (p.ns0 > mid ? 1 : 0);
    if (++bits == 24) {
      out.push_back(Rgb((word >> 8) & 0xFF, (word >> 16) & 0xFF, word & 0xFF));
      bits = 0;
      word = 0;
    }
  }
  return bits == 0;
}

}  // namespace pt
