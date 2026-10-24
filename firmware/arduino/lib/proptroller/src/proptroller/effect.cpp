#include "effect.h"

namespace pt {

static const char* const RECOVER_NAMES[] = {"cycle", "timed", "hold"};
const char* recoverModeName(RecoverMode m) { return RECOVER_NAMES[(int)m]; }
bool parseRecoverMode(const std::string& s, RecoverMode& out) {
  for (int i = 0; i < 3; i++)
    if (s == RECOVER_NAMES[i]) { out = (RecoverMode)i; return true; }
  return false;
}

static uint32_t clampRuns(uint8_t n) { return n < EFFECT_RUNS_MIN ? EFFECT_RUNS_MIN : n; }

EffectMachine::EffectMachine(const EffectConfig& c, Millis now)
    : safety_on(!c.auto_arm), boot_until_(now + EFFECT_BOOT_LOCKOUT_MS) {
  setMaxRuns(clampRuns(c.runs_per_cooldown));
}

EffectRefused EffectMachine::refusalInner(Millis now, bool window) const {
  if (safety_on) return EffectRefused::Safety;
  if (now < boot_until_) return EffectRefused::Boot;
  if (window && windowFull(now)) return EffectRefused::CooldownFull;
  if (busy()) return EffectRefused::Busy;
  if (recover != RecoverRun::NoRecover) return EffectRefused::Recovering;
  return EffectRefused::None;
}

EffectEvent EffectMachine::start(Millis now, const EffectConfig& c) {
  started(now, c.cooldown_ms);  // the gate: queue empty, a new release needed (SM-T6), the window counts (SM-L1)
  run_count++;
  recover_cycle_ = false;
  phase = EffectPhase::Pulse;
  phase_until = now + c.out1_on_ms;
  EffectEvent e;
  e.kind = EffectEvent::Kind::Started;
  e.value = run_count;
  return e;
}

EffectRefused EffectMachine::remoteTrigger(Millis now, const EffectConfig& c, EffectEvent& started) {
  setMaxRuns(clampRuns(c.runs_per_cooldown));
  started = EffectEvent();
  if (!c.remote_trigger_enable) return EffectRefused::RemoteDisabled;  // SM-Q4
  EffectRefused r = refusal(now);
  if (r == EffectRefused::None) {
    started = start(now, c);
    started.remote = true;
    return r;
  }
  if (r == EffectRefused::Safety || r == EffectRefused::CooldownFull) return r;  // SM-L5
  if (!queue()) return EffectRefused::AlreadyQueued;                             // SM-Q1
  return EffectRefused::None;
}

EffectEvent EffectMachine::safetyOff() {
  EffectEvent e;
  if (safety_on) {
    safety_on = false;
    released = false;  // Q1: the safety hold only releases the safety; a release and a new press start a run
    e.kind = EffectEvent::Kind::SafetyOff;
  }
  return e;
}

void EffectMachine::startRecover(Millis now, const EffectConfig& c) {
  switch (c.recover_mode) {
    case RecoverMode::Cycle:
      // OUT1 stays off for the pulse time, then the recovery; it counts as a run but uses no window run (SM-PR3)
      // and adds no cooldown (Q2)
      run_count++;
      recover_cycle_ = true;
      phase = EffectPhase::RecoverInterval;
      phase_until = now + c.out1_on_ms;
      break;
    case RecoverMode::Timed: recover = RecoverRun::RecoverTimed; recover_until = now + c.recover_time_ms; break;
    case RecoverMode::Hold: recover = RecoverRun::RecoverHold; recover_until = now + EFFECT_RECOVER_HOLD_CAP_MS; break;
  }
}

EffectRefused EffectMachine::recoverPress(Millis now, const EffectConfig& c, bool& queued) {
  queued = false;
  if (recover != RecoverRun::NoRecover) { recover = RecoverRun::NoRecover; return EffectRefused::Recovering; }
  EffectRefused r = refusalInner(now, false);  // Q3: a Full window does not hold a recover back
  if (r == EffectRefused::None) { startRecover(now, c); return r; }
  if (r == EffectRefused::Busy && c.recover_mode == RecoverMode::Cycle) {  // SM-PR2: remembered once
    queued_recover = true;
    queued = true;
    return EffectRefused::None;
  }
  return r;
}

EffectEvent EffectMachine::afterGap(Millis now, const EffectConfig& c, bool cycle) {
  EffectEvent e;
  if (c.out3_enabled && !cycle) {
    phase = EffectPhase::After;
    phase_until = now + c.out3_on_ms;
    return e;
  }
  uint32_t ms = c.out2_on_ms;
  last_recovery_ms = ms;
  phase = EffectPhase::Recovery;
  phase_until = now + ms;
  e.kind = EffectEvent::Kind::RecoveryStarted;
  e.value = ms;
  return e;
}

std::vector<EffectEvent> EffectMachine::tick(Millis now, const EffectConfig& c, bool trig1_closed) {
  // raw TRIG1 (SM-T1): an open sample breaks a closure, a closed sample breaks an opening
  sampleRaw(now, trig1_closed);
  std::vector<EffectEvent> out;
  setMaxRuns(clampRuns(c.runs_per_cooldown));
  if (now >= boot_until_) booted_ = true;
  if (recovery_bg && now >= recovery_bg_until) recovery_bg = false;
  if (recover != RecoverRun::NoRecover && now >= recover_until) recover = RecoverRun::NoRecover;
  EffectEvent e;
  switch (phase) {
    case EffectPhase::Pulse:
    case EffectPhase::RecoverInterval:
      if (now >= phase_until) { phase = EffectPhase::Gap; phase_until = now + EFFECT_GAP_MS; }
      break;
    case EffectPhase::Gap:
      if (now >= phase_until) {
        if (recovery_bg) phase = EffectPhase::WaitRecovery;
        else e = afterGap(now, c, recover_cycle_);
      }
      break;
    case EffectPhase::WaitRecovery:
      if (!recovery_bg) e = afterGap(now, c, recover_cycle_);
      break;
    case EffectPhase::After:
      if (now >= phase_until) {
        // Ready again; the recovery runs in the background (SM-F4)
        phase = EffectPhase::Idle;
        uint32_t ms = c.out2_on_ms;
        last_recovery_ms = ms;
        recovery_bg = true;
        recovery_bg_until = now + ms;
        e.kind = EffectEvent::Kind::RecoveryStarted;
        e.value = ms;
      }
      break;
    case EffectPhase::Recovery:
      if (now >= phase_until) phase = EffectPhase::Idle;
      break;
    case EffectPhase::Idle:
    default:
      break;
  }
  if (e.kind != EffectEvent::Kind::None) out.push_back(e);
  // the gate (gate.h): the queue's cancellations, the release outside a Full window, the hold to confirm
  bool rdy = ready(now);
  bool local = update(now, trig1_closed, c.remote_trigger_enable, rdy, c.release_ms, c.confirm_ms);
  // one thing starts per tick: a queued remote run, then TRIG1, then a remembered recover (§13)
  if (rdy && (queued_remote || local)) {
    bool remote = queued_remote;
    EffectEvent s = start(now, c);
    s.remote = remote;
    out.push_back(s);
  } else if (queued_recover && refusalInner(now, false) == EffectRefused::None) {
    queued_recover = false;
    startRecover(now, c);
    EffectEvent p;
    p.kind = EffectEvent::Kind::RecoverStarted;
    out.push_back(p);
  }
  return out;
}

void EffectMachine::outputs(bool& out1, bool& out2, bool& out3) const {
  out1 = phase == EffectPhase::Pulse;
  out2 = phase == EffectPhase::Recovery || recovery_bg || recover != RecoverRun::NoRecover;
  out3 = phase == EffectPhase::After;
}

const char* const EFFECT_STATES[7] = {"starting", "safety", "ready", "pulse", "recovery", "recover", "cooldown"};

const char* EffectMachine::stateName(Millis now) const {
  if (!booted_) return "starting";
  if (safety_on) return "safety";
  if (pulsing()) return "pulse";
  if (recover != RecoverRun::NoRecover || phase == EffectPhase::RecoverInterval) return "recover";
  if (busy()) return "recovery";
  if (windowFull(now)) return "cooldown";
  return "ready";
}

Pattern EffectMachine::pattern(Millis now) const {
  if (!booted_) return Pattern(Pattern::Kind::Off);
  if (recover != RecoverRun::NoRecover) return Pattern(Pattern::Kind::TripleBlink);
  if (busy()) return Pattern(Pattern::Kind::FastBlink);
  if (windowFull(now)) return Pattern(Pattern::Kind::SlowBlink);
  if (safety_on) return Pattern(Pattern::Kind::Heartbeat);
  if (!released && raw_closed) return Pattern(Pattern::Kind::Flicker);
  return Pattern(Pattern::Kind::Steady);
}

}  // namespace pt
