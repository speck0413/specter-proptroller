// Soft start and the max-on watchdog, applied per output after channel arbitration.
//
// Soft start is an envelope: when an output leaves 0 it may rise no faster than 0 -> 100 % over
// soft_start_ms, so "on" becomes a ramp for a motor or a fan. Off is always immediate.
#pragma once
#include "types.h"

namespace pt {

static const uint32_t MAX_SOFT_START_MS = 2000;

class Shaper {
 public:
  Shaper(uint32_t soft_start = 0, uint32_t max_on = 0)
      : soft_start_ms(soft_start < MAX_SOFT_START_MS ? soft_start : MAX_SOFT_START_MS), max_on_ms(max_on) {}
  uint32_t soft_start_ms;
  uint32_t max_on_ms;  // 0 = unlimited; exceeding it forces the output off until it is commanded off
  bool tripped() const { return tripped_; }
  Level level() const { return last_; }
  Level update(Millis now, Level target) {
    if (target > FULL) target = FULL;
    if (target == 0) { has_since_ = false; tripped_ = false; last_ = 0; return 0; }
    if (!has_since_) { has_since_ = true; since_ = now; }
    Millis on_for = now > since_ ? now - since_ : 0;
    if (max_on_ms > 0 && on_for >= max_on_ms) tripped_ = true;
    if (tripped_) { last_ = 0; return 0; }
    Level out = target;
    if (soft_start_ms > 0 && on_for < soft_start_ms) {
      Level env = (Level)((uint64_t)FULL * on_for / soft_start_ms);
      if (env < 1) env = 1;
      out = target < env ? target : env;
    }
    last_ = out;
    return out;
  }
  void reset() { has_since_ = false; tripped_ = false; last_ = 0; }

 private:
  bool has_since_ = false;
  Millis since_ = 0;
  bool tripped_ = false;
  Level last_ = 0;
};

}  // namespace pt
