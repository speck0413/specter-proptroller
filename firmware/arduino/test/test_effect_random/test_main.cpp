// Randomized state-machine test of the generic pulse-and-recover effect (firmware/STATE-MACHINE.md §14): random
// holds, taps, contact chatter, web / MQTT / serial requests, enables, session losses, cancels, recovers and
// restarts on the simulated board, 1 ms at a time; the invariants I1-I10 are checked at every millisecond. The
// stimuli include the E21-E24 cases: closures of 1-99 ms and chatter trains while Ready (E21), long holds (E22),
// single open samples inside a hold (E23) and Ready lost while TRIG1 is held (a recover or a recovery starting,
// E24); I10 is what each of them must satisfy.
#include "../effect_common.h"
#include <random>
#include <stdlib.h>

void setUp() {}
void tearDown() {}

struct Run {
  uint32_t seed;
  int n;           // runs_per_cooldown
  Millis cooldown;  // cooldown_ms
};

struct Totals {
  size_t runs = 0, local = 0, remote = 0, chatter = 0, checks = 0, short_closures = 0, ready_lost_held = 0, entries = 0;
};

static void run(const Run& cfg, Totals& tot) {
  std::mt19937 rng(cfg.seed);
  auto chance = [&](int per_100k) { return (int)(rng() % 100000) < per_100k; };
  FxSim s;
  char b[64];
  snprintf(b, sizeof b, "runs_per_cooldown=%d", cfg.n);
  std::string c1 = b;
  snprintf(b, sizeof b, "cooldown_ms=%llu", (unsigned long long)cfg.cooldown);
  std::string c2 = b;
  // the relays pulse with every run, interlocked; RLY1 and OUT2 have maximum on-times shorter than their pulses
  for (const char* l : {"out1_on_ms=200", "out2_on_ms=400", "mqtt_broker=127.0.0.1", c1.c_str(), c2.c_str(), "interlock_relays=true",
                        "rly1_role=trigger_repeat", "rly2_role=sound_trigger", "rly1_max_on_ms=100", "out2_max_on_ms=300"})
    TEST_ASSERT_TRUE_MESSAGE(s.serial(l).find("error") == std::string::npos, l);
  s.step(2500);
  s.ctl().mqttSubscriptions();
  const Millis OUT1_MS = 200, DEB = pt::TRIGGER_DEBOUNCE_MS, REL = pt::TRIGGER_DEBOUNCE_MS;
  bool trig = false, recover_in = false, web_held = false;
  Millis trig_changed = s.now(), chatter_until = 0;
  uint32_t session = 0;
  bool model_released = true;      // a release (raw open >= REL, outside a full window) since the last run
  Millis closed_ready_since = 0;   // TRIG1 closed and Ready and released, without a break, since
  bool closed_ready = false;
  std::vector<Millis> window_runs;
  Millis window_start = 0, out1_on_at = 0, last_restart = 0;
  bool window_open = false, out1_was = false, cc1_was = false, cc2_was = false, pulse_was = false;
  size_t runs_seen = 0, cc1_edges = 0, cc2_edges = 0;
  size_t entries = 0, remote_used = 0;  // I6: accepted queue entries vs remote runs
  Millis closure_start = 0;
  bool closure_ready = false, ready_was = false;
  const Millis MAX_ON[CHANNEL_COUNT] = {0, 300, 0, 0, 100, 0, 0, 0};
  Millis on_since[CHANNEL_COUNT] = {0};
  for (int step = 0; step < 300000; step++) {
    Millis now = s.now();
    // --- TRIG1: long holds, taps of 1-99 ms, and bursts of contact chatter (1-3 ms flips)
    if (now < chatter_until) {
      if (chance(40000)) { trig = !trig; trig_changed = now; }
    } else if (trig) {
      if (chance(now - trig_changed < 100 ? 3000 : 400)) { trig = false; trig_changed = now; }
    } else {
      if (chance(150)) { trig = true; trig_changed = now; }
      if (chance(20)) { chatter_until = now + 5 + rng() % 60; tot.chatter++; }
    }
    if (trig && !fx(s).raw_closed) { closure_start = now; closure_ready = fx(s).released; }
    if (!trig && fx(s).raw_closed && closure_ready && now - closure_start < DEB) tot.short_closures++;  // E21/E23
    s.setInput(Input::Trig1, trig);
    if (chance(30)) { recover_in = !recover_in; s.setInput(Input::Trig2, recover_in); }
    // --- remote requests
    bool pulse_at_start = fx(s).phase == pt::EffectPhase::Pulse;
    bool q_at_start = fx(s).queued_remote;
    if (chance(60)) { web_held = !web_held; act(s, "trigger", web_held, session); }
    if (chance(15)) act(s, "cancel", true, session);
    if (chance(15)) s.serial(chance(50000) ? "remote_trigger_enable=true" : "remote_trigger_enable=false");
    if (chance(15)) { session = (uint32_t)(1 + rng() % 3); setp(s, "remote_trigger_enable", "true", session); }
    if (chance(15) && session) s.sessionClosed(session);
    if (chance(15)) s.ctl().mqttOnMessage("proptroller/fx/control/trigger", "");
    if (chance(8)) s.serial("trigger");
    if (chance(8)) s.ctl().mqttOnMessage("proptroller/fx/control/recover", "");
    bool started_by_request = !pulse_at_start && fx(s).phase == pt::EffectPhase::Pulse;
    if ((!q_at_start && fx(s).queued_remote) || started_by_request) entries++;
    bool enable_before = s.ctl().settings().fx.remote_trigger_enable;
    bool queued_before = fx(s).queued_remote || started_by_request;
    bool safety_before = fx(s).safety_on;
    if (chance(8) && now - last_restart > 3000) {
      s.serial("restart");
      s.step(TICK);  // the board restarts at its next step
      last_restart = s.now();
      for (auto ch : ALL_CHANNELS) TEST_ASSERT_EQUAL_MESSAGE(0, s.level(ch), "I9 outputs off after a restart");
      TEST_ASSERT_EQUAL_MESSAGE(cfg.n, (int)fx(s).runsLeft(s.ctl().now()), "I9 window empty");
      TEST_ASSERT_FALSE_MESSAGE(fx(s).queued_remote || fx(s).queued_recover, "I9 queues empty");
      TEST_ASSERT_FALSE_MESSAGE(s.ctl().settings().fx.remote_trigger_enable, "I9 enable off");
      tot.checks += 4;
      window_open = false;
      window_runs.clear();
      model_released = false;
      closed_ready = false;
      out1_was = pulse_was = false;
      for (auto& x : on_since) x = 0;
      trig_changed = s.now();
      continue;
    }
    if (!started_by_request) s.tick1();
    Millis t = s.now();
    const pt::EffectMachine& m = fx(s);
    Millis fw = s.ctl().now();
    bool full = m.windowFull(fw);
    // a run starts on the 1 ms sample that accepts it (the outputs follow on the next 10 ms step)
    bool pulse = m.phase == pt::EffectPhase::Pulse;
    if (pulse && !pulse_was) {
      runs_seen++;
      TEST_ASSERT_TRUE_MESSAGE(fw >= pt::EFFECT_BOOT_LOCKOUT_MS && !safety_before, "I2 no run in the boot lockout or with the safety on");
      if (queued_before) {
        tot.remote++;
        TEST_ASSERT_TRUE_MESSAGE(enable_before, "I3 no remote run without the enable");
        TEST_ASSERT_TRUE_MESSAGE(remote_used < entries, "I6 one queued request gives at most one run");
        remote_used++;
      } else {
        tot.local++;
        TEST_ASSERT_TRUE_MESSAGE(trig, "I10 a local run only with TRIG1 closed");
        TEST_ASSERT_TRUE_MESSAGE(closed_ready && t - closed_ready_since >= DEB, "I10 closed on every sample for trig_debounce_ms, Ready and released throughout");
        TEST_ASSERT_TRUE_MESSAGE(model_released, "I7 a local run is preceded by a release");
      }
      model_released = false;
      closed_ready = false;
      if (cfg.cooldown > 0) {
        if (!window_open || t >= window_start + cfg.cooldown) { window_open = true; window_start = t; window_runs.clear(); }
        window_runs.push_back(t);
        TEST_ASSERT_TRUE_MESSAGE((int)window_runs.size() <= cfg.n, "I1 at most N runs per window");
      }
      tot.checks += 4;
    } else if (!pulse) {
      bool ready_now = m.refusal(fw) == pt::EffectRefused::None;
      if (trig && ready_was && !ready_now && model_released) tot.ready_lost_held++;  // E24
      ready_was = ready_now;
      // the model of SM-T7 / SM-T1a after this sample (the firmware decides on the same samples)
      if (!trig && t - trig_changed >= REL && !full) model_released = true;
      if (trig && model_released && ready_now) {
        if (!closed_ready) { closed_ready = true; closed_ready_since = t; }
      } else {
        closed_ready = false;
      }
    }
    pulse_was = pulse;
    // the outputs, on the 10 ms steps
    bool out1 = s.level(OUT1) > 0, out2 = s.level(OUT2) > 0, cc1 = s.level(Channel::Cc1) > 0, cc2 = s.level(Channel::Cc2) > 0;
    if (cc1 && !cc1_was) cc1_edges++;
    if (cc2 && !cc2_was) cc2_edges++;
    cc1_was = cc1;
    cc2_was = cc2;
    if (out1 && !out1_was) out1_on_at = t;
    if (out1) TEST_ASSERT_TRUE_MESSAGE(t - out1_on_at <= OUT1_MS + TICK, "I4 OUT1 on at most out1_on_ms");
    if (m.phase == pt::EffectPhase::RecoverInterval) TEST_ASSERT_FALSE_MESSAGE(out1 && !out1_was, "I4 OUT1 never turns on during a recover");
    TEST_ASSERT_FALSE_MESSAGE(out1 && out2, "I5 OUT1 and OUT2 never on together");
    TEST_ASSERT_FALSE_MESSAGE(s.level(Channel::Rly1) && s.level(Channel::Rly2), "an interlocked pair is never both on");
    for (int k = 0; k < CHANNEL_COUNT; k++) {
      if (!s.level(ALL_CHANNELS[k])) on_since[k] = 0;
      else if (!on_since[k]) on_since[k] = t;
      if (MAX_ON[k] && on_since[k]) TEST_ASSERT_TRUE_MESSAGE(t - on_since[k] <= MAX_ON[k] + TICK, "the maximum on-time is never exceeded");
    }
    tot.checks += 3;
    out1_was = out1;
  }
  s.step(1000);
  TEST_ASSERT_EQUAL_MESSAGE(runs_seen, cc1_edges, "I8 one trigger-out pulse per run (none for a recover)");
  TEST_ASSERT_EQUAL_MESSAGE(runs_seen, cc2_edges, "I8 one sound-out pulse per run (none for a recover)");
  tot.runs += runs_seen;
  tot.entries += entries;
}

void randomized_invariants_I1_to_I10() {
  Totals tot;
  const Run boards[] = {{1, 3, 6000}, {2, 1, 3000}, {3, 3, 0}, {4, 5, 9000}, {5, 2, 4000}, {6, 3, 180000}, {7, 20, 2000}, {8, 1, 0}};
  for (auto& r : boards) run(r, tot);
  char b[260];
  snprintf(b, sizeof b, "%u boards x 300 s: %u runs (%u local, %u remote of %u queue entries), %u chatter bursts, %u short closures after a release (E21/E23), %u Ready losses while held (E24), %u checks",
           (unsigned)(sizeof boards / sizeof boards[0]), (unsigned)tot.runs, (unsigned)tot.local, (unsigned)tot.remote, (unsigned)tot.entries,
           (unsigned)tot.chatter, (unsigned)tot.short_closures, (unsigned)tot.ready_lost_held, (unsigned)tot.checks);
  TEST_MESSAGE(b);
  TEST_ASSERT_TRUE(tot.local > 30 && tot.remote > 10 && tot.short_closures > 100 && tot.ready_lost_held > 5);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(randomized_invariants_I1_to_I10);
  return UNITY_END();
}
