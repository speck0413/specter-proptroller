// "Up to N runs per cooldown window": a small counter that any effect can use.
//
// A prop that has just run usually needs a rest (a tank refilling, a crowd moving on). The simplest rule
// is "after it runs, wait X seconds". RunWindow allows a little more: "it may run up to 3 times, then it
// rests until the window is over".
//
// * The window OPENS at a moment you choose (open()): when the first run starts, or when it ends. It stays
//   open for window_ms and its end never moves.
// * Every accepted run is counted (countRun(now)); a window that has ended is forgotten first.
// * While the window is open and the count has reached max_runs, allows() says no.
// * When the window ends, the count goes back to zero.
// * max_runs and window_ms are passed in at every call, so a changed setting applies to the very next run.
//   With max_runs = 1 it is the classic "wait X after each run".
#pragma once
#include "types.h"

namespace pt {

class RunWindow {
 public:
  void reset() { used_ = 0; open_ = false; ends_at_ = 0; }
  bool allows(Millis now, uint32_t max_runs) {
    expire(now);
    return used_ < (max_runs < 1 ? 1 : max_runs);
  }
  // Count an accepted run at `now` (a window that has ended is forgotten first, so this run opens a fresh one).
  void countRun(Millis now) {
    expire(now);
    if (used_ != 0xFFFFFFFFu) used_++;
  }
  // Open the window now for window_ms if none is open (an open window keeps its end). A zero-length window
  // closes at once: nothing is ever held back.
  void open(Millis now, uint32_t window_ms) {
    expire(now);
    if (!open_ && used_ > 0) {
      if (window_ms == 0) reset();
      else { open_ = true; ends_at_ = now + window_ms; }
    }
  }
  uint32_t left(Millis now, uint32_t max_runs) const {
    uint32_t m = max_runs < 1 ? 1 : max_runs;
    if (open_ && now >= ends_at_) return m;
    return used_ >= m ? 0 : m - used_;
  }
  uint32_t remainingMs(Millis now) const { return open_ && ends_at_ > now ? (uint32_t)(ends_at_ - now) : 0; }
  bool isOpen(Millis now) const { return open_ && now < ends_at_; }
  uint32_t used() const { return used_; }
  // When the open window ends (false when none is open).
  bool endsAt(Millis& out) const { out = ends_at_; return open_; }

 private:
  void expire(Millis now) { if (open_ && now >= ends_at_) reset(); }
  uint32_t used_ = 0;
  bool open_ = false;
  Millis ends_at_ = 0;
};

}  // namespace pt
