// The start gate of an effect that a trigger input and remote requests may start (firmware/STATE-MACHINE.md §4-§6):
// the run window (SM-L), the trigger input's release and hold-to-confirm (SM-T) and the one queued remote request
// (SM-Q). The pulse-and-recover effect (effect.h) and show-script effects that say "or remote" (script.h) both
// start through it, so both follow the same rules.
#pragma once
#include "types.h"
#include "window.h"

namespace pt {

class RunGate {
 public:
  bool queued_remote = false;  // SM-Q1
  bool released = false;       // a release (outside a Full window) since the last run (SM-T)
  bool confirming = false;     // SM-T1a timer running ...
  Millis confirm_since = 0;    // ... since
  bool raw_closed = false;     // the trigger input at the last sample
  Millis open_since = 0;       // raw open without a break since

  bool windowFull(Millis now) const { return window_.isOpen(now) && runsLeft(now) == 0; }
  uint32_t runsLeft(Millis now) const { return window_.left(now, max_runs_); }
  uint32_t cooldownRemaining(Millis now) const { return window_.isOpen(now) ? window_.remainingMs(now) : 0; }
  // The run limit of the window (at least 1); a changed limit applies from the next run (SM-L7).
  void setMaxRuns(uint32_t n) { max_runs_ = n < 1 ? 1 : n; }
  // One raw sample of the trigger input, first thing at every sample (SM-T1).
  void sampleRaw(Millis now, bool closed) {
    if (!closed && raw_closed) open_since = now;
    raw_closed = closed;
  }
  // After the effect's own state is up to date: the queue's cancellations (SM-Q5: the enable off, a Full window),
  // the release (SM-T7: registered only outside a Full window) and the hold to confirm (SM-T1a: closed on every
  // sample for confirm_ms, counted only while `ready` and after a release). True when a local run may start now.
  bool update(Millis now, bool closed, bool remote_enabled, bool ready, uint32_t release_ms, uint32_t confirm_ms) {
    if (!remote_enabled) queued_remote = false;
    bool full = windowFull(now);
    if (full) queued_remote = false;
    if (!full && !closed && now - open_since >= release_ms && seen_open_) released = true;
    if (!closed) seen_open_ = true;
    if (closed && released && ready) {
      if (!confirming) { confirming = true; confirm_since = now; }
    } else {
      confirming = false;
    }
    return confirming && now - confirm_since >= confirm_ms;
  }
  // A run starts (from any source): the queue is empty, the next local run needs a new release (SM-T6), and the
  // window opens at the first run and keeps its end; later runs only count (SM-L1).
  void started(Millis now, uint32_t window_ms) {
    queued_remote = false;
    released = false;
    confirming = false;
    window_.countRun(now);
    window_.open(now, window_ms);
  }
  // A remote request that cannot start now: queued (true), or already queued (false) (SM-Q1).
  bool queue() {
    if (queued_remote) return false;
    queued_remote = true;
    return true;
  }
  bool cancelRemote() { bool q = queued_remote; queued_remote = false; return q; }
  // Stop, restart: the queue and the release go (the next local run needs a release first).
  void clearRequests() { queued_remote = released = confirming = false; }
  void reset() { clearRequests(); window_.reset(); seen_open_ = false; raw_closed = false; open_since = 0; }

 protected:
  RunWindow window_;
  uint32_t max_runs_ = 1;
  bool seen_open_ = false;  // the trigger read open at least once since power-on (SM-B3)
};

}  // namespace pt
