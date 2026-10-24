// Every rule (SM-...), scenario (E1-E26) and owner answer (Q1-Q4) of firmware/STATE-MACHINE.md
// for the generic firmware's pulse-and-recover effect, one named test each (the invariants I1-I10 are in
// test_effect_random). The board samples TRIG1 every 1 ms and changes outputs on its 10 ms tick, so output
// times are checked to one tick.
#include "../effect_common.h"

void setUp() {}
void tearDown() {}

// A fast board: 200 ms pulse, 400 ms recovery.
static void fast(Sim& s, std::initializer_list<const char*> extra = {}) {
  s.serial("out1_on_ms=200\n");
  s.serial("out2_on_ms=400\n");
  for (auto e : extra) s.serial(std::string(e) + "\n");
  s.step(2500);
}
static Millis firstRun(Sim& s, Millis from) { return when(s, OUT1, from, on); }
static void hold(Sim& s, Millis ms) { s.setInput(Input::Trig1, true); s.step(ms); }
static void open1(Sim& s, Millis ms) { s.setInput(Input::Trig1, false); s.step(ms); }

// ---------------------------------------------------------------- Ready
void SM_A1_ready_needs_every_condition() {
  using pt::EffectRefused;
  FxSim s;
  s.serial("auto_arm=false\n");
  s.serial("out1_on_ms=200\n");
  s.serial("out2_on_ms=400\n");
  s.serial("runs_per_cooldown=2\n");
  s.serial("cooldown_ms=5000\n");
  s.step(1500);
  s.powerCycle(100);
  auto why = [&]() { return fx(s).refusal(s.ctl().now()); };
  s.step(100);
  TEST_ASSERT_TRUE_MESSAGE(why() != EffectRefused::None, "boot lockout / safety: not Ready");
  s.step(2500);
  TEST_ASSERT_EQUAL_MESSAGE(EffectRefused::Safety, why(), "safety on: not Ready");
  hold(s, 2100);
  open1(s, 150);
  TEST_ASSERT_EQUAL_MESSAGE(EffectRefused::None, why(), "Ready");
  TEST_ASSERT_TRUE(armed(s));
  s.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL_MESSAGE(EffectRefused::Busy, why(), "run cycle running: not Ready");
  s.step(1000);
  s.press(Input::Trig1, 150);
  s.step(1000);
  TEST_ASSERT_EQUAL_MESSAGE(EffectRefused::CooldownFull, why(), "window Full: not Ready");
  s.step(5000);
  TEST_ASSERT_EQUAL(EffectRefused::None, why());
  s.serial("recover_mode=timed\n");
  s.serial("recover\n");
  TEST_ASSERT_TRUE_MESSAGE(why() != EffectRefused::None, "recover running: not Ready");
  s.step(pt::EFFECT_RECOVER_TIME_DEFAULT_MS + 100);
  TEST_ASSERT_EQUAL(EffectRefused::None, why());
  // a timed recover running: not Ready
  s.serial("recover\n");
  TEST_ASSERT_EQUAL_MESSAGE(EffectRefused::Recovering, why(), "recover running: not Ready");
}

// ---------------------------------------------------------------- settings
void SM_S1_out_of_range_refused_nothing_changes() {
  FxSim s;
  fast(s);
  Response r = setp(s, "cooldown_ms", "4000000");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE(r.errorMessage().find("allows 0-3600000 ms") != std::string::npos);
  TEST_ASSERT_EQUAL(180000, s.ctl().settings().fx.cooldown_ms);
  std::string out = s.serial("runs_per_cooldown=99\n");
  TEST_ASSERT_TRUE_MESSAGE(out.find("error: ") == 0 && out.find("1-20") != std::string::npos, out.c_str());
  TEST_ASSERT_EQUAL(3, s.ctl().settings().fx.runs_per_cooldown);
}

void SM_S2_stored_values_survive_and_read_back_everywhere() {
  FxSim s;
  fast(s, {"mqtt_broker=127.0.0.1"});
  s.ctl().mqttSubscriptions();
  s.ctl().mqttOnMessage("proptroller/fx/config/set/runs_per_cooldown", "5");
  s.step(1500);
  s.powerCycle(300);
  s.ctl().mqttSubscriptions();
  TEST_ASSERT_EQUAL_STRING("runs_per_cooldown=5", s.serial("runs_per_cooldown\n").c_str());
  auto m = s.ctl().mqttOnMessage("proptroller/fx/config/get/runs_per_cooldown", "");
  TEST_ASSERT_EQUAL_STRING("5", m[0].payload.c_str());
  Response r = s.request(R"({"op":"get_params"})", 3);
  bool found = false;
  JsonArray values = r.doc()["result"]["values"];
  for (JsonVariant v : values)
    if (v["key"] == "runs_per_cooldown") found = v["value"] == 5;
  TEST_ASSERT_TRUE(found);
}

// ---------------------------------------------------------------- boot and safety
void SM_B1_I9_restart_clears_everything() {
  FxSim s;
  fast(s, {"remote_trigger_enable=true"});
  s.press(Input::Trig1, 150);
  s.step(100);
  act(s, "trigger", true);  // queued during the run
  TEST_ASSERT_TRUE(statusBool(s, "queued"));
  s.serial("restart\n");
  s.step(TICK);  // the board restarts at its next step
  for (auto c : ALL_CHANNELS) TEST_ASSERT_EQUAL(0, s.level(c));
  TEST_ASSERT_EQUAL(3, statusInt(s, "runs_left"));
  TEST_ASSERT_FALSE(statusBool(s, "queued"));
  TEST_ASSERT_FALSE(s.ctl().settings().fx.remote_trigger_enable);
  TEST_ASSERT_EQUAL(0, statusInt(s, "run_count"));
  TEST_ASSERT_EQUAL_STRING("", std::string(s.ctl().settings().fx.remote_trigger_enable ? "x" : "").c_str());
}

void SM_B2_nothing_runs_or_recovers_in_the_boot_cooldown() {
  FxSim s;
  s.serial("remote_trigger_enable=true\n");
  s.step(300);
  s.press(Input::Trig1, 400);
  s.press(Input::Trig2, 150);
  act(s, "trigger", true);
  s.step(800);
  TEST_ASSERT_EQUAL(0, firstRun(s, 0));
  TEST_ASSERT_EQUAL(0, when(s, OUT2, 0, on));
}

void SM_B3_E19_held_at_power_on_never_starts_a_run() {
  FxSim s;
  s.setInput(Input::Trig1, true);
  s.powerCycle(100);
  s.step(5000);
  TEST_ASSERT_EQUAL(0, firstRun(s, 0));
  open1(s, 150);
  Millis t = s.now();
  hold(s, 150);
  TEST_ASSERT_TRUE(firstRun(s, t) > 0);
}

void SM_B4_E20_Q1_safety_hold_never_starts_a_run() {
  FxSim s;
  s.serial("auto_arm=false\n");
  s.step(1500);
  s.powerCycle(100);
  s.step(2500);
  s.press(Input::Trig1, 300);
  s.step(300);
  TEST_ASSERT_EQUAL_MESSAGE(0, firstRun(s, 0), "nothing starts a run with the safety on");
  hold(s, 6000);  // E20: hold 2 s and keep holding
  TEST_ASSERT_FALSE(statusBool(s, "safety"));
  TEST_ASSERT_EQUAL_MESSAGE(0, firstRun(s, 0), "Q1: the safety hold only releases the safety");
  TEST_ASSERT_FALSE_MESSAGE(fx(s).released, "SM-T6: it clears release-seen");
  open1(s, 150);
  s.press(Input::Trig1, 150);
  TEST_ASSERT_TRUE_MESSAGE(firstRun(s, 0) > 0, "release + press starts a run");
}

void SM_B5_restart_from_web_mqtt_and_serial_no_stop_command() {
  for (int k = 0; k < 3; k++) {
    FxSim s;
    fast(s, {"mqtt_broker=127.0.0.1"});
    s.ctl().mqttSubscriptions();
    if (k == 0) s.request(R"({"op":"reboot"})", 2);
    if (k == 1) s.ctl().mqttOnMessage("proptroller/fx/control/restart", "");
    if (k == 2) s.serial("restart\n");
    s.step(10);
    TEST_ASSERT_EQUAL(2, s.boots);
  }
  FxSim s;
  Response r = s.request(R"({"op":"action","key":"stop"})");
  TEST_ASSERT_EQUAL_STRING("not_found", code(r).c_str());
}

// ---------------------------------------------------------------- run cycle
void SM_F1_I4_out1_exactly_its_time() {
  FxSim s;
  fast(s);
  s.press(Input::Trig1, 150);
  s.step(1000);
  Millis on_at = firstRun(s, 0), off_at = when(s, OUT1, on_at, off);
  TEST_ASSERT_EQUAL(200, off_at - on_at);
}

void SM_F2_recovery_time_is_out2_on_ms_read_when_out2_starts() {
  FxSim s;
  fast(s, {"out2_on_ms=2500"});
  s.press(Input::Trig1, 150);
  s.step(150);
  s.serial("out2_on_ms=3000");  // changed before OUT2 starts: the new value is used
  s.step(5000);
  Millis p = when(s, OUT2, 0, on);
  TEST_ASSERT_INT_WITHIN(TICK, 3000, when(s, OUT2, p, off) - p);
  Response r = setp(s, "out2_time_from_knob", "true");
  TEST_ASSERT_FALSE_MESSAGE(r.isOk(), "there is no knob (SM-F2)");
}

void SM_F3_I5_out1_and_out2_never_together_and_the_next_run_waits() {
  FxSim s;
  fast(s, {"cooldown_ms=0"});
  hold(s, 3000);
  auto a = s.changes(OUT1, 0), p = s.changes(OUT2, 0);
  TEST_ASSERT_EQUAL_MESSAGE(2, a.size(), "held through: one run (SM-T5)");
  open1(s, 200);
  hold(s, 2000);
  auto aa = s.changes(OUT1, 0), pp = s.changes(OUT2, 0);
  // the second run starts only after the first recovery ended
  TEST_ASSERT_TRUE(aa.size() >= 3 && aa[2].first >= pp[1].first);
}

void SM_F4_E18_out3_run_during_the_background_recovery_waits_for_it() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "out3_enabled=true", "out3_on_ms=200", "out2_on_ms=3000"});
  s.press(Input::Trig1, 150);
  s.step(800);  // OUT1 200, OUT3 200: Ready again, the recovery runs
  TEST_ASSERT_TRUE(armed(s));
  TEST_ASSERT_TRUE(s.level(OUT2) > 0);
  Millis t = s.now();
  s.press(Input::Trig1, 150);
  s.step(5000);
  Millis run2 = firstRun(s, t), recovery_end = when(s, OUT2, t, off), out3_next = when(s, OUT3, t, on);
  TEST_ASSERT_TRUE_MESSAGE(run2 > 0 && run2 < recovery_end, "starts a run during the recovery");
  TEST_ASSERT_TRUE_MESSAGE(out3_next >= recovery_end, "its recovery waits for the recovery");
}

void SM_F5_E11_every_source_pulses_and_sounds() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "remote_trigger_enable=true", "mqtt_broker=127.0.0.1"});
  s.ctl().mqttSubscriptions();
  for (int k = 0; k < 4; k++) {
    Millis t0 = s.now();
    size_t plays = s.world().df.plays.size();
    if (k == 0) s.press(Input::Trig1, 150);
    if (k == 1) { act(s, "trigger", true, 0); act(s, "trigger", false, 0); }
    if (k == 2) s.ctl().mqttOnMessage("proptroller/fx/control/trigger", "");
    if (k == 3) s.serial("trigger\n");
    s.step(1000);
    auto cc1 = s.changes(Channel::Cc1, t0), cc2 = s.changes(Channel::Cc2, t0);
    TEST_ASSERT_EQUAL(2, cc1.size());
    TEST_ASSERT_INT_WITHIN(TICK, 200, cc1[1].first - cc1[0].first);  // one 10 ms tick
    TEST_ASSERT_EQUAL(2, cc2.size());
    TEST_ASSERT_TRUE(s.world().df.plays.size() > plays);
  }
}

void SM_F6_status_firing_and_reloading() {
  FxSim s;
  fast(s);
  s.press(Input::Trig1, 150);
  TEST_ASSERT_TRUE(statusBool(s, "pulsing"));
  s.step(300);
  TEST_ASSERT_FALSE(statusBool(s, "pulsing"));
  TEST_ASSERT_TRUE_MESSAGE(statusBool(s, "recovering"), "recovering");
}

// ---------------------------------------------------------------- cooldown window
void SM_L1_L2_E4_three_per_window_fourth_refused_with_time_left() {
  FxSim s;
  fast(s, {"remote_trigger_enable=true"});
  for (int i = 0; i < 3; i++) { s.press(Input::Trig1, 150); s.step(800); }
  TEST_ASSERT_EQUAL(3, runs(s, 0));
  Millis t = s.now();
  s.press(Input::Trig1, 150);
  s.step(800);
  TEST_ASSERT_EQUAL(3, runs(s, 0));
  Response r = act(s, "trigger", true);
  TEST_ASSERT_TRUE(r.errorMessage().find("cooling down, 2:5") == 0);
  s.step(180000 - (s.now() - t) + 1000);
  open1(s, 200);
  s.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL(4, runs(s, 0));
}

void SM_L3_one_per_window_is_a_single_cooldown() {
  FxSim s;
  fast(s, {"runs_per_cooldown=1", "cooldown_ms=3000"});
  s.press(Input::Trig1, 150);
  s.step(1500);
  s.press(Input::Trig1, 150);
  s.step(500);
  TEST_ASSERT_EQUAL(1, runs(s, 0));
  s.step(1500);
  s.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL(2, runs(s, 0));
}

void SM_L4_SM_PR3_Q2_recover_uses_no_run_and_adds_no_cooldown() {
  FxSim s;
  fast(s, {"runs_per_cooldown=1", "cooldown_ms=60000"});
  s.press(Input::Trig2, 150);
  TEST_ASSERT_EQUAL_MESSAGE(pt::EffectRefused::Busy, fx(s).refusal(s.ctl().now()), "the recover's own cycle blocks pulsing");
  s.step(1500);
  TEST_ASSERT_EQUAL(1, statusInt(s, "run_count"));
  TEST_ASSERT_EQUAL_MESSAGE(1, statusInt(s, "runs_left"), "SM-PR3: no window run used");
  TEST_ASSERT_EQUAL_MESSAGE(0, statusInt(s, "cooldown_remaining_ms"), "Q2: no cooldown");
  s.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL(1, runs(s, 0));
}

// The phone page holds Recover down while its button is held only when info marks it momentary (hold mode);
// in cycle mode one tap runs one recovery.
void recover_action_is_momentary_only_in_hold_mode() {
  FxSim s;
  fast(s);
  auto momentary = [&]() { for (auto& a : s.ctl().info().actions) if (a.key == "recover") return a.momentary; TEST_FAIL_MESSAGE("no recover action"); return false; };
  TEST_ASSERT_FALSE_MESSAGE(momentary(), "cycle: a tap");
  s.serial("recover_mode=hold\n");
  TEST_ASSERT_TRUE_MESSAGE(momentary(), "hold: press and hold");
}

void E25_Q3_recover_during_a_full_window() {
  FxSim s;
  fast(s, {"runs_per_cooldown=1", "cooldown_ms=10000"});
  s.press(Input::Trig1, 150);
  s.step(1000);
  TEST_ASSERT_TRUE(fx(s).windowFull(s.ctl().now()));
  Millis t = s.now();
  s.press(Input::Trig2, 150);
  s.step(1500);
  TEST_ASSERT_TRUE_MESSAGE(when(s, OUT2, t, on) > 0, "the recover runs");
  TEST_ASSERT_EQUAL(0, when(s, OUT1, t, on));
  s.press(Input::Trig1, 150);
  s.step(500);
  TEST_ASSERT_EQUAL_MESSAGE(1, runs(s, 0), "pulsing stays locked");
  s.step(10000 - (s.now() - t));
  open1(s, 150);
  s.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL_MESSAGE(2, runs(s, 0), "until the window ends");
}

void E26_press_right_after_a_recover_starts_a_run() {
  FxSim s;
  fast(s);  // the default 180 s cooldown
  Millis t = s.now();
  s.press(Input::Trig2, 150);
  s.step(5);
  // the recover cycle: pulse interval (200), gap, recovery (400)
  Millis recovery_off = 0;
  for (int i = 0; i < 3000 && !recovery_off; i++) {
    s.step(1);
    Millis p = when(s, OUT2, t, on);
    if (p) recovery_off = when(s, OUT2, p, off);
  }
  TEST_ASSERT_TRUE(recovery_off > 0);
  hold(s, 200);
  Millis f = firstRun(s, t);
  TEST_ASSERT_TRUE_MESSAGE(f > 0 && f <= recovery_off + pt::TRIGGER_DEBOUNCE_MS + 2 * TICK, "the press starts a run: a recover adds no cooldown");
}

void SM_L5_full_window_ignores_and_cancels_the_queue() {
  FxSim s;
  fast(s, {"runs_per_cooldown=2", "cooldown_ms=60000", "remote_trigger_enable=true", "out2_on_ms=2000"});
  s.press(Input::Trig1, 150);
  s.step(300);
  TEST_ASSERT_TRUE(act(s, "trigger", true).isOk());  // queued: the second run of the window
  act(s, "trigger", false);
  s.step(3000);
  TEST_ASSERT_EQUAL(2, runs(s, 0));
  // full now: a press is not queued
  Response r = act(s, "trigger", true);
  TEST_ASSERT_EQUAL_STRING("refused", code(r).c_str());
  TEST_ASSERT_FALSE(statusBool(s, "queued"));
  // a queued request is cancelled when the window becomes full
  FxSim q;
  fast(q, {"runs_per_cooldown=1", "cooldown_ms=60000", "remote_trigger_enable=true", "out2_on_ms=2000"});
  q.press(Input::Trig1, 150);  // the only run of the window
  q.step(100);
  Response rq = act(q, "trigger", true);
  TEST_ASSERT_EQUAL_STRING("refused", code(rq).c_str());
}

void SM_L6_status_reports_runs_left_and_the_remaining_time() {
  FxSim s;
  fast(s);
  TEST_ASSERT_EQUAL(3, statusInt(s, "runs_left"));
  TEST_ASSERT_EQUAL(0, statusInt(s, "cooldown_remaining_ms"));
  s.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL(2, statusInt(s, "runs_left"));
  TEST_ASSERT_TRUE(statusInt(s, "cooldown_remaining_ms") > 179000);
}

void SM_L7_E14_lowering_n_mid_window_makes_it_full() {
  FxSim s;
  fast(s);
  s.press(Input::Trig1, 150);
  s.step(1000);
  s.serial("runs_per_cooldown=1\n");
  s.step(1);
  TEST_ASSERT_EQUAL(0, statusInt(s, "runs_left"));
  TEST_ASSERT_TRUE(fx(s).windowFull(s.ctl().now()));
  int64_t rem = statusInt(s, "cooldown_remaining_ms");
  s.serial("cooldown_ms=5000\n");
  TEST_ASSERT_TRUE_MESSAGE(statusInt(s, "cooldown_remaining_ms") >= rem - 5, "the running window keeps its end");
}

void SM_L8_E13_restart_mid_window_with_a_queued_run() {
  FxSim s;
  fast(s, {"remote_trigger_enable=true"});
  s.press(Input::Trig1, 150);
  s.step(100);
  act(s, "trigger", true);
  s.serial("restart\n");
  s.step(3000);
  TEST_ASSERT_EQUAL(3, statusInt(s, "runs_left"));
  TEST_ASSERT_EQUAL(1, runs(s, 0));
}

void E17_cooldown_zero_means_no_window() {
  FxSim s;
  fast(s, {"cooldown_ms=0"});
  for (int i = 0; i < 6; i++) { s.press(Input::Trig1, 150); s.step(800); }
  TEST_ASSERT_EQUAL(6, runs(s, 0));
}

// ---------------------------------------------------------------- TRIG1 gate
void SM_T1_Q4_release_time_and_confirm_time() {
  // TRIG1 is sampled every 1 ms (scripted changes land on their millisecond); the effect profile uses 100 ms
  // for both trig_release_ms and trig_debounce_ms (Q4)
  TEST_ASSERT_EQUAL(100, pt::TRIGGER_DEBOUNCE_MS);
  FxSim s;
  fast(s, {"cooldown_ms=0"});
  hold(s, 1500);  // one run, held through the whole cycle
  TEST_ASSERT_EQUAL(1, runs(s, 0));
  Millis t = s.now();
  s.script(Input::Trig1, {{1, false}, {100, true}});  // open for 99 ms: not a release
  s.step(400);
  TEST_ASSERT_EQUAL_MESSAGE(0, firstRun(s, t), "99 ms open is no release");
  t = s.now();
  s.script(Input::Trig1, {{1, false}, {102, true}});  // open for 101 ms
  s.step(400);
  TEST_ASSERT_TRUE(firstRun(s, t) > 0);
  // longer times lengthen both
  s.serial("trig_release_ms=300\n");
  s.serial("trig_debounce_ms=300\n");
  hold(s, 1500);
  t = s.now();
  s.script(Input::Trig1, {{1, false}, {251, true}});
  s.step(900);
  TEST_ASSERT_EQUAL_MESSAGE(0, firstRun(s, t), "250 ms open is no release at 300 ms");
  t = s.now();
  s.script(Input::Trig1, {{1, false}, {311, true}});
  s.step(900);
  Millis f = firstRun(s, t);
  TEST_ASSERT_TRUE_MESSAGE(f >= t + 311 + 300 && f <= t + 311 + 300 + TICK, "confirmed 300 ms after the closure");
}

void SM_T1a_E22_one_run_exactly_the_confirm_time_after_the_closure() {
  FxSim s;
  fast(s);
  Millis t = s.now();
  s.script(Input::Trig1, {{1, true}});
  s.step(300);
  // confirmed on the 1 ms sample 100 ms after the closure; the output follows on the next 10 ms tick
  Millis f = firstRun(s, t);
  TEST_ASSERT_TRUE_MESSAGE(f >= t + 1 + 100 && f <= t + 1 + 100 + TICK, "100 ms after the closure");
}

void E21_short_closures_and_chatter_never_start_a_run() {
  FxSim s;
  fast(s);
  std::vector<std::pair<Millis, bool>> p;
  Millis at = 1;
  for (Millis d : {1, 10, 50, 99}) { p.push_back({at, true}); p.push_back({at + d, false}); at += d + 150; }
  for (int i = 0; i < 40; i++) { p.push_back({at, true}); at += 1 + i % 5; p.push_back({at, false}); at += 1 + i % 3; }  // a fast train
  for (int i = 0; i < 10; i++) { p.push_back({at, true}); at += 90; p.push_back({at, false}); at += 1; }                  // nearly long enough, broken
  s.script(Input::Trig1, p);
  s.step(at + 200);
  TEST_ASSERT_EQUAL(0, runs(s, 0));
}

void E23_one_open_sample_restarts_the_timer() {
  FxSim s;
  fast(s);
  Millis t = s.now();
  s.script(Input::Trig1, {{1, true}, {61, false}, {62, true}});  // held 60 ms, one 1 ms open sample, held again
  s.step(400);
  Millis f = firstRun(s, t);
  TEST_ASSERT_TRUE_MESSAGE(f >= t + 62 + 100 && f <= t + 62 + 100 + TICK, "100 ms after the open sample, not before");
}

void E24_losing_ready_restarts_the_timer() {
  FxSim s;
  fast(s, {"cooldown_ms=0"});
  s.press(Input::Trig1, 150);
  s.step(300);
  hold(s, 0);
  // Ready returns when the recovery ends; take it away for a moment by starting a recover (it is not Ready)
  Millis recovery_end = 0;
  for (int i = 0; i < 1000 && !recovery_end; i++) {
    s.step(1);
    if (armed(s) || fx(s).refusal(s.ctl().now()) == pt::EffectRefused::None) recovery_end = s.now();
  }
  TEST_ASSERT_TRUE(recovery_end > 0);
  s.step(50);
  s.serial("recover_mode=timed\n");
  s.serial("recover_time_ms=100\n");
  s.serial("recover\n");  // Ready lost for 100 ms
  s.step(100);
  Millis back = 0;
  for (int i = 0; i < 50 && !back; i++) {
    if (fx(s).refusal(s.ctl().now()) == pt::EffectRefused::None) back = s.now();
    else s.step(1);
  }
  s.step(300);
  Millis started = firstRun(s, recovery_end);
  TEST_ASSERT_TRUE_MESSAGE(started >= back + 99 && started <= back + 101, "one run trig_debounce_ms after Ready returns");
}

void SM_T2_E1_taps_during_a_recovery_do_not_start_a_run() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "out2_on_ms=2000"});
  s.press(Input::Trig1, 150);
  s.step(300);
  for (int i = 0; i < 3; i++) { s.press(Input::Trig1, 150); s.step(200); }
  s.step(3000);
  TEST_ASSERT_EQUAL(1, runs(s, 0));
}

void SM_T3_E2_held_during_the_recovery_one_run() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "out2_on_ms=1000"});
  s.press(Input::Trig1, 150);
  s.step(300);
  hold(s, 3000);
  TEST_ASSERT_EQUAL(2, runs(s, 0));
}

void SM_T4_local_run_needs_all_three() {
  FxSim s;
  fast(s);
  TEST_ASSERT_EQUAL_MESSAGE(0, runs(s, 0), "Ready and released, but TRIG1 open: nothing");
  hold(s, 150);
  TEST_ASSERT_EQUAL(1, runs(s, 0));
}

void SM_T5_E3_held_since_the_last_run() {
  FxSim s;
  fast(s, {"cooldown_ms=0"});
  hold(s, 5000);
  TEST_ASSERT_EQUAL(1, runs(s, 0));
}

void SM_T6_a_remote_run_clears_the_release() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "remote_trigger_enable=true"});
  s.setInput(Input::Trig1, true);
  s.step(20);  // closed, not yet confirmed
  act(s, "trigger", true);
  s.step(3000);
  TEST_ASSERT_EQUAL_MESSAGE(1, runs(s, 0), "the held TRIG1 needs a release after the remote run");
}

void SM_T7_E5_E6_full_window_releases_do_not_count() {
  FxSim s;
  fast(s, {"runs_per_cooldown=1", "cooldown_ms=5000"});
  s.press(Input::Trig1, 150);
  s.step(500);
  for (int i = 0; i < 3; i++) { s.press(Input::Trig1, 150); s.step(400); }  // E6: presses during Full
  hold(s, 5000);  // held across the window end
  TEST_ASSERT_EQUAL(1, runs(s, 0));
  open1(s, 150);
  s.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL(2, runs(s, 0));
}

// ---------------------------------------------------------------- remote request and enable
void SM_Q1_Q2_E7_a_web_press_during_a_run_queues_one() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "remote_trigger_enable=true", "out2_on_ms=2000"});
  TEST_ASSERT_TRUE(act(s, "trigger", true).isOk());  // Ready: starts now
  s.step(500);
  TEST_ASSERT_TRUE_MESSAGE(act(s, "trigger", true).isOk(), "during the run: queued");
  TEST_ASSERT_TRUE(statusBool(s, "queued"));
  Response r = act(s, "trigger", true);
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("already queued") == 0, "one queued at most (SM-Q1)");
  TEST_ASSERT_TRUE(act(s, "trigger", false).isOk());  // a release changes nothing
  s.step(6000);
  TEST_ASSERT_EQUAL_MESSAGE(2, runs(s, 0), "the run and the queued one, no repeat");
}

void SM_R3_usb_json_lines_are_serial() {
  FxSim s;
  fast(s);
  std::string j = R"({"op":"set_params","values":[{"key":"remote_trigger_enable","value":true}]})";
  std::string out = s.ctl().callSerial(j.data(), j.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.c_str());
  Value v;
  TEST_ASSERT_TRUE(s.status("remote_trigger_by", v) && v.s == "serial");
  TEST_ASSERT_EQUAL_STRING("pin=(empty)", s.serial("pin").c_str());
  TEST_ASSERT_TRUE(s.serial("status").find("run_count=0") != std::string::npos);
  // the page lock does not apply to the USB serial port (local access)
  s.request(R"({"op":"set_params","values":[{"key":"pin","value":"door-42"},{"key":"page_lock","value":true}]})");
  std::string j2 = R"({"op":"set_params","values":[{"key":"out1_on_ms","value":250}]})";
  TEST_ASSERT_TRUE(s.ctl().callSerial(j2.data(), j2.size()).find("\"ok\":true") != std::string::npos);
  TEST_ASSERT_FALSE(s.request(j2).isOk());
}

void SM_Q3_queued_starts_when_ready_without_the_trigger_gate() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "remote_trigger_enable=true"});
  s.press(Input::Trig1, 150);
  s.step(50);
  act(s, "trigger", true);
  s.step(1000);
  TEST_ASSERT_EQUAL(2, runs(s, 0));
}

void SM_Q4_E10_remote_trigger_needs_the_enable() {
  FxSim s;
  fast(s, {"mqtt_broker=127.0.0.1"});
  s.ctl().mqttSubscriptions();
  auto m = s.ctl().mqttOnMessage("proptroller/fx/control/trigger", "");
  TEST_ASSERT_TRUE(m.size() == 1 && m[0].payload.find("not enabled") != std::string::npos);
  Response r = act(s, "trigger", true);
  TEST_ASSERT_EQUAL_STRING("refused", code(r).c_str());
  s.step(500);
  TEST_ASSERT_EQUAL(0, runs(s, 0));
}

void SM_Q5_E8_cancel_and_the_other_cancellations() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "remote_trigger_enable=true", "out2_on_ms=2000"});
  s.press(Input::Trig1, 150);
  s.step(100);
  act(s, "trigger", true);
  act(s, "cancel", true);
  s.step(4000);
  TEST_ASSERT_EQUAL_MESSAGE(1, runs(s, 0), "E8 Cancel");
  act(s, "trigger", false);
  s.press(Input::Trig1, 150);
  s.step(100);
  act(s, "trigger", true);
  s.serial("remote_trigger_enable=false\n");
  s.step(4000);
  TEST_ASSERT_EQUAL_MESSAGE(2, runs(s, 0), "the enable going off");
}

void SM_Q6_mqtt_and_serial_trigger_like_a_web_press() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "remote_trigger_enable=true", "mqtt_broker=127.0.0.1", "out2_on_ms=2000"});
  s.ctl().mqttSubscriptions();
  s.press(Input::Trig1, 150);
  s.step(100);
  s.ctl().mqttOnMessage("proptroller/fx/control/trigger", "");  // queued
  s.serial("trigger\n");                                    // already queued: still one
  s.step(5000);
  TEST_ASSERT_EQUAL(2, runs(s, 0));
}

void SM_R1_enable_false_at_power_on_never_stored() {
  FxSim s;
  fast(s, {"remote_trigger_enable=true"});
  s.step(2000);
  s.powerCycle(100);
  TEST_ASSERT_FALSE(s.ctl().settings().fx.remote_trigger_enable);
}

void SM_R2_E9_web_enable_ends_with_the_page() {
  FxSim s;
  fast(s, {"cooldown_ms=0", "out2_on_ms=2000"});
  TEST_ASSERT_FALSE_MESSAGE(setp(s, "remote_trigger_enable", "true", 0).isOk(), "plain HTTP cannot enable");
  TEST_ASSERT_TRUE(setp(s, "remote_trigger_enable", "true", 4).isOk());
  s.press(Input::Trig1, 150);
  s.step(100);
  act(s, "trigger", true, 4);
  s.sessionClosed(4);
  TEST_ASSERT_FALSE(s.ctl().settings().fx.remote_trigger_enable);
  s.step(4000);
  TEST_ASSERT_EQUAL(1, runs(s, 0));
  TEST_ASSERT_EQUAL(5000u, s.wsHeartbeatMs());
}

void SM_R3_mqtt_or_serial_enable_stays() {
  FxSim s;
  fast(s);
  s.serial("remote_trigger_enable=true\n");
  s.sessionClosed(1);
  TEST_ASSERT_TRUE(s.ctl().settings().fx.remote_trigger_enable);
}

void SM_R4_status_says_who_enabled() {
  FxSim s;
  fast(s, {"mqtt_broker=127.0.0.1"});
  s.serial("remote_trigger_enable=true\n");
  Value v;
  s.status("remote_trigger_by", v);
  TEST_ASSERT_EQUAL_STRING("serial", v.s.c_str());
  s.serial("remote_trigger_enable=false\n");
  setp(s, "remote_trigger_enable", "true", 9);
  s.status("remote_trigger_by", v);
  TEST_ASSERT_EQUAL_STRING("web:9", v.s.c_str());
  s.serial("remote_trigger_enable=false\n");
  s.ctl().mqttSubscriptions();
  s.ctl().mqttOnMessage("proptroller/fx/config/set/remote_trigger_enable", "true");
  s.status("remote_trigger_by", v);
  TEST_ASSERT_EQUAL_STRING("mqtt", v.s.c_str());
}

// ---------------------------------------------------------------- recover
void SM_PR1_E12_recover_opens_nothing_pulses_nothing() {
  FxSim s;
  fast(s, {"mqtt_broker=127.0.0.1", "cooldown_ms=0"});  // no cooldown, so the three recovers can follow each other
  s.ctl().mqttSubscriptions();
  for (int k = 0; k < 3; k++) {
    Millis t = s.now();
    size_t plays = s.world().df.plays.size();
    if (k == 0) s.press(Input::Trig2, 150);
    if (k == 1) { act(s, "recover", true); act(s, "recover", false); }
    if (k == 2) s.ctl().mqttOnMessage("proptroller/fx/control/recover", "");
    s.step(1500);
    TEST_ASSERT_EQUAL(0, when(s, OUT1, t, on));
    TEST_ASSERT_TRUE(s.changes(Channel::Cc1, t).empty() && s.changes(Channel::Cc2, t).empty());
    for (size_t i = plays; i < s.world().df.plays.size(); i++)
      TEST_ASSERT_TRUE_MESSAGE(s.world().df.plays[i].what != "/mp3/0001", "no run sound");
    TEST_ASSERT_TRUE(when(s, OUT2, t, on) > 0);
  }
  TEST_ASSERT_EQUAL(3, statusInt(s, "runs_left"));
}

void SM_PR2_recover_during_a_cycle_is_remembered_once() {
  FxSim s;
  fast(s, {"out2_on_ms=1000"});
  s.press(Input::Trig1, 150);
  s.step(50);
  s.serial("recover\n");
  s.serial("recover\n");
  TEST_ASSERT_TRUE(fx(s).queued_recover);
  s.step(5000);
  TEST_ASSERT_EQUAL_MESSAGE(2, statusInt(s, "run_count"), "the run and one recover");
}

// ---------------------------------------------------------------- sound
void SM_SND1_SND2_ambient_and_the_trigger_sound() {
  FxSim s;
  s.world().df.setCard({{DfModel::MP3, 2, 60000}, {DfModel::MP3, 1, 1000}});
  s.powerCycle(10);
  fast(s);
  TEST_ASSERT_EQUAL_STRING("/mp3/0002", s.world().df.plays.back().what.c_str());
  s.press(Input::Trig1, 150);
  s.step(100);
  TEST_ASSERT_EQUAL_STRING("/mp3/0001", s.world().df.plays.back().what.c_str());
  s.step(2000);
  TEST_ASSERT_EQUAL_STRING("/mp3/0002", s.world().df.plays.back().what.c_str());
  FxSim one;
  one.world().df.setCard({{DfModel::MP3, 1, 1000}});
  one.powerCycle(10);
  fast(one);
  TEST_ASSERT_TRUE_MESSAGE(one.world().df.plays.empty(), "one track: no ambient");
}

// ---------------------------------------------------------------- page lock
void SM_P1_P2_P3_P4_E16_page_lock() {
  FxSim s;
  fast(s);
  TEST_ASSERT_TRUE_MESSAGE(setp(s, "out1_on_ms", "300", 2).isOk(), "P1: no password asked by default");
  s.request(R"({"op":"set_params","values":[{"key":"pin","value":"ghost-1"},{"key":"page_lock","value":true}]})", 2);
  Response r = s.request(R"({"op":"set_params","pin":"wrong","values":[{"key":"out1_on_ms","value":400}]})", 2);
  TEST_ASSERT_EQUAL_STRING("pin", code(r).c_str());
  TEST_ASSERT_EQUAL_MESSAGE(300, s.ctl().settings().fx.out1_on_ms, "P2/E16: nothing saved");
  r = setp(s, "page_lock", "false", 2);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("pin", code(r).c_str(), "P3/E16: turning it off needs the password");
  r = s.request(R"({"op":"set_params","values":[{"key":"remote_trigger_enable","value":true}]})", 2);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("pin", code(r).c_str(), "P2: the start enable needs it");
  // P4: hashes only, never echoed or published
  std::string all = s.ctl().stateJson() + s.ctl().infoJson() + s.serial("settings\n") + s.serial("pin\n");
  s.ctl().mqttSubscriptions();
  for (auto& m : s.ctl().mqttOnConnect()) all += m.payload;
  r = s.request(R"({"op":"get_params"})", 2);
  all += r.json();
  TEST_ASSERT_TRUE(all.find("ghost-1") == std::string::npos);
  s.step(1000);  // the next try waits 1 s after a wrong password
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_params","pin":"ghost-1","values":[{"key":"page_lock","value":false}]})", 2).isOk());
}

// ---------------------------------------------------------------- MQTT addressing (more in test_rules)
void SM_N1_N2_N3_E15_addressing() {
  FxSim a, b;
  fast(a, {"name=fx1", "mqtt_broker=127.0.0.1"});
  fast(b, {"name=fx2", "mqtt_broker=127.0.0.1"});
  auto sa = a.ctl().mqttSubscriptions(), sb = b.ctl().mqttSubscriptions();
  TEST_ASSERT_EQUAL_STRING("proptroller/broadcast/getName", sa[0].c_str());
  for (auto& f : sa) TEST_ASSERT_TRUE(f == "proptroller/broadcast/getName" || f.compare(0, 16, "proptroller/fx1/") == 0);
  a.ctl().mqttOnMessage("proptroller/fx1/config/set/out1_on_ms", "500");
  b.ctl().mqttOnMessage("proptroller/fx1/config/set/out1_on_ms", "500");  // b sees it too (a wildcard client): ignored
  a.ctl().mqttOnMessage("proptroller/broadcast/config/set/out1_on_ms", "900");
  a.ctl().mqttOnMessage("proptroller/+/config/set/out1_on_ms", "900");
  TEST_ASSERT_EQUAL(500, a.ctl().settings().fx.out1_on_ms);
  TEST_ASSERT_EQUAL(200, b.ctl().settings().fx.out1_on_ms);
}

void SM_N4_retained_status_and_last_will() {
  FxSim s;
  fast(s, {"mqtt_broker=127.0.0.1"});
  s.ctl().mqttSubscriptions();
  MqttMsg will = s.ctl().mqttWill();
  TEST_ASSERT_TRUE(will.topic == "proptroller/fx/online" && will.payload == "false" && will.retain);
  bool retained = true;
  for (auto& m : s.ctl().mqttOnConnect())
    if (m.topic.find("/status/") != std::string::npos) retained &= m.retain;
  TEST_ASSERT_TRUE(retained);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(SM_A1_ready_needs_every_condition);
  RUN_TEST(SM_S1_out_of_range_refused_nothing_changes);
  RUN_TEST(SM_S2_stored_values_survive_and_read_back_everywhere);
  RUN_TEST(SM_B1_I9_restart_clears_everything);
  RUN_TEST(SM_B2_nothing_runs_or_recovers_in_the_boot_cooldown);
  RUN_TEST(SM_B3_E19_held_at_power_on_never_starts_a_run);
  RUN_TEST(SM_B4_E20_Q1_safety_hold_never_starts_a_run);
  RUN_TEST(SM_B5_restart_from_web_mqtt_and_serial_no_stop_command);
  RUN_TEST(SM_F1_I4_out1_exactly_its_time);
  RUN_TEST(SM_F2_recovery_time_is_out2_on_ms_read_when_out2_starts);
  RUN_TEST(SM_F3_I5_out1_and_out2_never_together_and_the_next_run_waits);
  RUN_TEST(SM_F4_E18_out3_run_during_the_background_recovery_waits_for_it);
  RUN_TEST(SM_F5_E11_every_source_pulses_and_sounds);
  RUN_TEST(SM_F6_status_firing_and_reloading);
  RUN_TEST(SM_L1_L2_E4_three_per_window_fourth_refused_with_time_left);
  RUN_TEST(SM_L3_one_per_window_is_a_single_cooldown);
  RUN_TEST(SM_L4_SM_PR3_Q2_recover_uses_no_run_and_adds_no_cooldown);
  RUN_TEST(recover_action_is_momentary_only_in_hold_mode);
  RUN_TEST(E25_Q3_recover_during_a_full_window);
  RUN_TEST(E26_press_right_after_a_recover_starts_a_run);
  RUN_TEST(SM_L5_full_window_ignores_and_cancels_the_queue);
  RUN_TEST(SM_L6_status_reports_runs_left_and_the_remaining_time);
  RUN_TEST(SM_L7_E14_lowering_n_mid_window_makes_it_full);
  RUN_TEST(SM_L8_E13_restart_mid_window_with_a_queued_run);
  RUN_TEST(E17_cooldown_zero_means_no_window);
  RUN_TEST(SM_T1_Q4_release_time_and_confirm_time);
  RUN_TEST(SM_T1a_E22_one_run_exactly_the_confirm_time_after_the_closure);
  RUN_TEST(E21_short_closures_and_chatter_never_start_a_run);
  RUN_TEST(E23_one_open_sample_restarts_the_timer);
  RUN_TEST(E24_losing_ready_restarts_the_timer);
  RUN_TEST(SM_T2_E1_taps_during_a_recovery_do_not_start_a_run);
  RUN_TEST(SM_T3_E2_held_during_the_recovery_one_run);
  RUN_TEST(SM_T4_local_run_needs_all_three);
  RUN_TEST(SM_T5_E3_held_since_the_last_run);
  RUN_TEST(SM_T6_a_remote_run_clears_the_release);
  RUN_TEST(SM_T7_E5_E6_full_window_releases_do_not_count);
  RUN_TEST(SM_Q1_Q2_E7_a_web_press_during_a_run_queues_one);
  RUN_TEST(SM_R3_usb_json_lines_are_serial);
  RUN_TEST(SM_Q3_queued_starts_when_ready_without_the_trigger_gate);
  RUN_TEST(SM_Q4_E10_remote_trigger_needs_the_enable);
  RUN_TEST(SM_Q5_E8_cancel_and_the_other_cancellations);
  RUN_TEST(SM_Q6_mqtt_and_serial_trigger_like_a_web_press);
  RUN_TEST(SM_R1_enable_false_at_power_on_never_stored);
  RUN_TEST(SM_R2_E9_web_enable_ends_with_the_page);
  RUN_TEST(SM_R3_mqtt_or_serial_enable_stays);
  RUN_TEST(SM_R4_status_says_who_enabled);
  RUN_TEST(SM_PR1_E12_recover_opens_nothing_pulses_nothing);
  RUN_TEST(SM_PR2_recover_during_a_cycle_is_remembered_once);
  RUN_TEST(SM_SND1_SND2_ambient_and_the_trigger_sound);
  RUN_TEST(SM_P1_P2_P3_P4_E16_page_lock);
  RUN_TEST(SM_N1_N2_N3_E15_addressing);
  RUN_TEST(SM_N4_retained_status_and_last_will);
  return UNITY_END();
}
