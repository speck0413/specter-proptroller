// Teach: measure a duration by two taps.
#pragma once
#include "show.h"

namespace pt {

static const uint32_t QUANTUM_MS = 10;
// Round to the nearest 10 ms.
inline uint32_t quantize(uint64_t ms) {
  uint64_t q = ((ms + QUANTUM_MS / 2) / QUANTUM_MS) * QUANTUM_MS;
  return q > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)q;
}

// Measure a duration between two taps (quantized, clamped to [min_ms, max_ms]).
class Teach {
 public:
  Teach(uint32_t min = 100, uint32_t max = 600000) : min_ms(min), max_ms(max) {}
  uint32_t min_ms, max_ms;
  void start(Millis now) { active_ = true; started_ = now; }
  bool isActive() const { return active_; }
  bool elapsed(Millis now, uint32_t& out) const {
    if (!active_) return false;
    out = (uint32_t)(now > started_ ? now - started_ : 0);
    return true;
  }
  bool timedOut(Millis now) const { uint32_t e = 0; return elapsed(now, e) && e >= max_ms; }
  bool stop(Millis now, uint32_t& out) {
    if (!active_) return false;
    active_ = false;
    uint32_t v = quantize(now > started_ ? now - started_ : 0);
    out = v < min_ms ? min_ms : (v > max_ms ? max_ms : v);
    return true;
  }
  void abort() { active_ = false; }

 private:
  bool active_ = false;
  Millis started_ = 0;
};

}  // namespace pt
