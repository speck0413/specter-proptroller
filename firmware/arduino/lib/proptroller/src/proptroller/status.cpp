#include "status.h"
#include <stdio.h>

namespace pt {

static bool flashes(uint64_t phase, uint64_t n, uint64_t on, uint64_t off) {
  uint64_t slot = on + off;
  return phase < n * slot && phase % slot < on;
}

bool Pattern::level(uint64_t t) const {
  switch (kind) {
    case Kind::Off: return false;
    case Kind::Steady: return true;
    case Kind::FastBlink: return t % 250 < 125;
    case Kind::SlowBlink: return t % 1000 < 500;
    case Kind::Flicker: return t % 125 < 63;
    case Kind::DoubleBlink: return flashes(t % 1000, 2, 100, 100);
    case Kind::TripleBlink: return flashes(t % 1000, 3, 100, 100);
    case Kind::Count: {
      uint64_t n = count < 1 ? 1 : count;
      uint64_t period = n * 300 + 1000;
      return flashes(t % period, n, 150, 150);
    }
    case Kind::Heartbeat: return t % 2000 < 50;
  }
  return false;
}

const char* Pattern::name() const {
  switch (kind) {
    case Kind::Off: return "off";
    case Kind::Steady: return "steady";
    case Kind::FastBlink: return "fast_blink";
    case Kind::SlowBlink: return "slow_blink";
    case Kind::DoubleBlink: return "double_blink";
    case Kind::TripleBlink: return "triple_blink";
    case Kind::Flicker: return "flicker";
    case Kind::Count: return "count";
    case Kind::Heartbeat: return "heartbeat";
  }
  return "off";
}

std::string Pattern::debugLower() const {
  static const char* const n[] = {"off", "steady", "fastblink", "slowblink", "doubleblink", "tripleblink", "flicker", "count", "heartbeat"};
  if (kind == Kind::Count) {
    char b[16];
    (void)snprintf(b, sizeof b, "count(%u)", (unsigned)count);
    return b;
  }
  PT_ONE_PER(n, Kind::Heartbeat);
  return n[idx(kind)];
}

}  // namespace pt
