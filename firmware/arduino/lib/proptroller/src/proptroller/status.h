// Blink patterns for the status LED (or a status lamp on an output). A pattern is a pure function of time.
#pragma once
#include "types.h"

namespace pt {

struct Pattern {
  enum class Kind : uint8_t { Off, Steady, FastBlink, SlowBlink, DoubleBlink, TripleBlink, Flicker, Count, Heartbeat };
  Kind kind = Kind::Off;
  uint8_t count = 0;  // for Count: n 150 ms flashes, then a 1 s pause
  Pattern() {}
  Pattern(Kind k, uint8_t n = 0) : kind(k), count(n) {}
  bool operator==(const Pattern& o) const { return kind == o.kind && (kind != Kind::Count || count == o.count); }
  bool operator!=(const Pattern& o) const { return !(*this == o); }
  // LED state at time t (ms).
  bool level(uint64_t t) const;
  // Protocol name: "off", "steady", "fast_blink", ... (Count is written as {"count":n} by the protocol).
  const char* name() const;
  // Lower-case name ("fastblink", "count(3)") used for a few status strings.
  std::string debugLower() const;
};

}  // namespace pt
