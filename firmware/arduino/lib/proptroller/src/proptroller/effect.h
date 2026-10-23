// The pulse-and-recover effect (firmware/STATE-MACHINE.md) as its own state machine beside the
// show engine: a run pulses OUT1 for out1_on_ms, then OUT2 runs the recovery for out2_on_ms (no knob, SM-F2);
// optionally OUT3 pulses first and the recovery runs in the background. Up to runs_per_cooldown runs start in a
// cooldown_ms window that opens at the first run. A local run needs a TRIG1 release since the last run, then
// TRIG1 closed on every raw sample for trig_debounce_ms while Ready (SM-T1a); a remote request queues one run.
// A recover runs the recovery without the OUT1 pulse. Milliseconds since power-on; the controller samples
// TRIG1 every 1 ms and passes each sample in.
#pragma once
#include <vector>
#include "status.h"
#include "types.h"
#include "window.h"

namespace pt {

// Defaults and limits of the effect's settings (the reference table), each one named.
static const uint32_t EFFECT_OUT1_ON_DEFAULT_MS = 1000, EFFECT_OUT1_ON_MIN_MS = 10, EFFECT_OUT1_ON_MAX_MS = 60000;
static const uint32_t EFFECT_OUT2_ON_DEFAULT_MS = 5500, EFFECT_OUT2_ON_MIN_MS = 0, EFFECT_OUT2_ON_MAX_MS = 60000;
static const uint32_t EFFECT_OUT3_ON_DEFAULT_MS = 1000, EFFECT_OUT3_ON_MIN_MS = 0, EFFECT_OUT3_ON_MAX_MS = 60000;
static const uint32_t EFFECT_COOLDOWN_DEFAULT_MS = 180000, EFFECT_COOLDOWN_MIN_MS = 0, EFFECT_COOLDOWN_MAX_MS = 3600000;
static const uint8_t EFFECT_RUNS_DEFAULT = 3, EFFECT_RUNS_MIN = 1, EFFECT_RUNS_MAX = 20;
static const uint32_t EFFECT_RECOVER_TIME_DEFAULT_MS = 5500, EFFECT_RECOVER_TIME_MIN_MS = 100, EFFECT_RECOVER_TIME_MAX_MS = 600000;
static const uint32_t EFFECT_TRIGGER_OUT_PULSE_DEFAULT_MS = 200, EFFECT_TRIGGER_OUT_PULSE_MIN_MS = 10, EFFECT_TRIGGER_OUT_PULSE_MAX_MS = 5000;
static const Millis EFFECT_BOOT_LOCKOUT_MS = 2000;       // nothing runs or recovers after power-on (SM-B2)
static const Millis EFFECT_SAFETY_HOLD_MS = 2000;        // TRIG1 held this long releases the safety (SM-B4)
static const Millis EFFECT_GAP_MS = 10;                  // OUT1 off before the recovery starts
static const Millis EFFECT_RECOVER_HOLD_CAP_MS = 30000;  // recover_mode hold: OUT2 on while TRIG2 held, at most this
static const Millis EFFECT_ACTION_LEASE_MS = 1500;       // a held web Recover button lasts this long past its last repeat

enum class RecoverMode : uint8_t { Cycle, Timed, Hold };
const char* recoverModeName(RecoverMode m);  // "cycle", "timed", "hold"
bool parseRecoverMode(const std::string& s, RecoverMode& out);

struct EffectConfig {
  uint32_t out1_on_ms = EFFECT_OUT1_ON_DEFAULT_MS;
  uint32_t out2_on_ms = EFFECT_OUT2_ON_DEFAULT_MS;
  bool out3_enabled = false;
  uint32_t out3_on_ms = EFFECT_OUT3_ON_DEFAULT_MS;
  uint32_t cooldown_ms = EFFECT_COOLDOWN_DEFAULT_MS;  // 0 = no cooldown
  uint8_t runs_per_cooldown = EFFECT_RUNS_DEFAULT;
  bool auto_arm = true;
  RecoverMode recover_mode = RecoverMode::Cycle;
  uint32_t recover_time_ms = EFFECT_RECOVER_TIME_DEFAULT_MS;
  uint32_t confirm_ms = TRIGGER_DEBOUNCE_MS;  // trig_debounce_ms
  uint32_t release_ms = TRIGGER_DEBOUNCE_MS;  // trig_release_ms
  bool remote_trigger_enable = false;
};

enum class EffectPhase : uint8_t { Idle, Pulse, RecoverInterval, Gap, WaitRecovery, After, Recovery };
// Why a run (or a recover) cannot start now.
enum class EffectRefused : uint8_t { None, Safety, Boot, CooldownFull, Busy, Recovering, RemoteDisabled, AlreadyQueued };
// Every state of the state panel, in display order.
extern const char* const EFFECT_STATES[7];

struct EffectEvent {
  enum class Kind : uint8_t { None, Started, RecoverStarted, RecoveryStarted, SafetyOff };
  Kind kind = Kind::None;
  uint32_t value = 0;  // run count, recovery ms
  bool remote = false; // Started: from the remote queue
};

class EffectMachine {
 public:
  EffectMachine(const EffectConfig& c, Millis now);
  bool safety_on;
  EffectPhase phase = EffectPhase::Idle;
  Millis phase_until = 0;
  bool recovery_bg = false;  // the background recovery after OUT3 (SM-F4)
  Millis recovery_bg_until = 0;
  enum class RecoverRun : uint8_t { NoRecover, RecoverTimed, RecoverHold };
  RecoverRun recover = RecoverRun::NoRecover;
  Millis recover_until = 0;
  uint32_t run_count = 0;  // since boot (recover cycles count too, SM-PR3)
  uint32_t last_recovery_ms = 0;
  bool queued_remote = false;   // SM-Q1
  bool queued_recover = false;  // SM-PR2
  bool released = false;        // a TRIG1 release (outside a Full window) since the last run (SM-T)
  bool confirming = false;      // SM-T1a timer running ...
  Millis confirm_since = 0;     // ... since
  bool raw_closed = false;      // TRIG1 at the last sample
  Millis open_since = 0;        // raw open without a break since

  bool pulsing() const { return phase == EffectPhase::Pulse; }
  bool busy() const { return phase != EffectPhase::Idle; }
  bool recovering() const { return phase == EffectPhase::Recovery || recovery_bg || recover != RecoverRun::NoRecover || phase == EffectPhase::RecoverInterval; }
  bool windowFull(Millis now) const { return window_.isOpen(now) && runsLeft(now) == 0; }
  uint32_t runsLeft(Millis now) const { return window_.left(now, max_runs_); }
  uint32_t cooldownRemaining(Millis now) const { return window_.isOpen(now) ? window_.remainingMs(now) : 0; }
  // Why a run cannot start now (ignoring TRIG1), or None. Ready = None (SM-A1).
  EffectRefused refusal(Millis now) const { return refusalInner(now, true); }
  bool ready(Millis now) const { return refusal(now) == EffectRefused::None; }

  // A remote trigger (web, MQTT, serial): starts now if Ready, else waits (one deep).
  EffectRefused remoteTrigger(Millis now, const EffectConfig& c, EffectEvent& started);
  bool cancelRemote() { bool q = queued_remote; queued_remote = false; return q; }
  EffectEvent safetyOff();
  // Recover (TRIG2, web, MQTT, serial). A second press during a timed or held recover stops it.
  EffectRefused recoverPress(Millis now, const EffectConfig& c, bool& queued);
  void recoverRelease() { if (recover == RecoverRun::RecoverHold) recover = RecoverRun::NoRecover; }
  // STOP / pause: the cycle, a recover and the queues end; the next local run needs a release first.
  void stop() {
    phase = EffectPhase::Idle;
    recover = RecoverRun::NoRecover;
    recovery_bg = queued_remote = queued_recover = released = confirming = false;
  }
  // Advance with TRIG1's raw sample now (every 1 ms).
  std::vector<EffectEvent> tick(Millis now, const EffectConfig& c, bool trig1_closed);
  // OUT1 / OUT2 / OUT3 now.
  void outputs(bool& out1, bool& out2, bool& out3) const;
  Pattern pattern(Millis now) const;
  // The state for the state panel (one of EFFECT_STATES).
  const char* stateName(Millis now) const;

 private:
  EffectRefused refusalInner(Millis now, bool window) const;
  EffectEvent start(Millis now, const EffectConfig& c);
  void startRecover(Millis now, const EffectConfig& c);
  EffectEvent afterGap(Millis now, const EffectConfig& c, bool cycle);
  Millis boot_until_;
  RunWindow window_;
  uint32_t max_runs_;
  bool booted_ = false;
  bool seen_open_ = false;   // TRIG1 read open at least once since power-on (SM-B3)
  bool recover_cycle_ = false;  // the running cycle is a recover (no OUT3 stage)
};

}  // namespace pt
