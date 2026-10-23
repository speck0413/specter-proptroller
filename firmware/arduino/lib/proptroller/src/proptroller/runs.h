// Run counters: how many runs started since power-on (a night's show: the board is powered up for it and off
// afterwards) and in the board's whole life. A run is one start of a show, of a show script's effect or of the
// built-in pulse-and-recover effect, whatever started it (a trigger input, the web page, MQTT, the serial port, a
// cue, a timer). A restart of a running show, a loop of a show and a recover cycle are not runs.
//
// The lifetime count lives in its own small record (two files, runs-a / runs-b, power-safe like the settings),
// so a backup, a restore and a firmware update leave it alone. It is written from the loop, never from the step,
// and only when nothing runs: a flash write stalls the step wherever it runs (the ESP32 turns its flash cache off,
// the RP2040 parks the other core), estimated 1-3 ms for this record and about 45 ms typical (400 ms datasheet
// worst case) on the occasional write that makes LittleFS erase a 4 KB sector. A trigger pressed during it is
// seen that much later; no show, effect cycle or script effect is running then. See RunCounter::saveDue for the
// throttle and the worst case lost on a sudden power cut.
#pragma once
#include <stdint.h>
#include <string>
#include "types.h"

namespace pt {

// The run record's capacity: {"lifetime":4294967295} with room to spare.
static const size_t RUNS_STORE_CAPACITY = 64;
// The lifetime count is written once RUNS_SAVE_EVERY runs are waiting, or RUNS_SAVE_PERIOD_MS after the first
// run not yet written, whichever comes first, and only while nothing runs (quiet) ...
static const uint32_t RUNS_SAVE_EVERY = 10;
static const Millis RUNS_SAVE_PERIOD_MS = 60000;
// ... and at most once per RUNS_SAVE_MIN_GAP_MS (flash wear: at most 6 small writes a minute, in practice one a
// minute or less: a run takes seconds) ...
static const Millis RUNS_SAVE_MIN_GAP_MS = 10000;
// ... never while something runs (owner rule: nothing stalls the step during a show), so a board that never goes
// quiet (a looping show) writes at its next quiet moment. A clean restart (Restart, reboot, a factory reset, a
// firmware update) writes it at once.
// Worst case lost on a sudden power cut: the runs since the last write. With a quiet moment between runs that is
// at most RUNS_SAVE_EVERY (10) runs or RUNS_SAVE_PERIOD_MS (60 s) of runs, whichever is fewer (more than 10 only
// when runs come faster than one per RUNS_SAVE_MIN_GAP_MS / 10 = 1 s); while the board is busy without a break,
// every run since it was last quiet. runs_since_power_on is RAM only (it starts at 0 at every power-on).
//
// A failed write is tried again this long after.
static const Millis RUNS_SAVE_RETRY_MS = 10000;

class RunCounter {
 public:
  uint32_t sincePowerOn() const { return since_power_on_; }
  uint64_t lifetime() const { return lifetime_; }
  bool hasRun() const { return since_power_on_ > 0; }
  Millis lastRunAt() const { return last_at_; }  // uptime of the last run (valid when hasRun())
  uint32_t unsaved() const { return unsaved_; }

  // A run started at `now`.
  void count(Millis now) {
    since_power_on_++;
    lifetime_++;
    last_at_ = now;
    if (unsaved_ == 0) unsaved_since_ = now;
    unsaved_++;
  }
  // The stored lifetime count, at power-on.
  void loaded(uint64_t lifetime) { lifetime_ = lifetime; }
  // Set the lifetime count to 0 (op reset_runs); written at the next save.
  void resetLifetime(Millis now) {
    lifetime_ = 0;
    if (unsaved_ == 0) unsaved_since_ = now;
    unsaved_++;
    force_ = true;
  }
  // Is a write due now? `quiet`: nothing runs (no show, effect cycle or script effect).
  bool saveDue(Millis now, bool quiet) const {
    if (unsaved_ == 0 || now < not_before_) return false;
    if (force_) return true;  // a clean restart or a reset: whatever runs
    Millis waiting = now - unsaved_since_;
    if (now < last_save_ + RUNS_SAVE_MIN_GAP_MS && saved_once_) return false;
    return quiet && (unsaved_ >= RUNS_SAVE_EVERY || waiting >= RUNS_SAVE_PERIOD_MS);
  }
  // A clean restart is coming: write whatever is waiting.
  void flushSoon() { if (unsaved_) force_ = true; }
  // The write went through (true) or failed.
  void saved(Millis now, bool ok) {
    if (!ok) { not_before_ = now + RUNS_SAVE_RETRY_MS; return; }
    unsaved_ = 0;
    force_ = false;
    last_save_ = now;
    saved_once_ = true;
    not_before_ = 0;
  }
  std::string record() const { return "{\"lifetime\":" + std::to_string((unsigned long long)lifetime_) + "}"; }
  // false when the record is not one of ours
  static bool parse(const std::string& rec, uint64_t& out);

 private:
  uint32_t since_power_on_ = 0;
  uint64_t lifetime_ = 0;
  Millis last_at_ = 0;
  uint32_t unsaved_ = 0;
  Millis unsaved_since_ = 0;
  Millis last_save_ = 0;
  Millis not_before_ = 0;
  bool saved_once_ = false;
  bool force_ = false;
};

}  // namespace pt
