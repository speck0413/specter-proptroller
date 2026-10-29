// Run counters (runs.h): runs since power-on and lifetime runs, counted the same way for every source (a trigger
// input, the web page / MQTT / serial, a show script's effect, the pulse-and-recover effect), the lifetime count
// kept across a restart, a firmware update and a backup restore, and written to flash only as often as the
// throttle allows.
#include "../effect_common.h"
#include "proptroller/runs.h"
#include "proptroller/password.h"

void setUp() {}
void tearDown() {}

static int64_t stateInt(Sim& s, const char* key) {
  JsonDocument d;
  deserializeJson(d, s.stateJson());
  return d[key].isNull() ? -1 : d[key].as<int64_t>();
}
static int64_t tonight(Sim& s) { return stateInt(s, "runs_since_power_on"); }
static int64_t lifetime(Sim& s) { return stateInt(s, "runs_lifetime"); }
// the "run" events' data since the board started
static std::vector<std::string> runEvents(Sim& s) {
  std::vector<std::string> evs, out;
  s.ctl().eventsSince(0, evs);
  for (auto& e : evs)
    if (e.find("\"event\":\"run\"") != std::string::npos) out.push_back(e);
  return out;
}
// a status value as MQTT status/get/<key> answers it ("" when refused)
static std::string statusGet(Sim& s, const std::string& key) {
  (void)s.mqttSubscriptions();
  std::string p = s.ctl().mqttPrefix();
  auto r = s.mqttOnMessage(p + "/status/get/" + key, "");
  return r.size() == 1 && r[0].topic == p + "/status/" + key ? r[0].payload : std::string();
}
static bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
// every event since the board started (the log lines among them)
static std::string logs(Sim& s) {
  std::vector<std::string> evs;
  s.ctl().eventsSince(0, evs);
  std::string all;
  for (auto& e : evs) all += e + "\n";
  return all;
}

// a board with the example show (TRIG1 pulses OUT1 for 2 s, then a 3 s cooldown), past its boot lockout
struct ShowSim : Sim {
  ShowSim() : Sim("runs") {
    addExample();
    step(3000);
  }
};

// ---------------------------------------------------------------- what counts

void a_show_run_counts_from_an_input_and_from_a_request() {
  ShowSim s;
  TEST_ASSERT_EQUAL(0, tonight(s));
  TEST_ASSERT_EQUAL(-1, stateInt(s, "last_run_uptime_ms"));  // no run yet: not in the state
  s.press(Input::Trig1, 200);
  s.step(20);
  TEST_ASSERT_EQUAL(1, tonight(s));
  TEST_ASSERT_EQUAL(1, lifetime(s));
  int64_t at = stateInt(s, "last_run_uptime_ms");
  TEST_ASSERT_TRUE(at > 3000 && at <= (int64_t)s.uptime());
  s.step(6000);  // past the run and its cooldown
  TEST_ASSERT_TRUE(s.request(R"({"op":"run","show":1})").isOk());
  s.step(20);
  TEST_ASSERT_EQUAL(2, tonight(s));
  auto ev = runEvents(s);
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_TRUE(has(ev[0], "\"source\":\"input\"") && has(ev[0], "\"kind\":\"show\"") && has(ev[0], "\"detail\":\"TRIG1\""));
  TEST_ASSERT_TRUE(has(ev[1], "\"source\":\"api\"") && has(ev[1], "\"runs_since_power_on\":2") && has(ev[1], "\"runs_lifetime\":2"));
}

void an_ignored_trigger_a_restart_and_a_loop_are_not_runs() {
  ShowSim s;
  s.press(Input::Trig1, 200);
  s.step(500);
  s.press(Input::Trig1, 200);  // while it runs: ignored
  s.step(6000);
  s.press(Input::Trig1, 200);  // cooldown over: a run
  s.step(500);
  TEST_ASSERT_EQUAL(2, tonight(s));
  // a looping show that restarts on a trigger: neither a loop nor a restart is a fresh start
  Sim l("loop");
  Response r = l.request(R"({"op":"put_show","new":true,"data":{"id":2,"name":"Loop","looping":true,"retrigger":"restart",
    "triggers":[{"on":"input","input":"USER1"}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"pulse","ms":300}]}]}})");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  l.step(3000);
  l.press(Input::User1, 200);
  l.step(2000);  // several loops
  l.press(Input::User1, 200);  // a restart
  l.step(200);
  TEST_ASSERT_EQUAL(1, tonight(l));
}

void the_effect_counts_local_and_remote_runs_but_not_a_recover() {
  FxSim s;
  s.step(3000);
  s.press(Input::Trig1, TAP);
  s.step(20);
  TEST_ASSERT_EQUAL(1, tonight(s));
  TEST_ASSERT_EQUAL(1, statusInt(s, "run_count"));
  s.step(10000);  // the cycle is over
  TEST_ASSERT_TRUE(act(s, "recover", true).isOk());
  s.step(20);
  TEST_ASSERT_EQUAL(2, statusInt(s, "run_count"));  // run_count keeps its meaning: recover cycles count (SM-PR3)
  TEST_ASSERT_EQUAL(1, tonight(s));                 // a recover is not a run
  s.step(10000);
  TEST_ASSERT_TRUE(setp(s, "remote_trigger_enable", "true", 1).isOk());  // from a phone page (SM-R2)
  TEST_ASSERT_TRUE(act(s, "trigger", true, 1).isOk());
  s.step(20);
  TEST_ASSERT_EQUAL(2, tonight(s));
  auto ev = runEvents(s);
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_TRUE(has(ev[0], "\"kind\":\"effect\"") && has(ev[0], "\"source\":\"input\""));
  TEST_ASSERT_TRUE(has(ev[1], "\"kind\":\"effect\"") && has(ev[1], "\"source\":\"api\""));
  // the counters are status values too (MQTT status/<key>, the console's status)
  TEST_ASSERT_EQUAL_STRING("2", statusGet(s, "runs_since_power_on").c_str());
  TEST_ASSERT_TRUE(has(s.serial("status"), "runs_lifetime=2"));
}

void a_show_script_effect_counts_each_start() {
  Sim s("script");
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = "effect porch light:\n  wait for trig1\n  out1 = on\n  sleep 0.5\n  out1 = off\n";
  std::string body;
  serializeJson(d, body);
  Response r = s.request(body);
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  s.step(3000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  TEST_ASSERT_EQUAL(2, tonight(s));
  auto ev = runEvents(s);
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_TRUE(has(ev[0], "\"kind\":\"script\"") && has(ev[0], "\"name\":\"porch light\"") && has(ev[0], "\"detail\":\"TRIG1\""));
}

// PROTOCOL.md "run" events: a start from the web page, MQTT, POST /rpc or the serial port is `api`, as for a show
// and the pulse-and-recover effect; `detail` stays the input of the script effect's first `wait for`.
void a_show_script_effect_started_from_the_page_or_mqtt_is_an_api_run() {
  Sim s("script");
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = "effect porch:\n  wait for trig1\n  out1 = on\n  sleep 0.5\n  out1 = off\n";
  std::string body;
  serializeJson(d, body);
  Response r = s.request(body);
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  r = setp(s, "remote_free_effects", "\"porch\"");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  s.step(3000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  r = act(s, "porch", true);
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  s.step(1000);
  (void)s.mqttSubscriptions();
  (void)s.mqttOnMessage(s.ctl().mqttPrefix() + "/control/porch", "");
  s.step(1000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  TEST_ASSERT_EQUAL(4, tonight(s));
  auto ev = runEvents(s);
  TEST_ASSERT_EQUAL(4, ev.size());
  const char* want[] = {"\"source\":\"input\"", "\"source\":\"api\"", "\"source\":\"api\"", "\"source\":\"input\""};
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_TRUE_MESSAGE(has(ev[i], want[i]), ev[i].c_str());
    TEST_ASSERT_TRUE_MESSAGE(has(ev[i], "\"kind\":\"script\"") && has(ev[i], "\"detail\":\"TRIG1\""), ev[i].c_str());
  }
}

// STOP, pause and a changed input type put each script effect back to its first line (Controller::scriptReload):
// every run is still counted once, with its own source, before and after it. A board with one script effect
// (TRIG1: OUT1 for 0.5 s) that the web page may start, past its boot lockout.
struct PorchSim : Sim {
  PorchSim() : Sim("script") {
    JsonDocument d;
    d["op"] = "set_script";
    d["text"] = "effect porch:\n  wait for trig1\n  out1 = on\n  sleep 0.5\n  out1 = off\n";
    std::string body;
    serializeJson(d, body);
    Response r = request(body);
    TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
    r = setp(*this, "remote_free_effects", "\"porch\"");
    TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
    step(3000);
  }
  void stopAll() { TEST_ASSERT_TRUE(request(R"({"op":"stop_all"})").isOk()); }
  void pause(bool on) { TEST_ASSERT_TRUE(request(on ? R"({"op":"pause","on":true})" : R"({"op":"pause","on":false})").isOk()); }
  void webStart() {
    Response r = act(*this, "porch", true);
    TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  }
  // a web start while the effect runs: taken, and queued (not started)
  void webStartQueued() {
    const char* queued = "remote start queued: effect \\\"porch\\\"";
    TEST_ASSERT_FALSE(has(logs(*this), queued));
    webStart();
    std::string l = logs(*this);
    TEST_ASSERT_TRUE_MESSAGE(has(l, queued), l.c_str());
  }
};
// the run events' sources, in order, and the counters
static void expectRuns(Sim& s, const std::vector<const char*>& sources) {
  auto ev = runEvents(s);
  std::string all;
  for (auto& e : ev) all += e + "\n";
  TEST_ASSERT_EQUAL_MESSAGE(sources.size(), ev.size(), all.c_str());
  for (size_t i = 0; i < sources.size(); i++) {
    std::string want = std::string("\"source\":\"") + sources[i] + "\"";
    TEST_ASSERT_TRUE_MESSAGE(has(ev[i], want.c_str()) && has(ev[i], "\"name\":\"porch\""), ev[i].c_str());
    std::string n = "\"runs_since_power_on\":" + std::to_string(i + 1);
    TEST_ASSERT_TRUE_MESSAGE(has(ev[i], n.c_str()), ev[i].c_str());
  }
  TEST_ASSERT_EQUAL(sources.size(), tonight(s));
  TEST_ASSERT_EQUAL(sources.size(), lifetime(s));
}

void a_script_effect_counts_its_runs_after_stop_all() {
  PorchSim s;
  s.press(Input::Trig1, 300);
  s.step(1000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  s.stopAll();
  s.step(100);
  s.press(Input::Trig1, 300);
  s.step(1000);
  expectRuns(s, {"input", "input", "input"});
}

void a_press_after_stop_all_is_an_input_run_and_a_web_start_an_api_run() {
  PorchSim s;
  s.press(Input::Trig1, 300);
  s.step(1000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  s.stopAll();
  s.step(100);
  s.webStart();
  s.step(1000);
  s.webStart();
  s.step(1000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  expectRuns(s, {"input", "input", "api", "api", "input"});
}

void a_press_after_a_pause_is_an_input_run_and_a_web_start_an_api_run() {
  PorchSim s;
  s.press(Input::Trig1, 300);
  s.step(1000);
  s.pause(true);
  s.step(100);
  s.pause(false);
  s.step(100);
  s.webStart();
  s.step(1000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  expectRuns(s, {"input", "api", "input"});
}

void a_script_effect_counts_its_runs_after_an_input_type_change() {
  PorchSim s;
  s.press(Input::Trig1, 300);
  s.step(1000);
  TEST_ASSERT_TRUE(setp(s, "trig1_input", "\"sensor_high\"").isOk());  // each change: back to the first line
  TEST_ASSERT_TRUE(setp(s, "trig1_input", "\"switch\"").isOk());
  s.step(1000);
  s.press(Input::Trig1, 300);
  s.step(1000);
  expectRuns(s, {"input", "input"});
}

void a_cancelled_queued_start_is_not_a_run() {
  PorchSim s;
  s.press(Input::Trig1, 300);
  s.step(100);  // the run is in its sleep: a web start waits for it
  s.webStartQueued();
  TEST_ASSERT_TRUE(act(s, "cancel", true).isOk());
  s.step(1500);
  s.press(Input::Trig1, 300);
  s.step(1000);
  expectRuns(s, {"input", "input"});
}

void a_queued_start_that_stop_all_drops_is_not_a_run() {
  PorchSim s;
  s.press(Input::Trig1, 300);
  s.step(100);
  s.webStartQueued();
  s.stopAll();
  s.step(1500);
  s.press(Input::Trig1, 300);
  s.step(1000);
  expectRuns(s, {"input", "input"});
}

void a_queued_start_between_two_presses_is_an_api_run() {
  PorchSim s;
  s.press(Input::Trig1, 300);
  s.step(100);
  s.webStartQueued();  // queued: it starts when the press's run ends
  s.step(1500);
  s.press(Input::Trig1, 300);
  s.step(1000);
  expectRuns(s, {"input", "api", "input"});
}

// a start taken on a 1 ms sample is reported by the next 10 ms step: STOP before that step still counts it
void a_run_started_on_a_sample_just_before_stop_all_is_counted() {
  PorchSim s;
  while (s.uptime() % Sim::TICK_MS != 0) s.tick1();
  s.webStart();
  s.tick1();  // a sample, not a step: the effect starts
  s.stopAll();
  s.step(1000);
  expectRuns(s, {"api"});
}

// ... and so does a script save or a backup restore, which load the runner again: the start is counted once
static std::string porchBackup() {
  PorchSim from;
  Response b = from.request(R"({"op":"get_backup"})");
  TEST_ASSERT_TRUE_MESSAGE(b.isOk(), b.json().c_str());
  JsonDocument p;
  p["op"] = "put_backup";
  p["data"] = b.doc()["result"]["data"];
  std::string body;
  serializeJson(p, body);
  return body;
}
static void runOnASampleThen(const std::string& body) {
  PorchSim s;
  while (s.uptime() % Sim::TICK_MS != 0) s.tick1();
  s.webStart();
  s.tick1();  // a sample, not a step: the effect starts
  Response r = s.request(body);
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  s.step(1000);
  expectRuns(s, {"api"});
}
void a_run_started_on_a_sample_just_before_set_script_is_counted() {
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = "effect porch:\n  wait for trig1\n  out1 = on\n  sleep 0.5\n  out1 = off\n";
  std::string body;
  serializeJson(d, body);
  runOnASampleThen(body);
}
void a_run_started_on_a_sample_just_before_put_backup_is_counted() { runOnASampleThen(porchBackup()); }

// ---------------------------------------------------------------- persistence and the throttle

void the_lifetime_count_survives_a_power_cycle_and_tonight_starts_at_0() {
  ShowSim s;
  for (int i = 0; i < 3; i++) {
    s.press(Input::Trig1, 200);
    s.step(6000);
  }
  s.step(RUNS_SAVE_PERIOD_MS);  // quiet: written
  TEST_ASSERT_TRUE(s.runsFlash().saves >= 1);
  s.powerCycle();
  s.step(100);
  TEST_ASSERT_EQUAL(0, tonight(s));
  TEST_ASSERT_EQUAL(3, lifetime(s));
  s.step(3000);
  s.press(Input::Trig1, 200);
  s.step(20);
  TEST_ASSERT_EQUAL(1, tonight(s));
  TEST_ASSERT_EQUAL(4, lifetime(s));
}

void the_write_waits_for_the_period_or_ten_runs_and_a_quiet_board() {
  ShowSim s;
  s.press(Input::Trig1, 200);
  s.step(6000);
  TEST_ASSERT_EQUAL(0, s.runsFlash().saves);  // one run: not yet
  s.step(RUNS_SAVE_PERIOD_MS - 6000 + 100);
  TEST_ASSERT_EQUAL(1, s.runsFlash().saves);  // 60 s after it
  // ten runs: written at the first quiet moment, well before 60 s
  Sim f("fast");
  Response r = f.request(R"({"op":"put_show","new":true,"data":{"id":2,"name":"Quick","triggers":[{"on":"input","input":"USER1"}],
    "tracks":[{"target":"OUT2","cues":[{"at":0,"do":"pulse","ms":100}]}]}})");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  f.step(3000);
  for (uint32_t i = 0; i < RUNS_SAVE_EVERY; i++) {
    f.press(Input::User1, 100);
    f.step(400);
  }
  TEST_ASSERT_EQUAL((int)RUNS_SAVE_EVERY, tonight(f));
  f.step(200);
  TEST_ASSERT_EQUAL(1, f.runsFlash().saves);
  // and never more often than RUNS_SAVE_MIN_GAP_MS
  for (uint32_t i = 0; i < RUNS_SAVE_EVERY; i++) {
    f.press(Input::User1, 100);
    f.step(300);
  }
  TEST_ASSERT_EQUAL(1, f.runsFlash().saves);
  f.step(RUNS_SAVE_MIN_GAP_MS);
  TEST_ASSERT_EQUAL(2, f.runsFlash().saves);
}

void nothing_is_written_while_a_show_runs() {
  Sim s("busy");
  Response r = s.request(R"({"op":"put_show","new":true,"data":{"id":2,"name":"Loop","looping":true,
    "triggers":[{"on":"input","input":"USER1"}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"pulse","ms":300}]}]}})");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  s.step(3000);
  s.press(Input::User1, 200);
  s.step(RUNS_SAVE_PERIOD_MS * 10);
  TEST_ASSERT_EQUAL(0, s.runsFlash().saves);  // the show never stops: no flash stall in it, however long
  TEST_ASSERT_TRUE(s.request(R"({"op":"stop_all"})").isOk());
  s.step(100);
  TEST_ASSERT_EQUAL(1, s.runsFlash().saves);  // written at the first quiet moment
}

void a_sudden_power_cut_loses_only_the_runs_not_yet_written() {
  ShowSim s;
  s.press(Input::Trig1, 200);
  s.step(6000 + RUNS_SAVE_PERIOD_MS);  // written: 1
  s.press(Input::Trig1, 200);
  s.step(6000);
  s.press(Input::Trig1, 200);
  s.step(1000);
  TEST_ASSERT_EQUAL(3, lifetime(s));
  s.powerCycle();  // the power goes: the two runs since the write are lost
  s.step(100);
  TEST_ASSERT_EQUAL(1, lifetime(s));
}

// The lifetime count is written with the lock given back (a window): the step keeps running and can count runs
// meanwhile. The record holds the count from before the window; a run counted during it waits for the next write,
// on the same throttle as any run (no extra write). The window is the run store's slow flash: a press held while
// it writes, the step running on (Sim::press also runs the loop's background work, which has nothing to write here).
static Sim* g_runs_sim = nullptr;
static int g_runs_windows = 0;
static std::string g_runs_record;
static int runsRelease(void*) { return 1; }  // the window opens (Controller::Unlocked); nothing else is held
static void runsRetake(void*, int) {}
static void runsSlowSave() {
  if (g_runs_windows++) return;
  g_runs_sim->press(Input::Trig1, 300);  // a show run, started and counted by the step during the write
}
void a_run_counted_while_the_count_is_written_is_written_later() {
  ShowSim s;
  s.press(Input::Trig1, 200);
  s.step(6000);  // the show and its cooldown end: quiet
  g_runs_sim = &s;
  g_runs_windows = 0;
  s.ctl().setLockHooks(runsRelease, runsRetake, nullptr);
  s.runsFlash().save_hook = runsSlowSave;
  for (Millis t = 0; t < 2 * RUNS_SAVE_PERIOD_MS && s.runsFlash().saves == 0; t += 1000) s.step(1000);
  s.runsFlash().save_hook = nullptr;
  s.ctl().setLockHooks(nullptr, nullptr, nullptr);
  g_runs_sim = nullptr;
  TEST_ASSERT_EQUAL(1, s.runsFlash().saves);
  TEST_ASSERT_EQUAL(1, g_runs_windows);
  TEST_ASSERT_EQUAL(2, lifetime(s));  // the record written holds 1
  s.step(6000 + RUNS_SAVE_MIN_GAP_MS);  // quiet and past the minimum gap: the run waits for its own period
  TEST_ASSERT_EQUAL(1, s.runsFlash().saves);
  s.step(RUNS_SAVE_PERIOD_MS);
  TEST_ASSERT_EQUAL(2, s.runsFlash().saves);
  s.powerCycle();
  s.step(100);
  TEST_ASSERT_EQUAL(2, lifetime(s));
}

void a_clean_restart_writes_at_once() {
  ShowSim s;
  s.press(Input::Trig1, 200);
  s.step(500);
  TEST_ASSERT_EQUAL(0, s.runsFlash().saves);
  TEST_ASSERT_TRUE(s.request(R"({"op":"reboot"})").isOk());
  s.step(500);  // restarted
  TEST_ASSERT_EQUAL(1, lifetime(s));
  TEST_ASSERT_EQUAL(0, tonight(s));
  // the Restart control (web, MQTT, console) too
  s.step(3000);
  s.press(Input::Trig1, 200);
  s.step(500);
  s.serial("restart");
  s.step(500);
  TEST_ASSERT_EQUAL(2, lifetime(s));
}

void a_backup_restore_and_a_factory_reset_keep_the_lifetime_count() {
  ShowSim s;
  s.press(Input::Trig1, 200);
  s.step(6000 + RUNS_SAVE_PERIOD_MS);
  Response b = s.request(R"({"op":"get_backup"})");
  TEST_ASSERT_TRUE(b.isOk());
  TEST_ASSERT_FALSE(has(b.json(), "runs_lifetime"));  // a backup is the board's setup, not its odometer
  JsonDocument d;
  d["op"] = "put_backup";
  d["data"] = b.doc()["result"]["data"];
  std::string body;
  serializeJson(d, body);
  Response p = s.request(body);
  TEST_ASSERT_TRUE_MESSAGE(p.isOk(), p.json().c_str());
  s.step(500);
  TEST_ASSERT_EQUAL(1, lifetime(s));
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset"})").isOk());
  s.step(500);
  TEST_ASSERT_EQUAL(1, lifetime(s));
}

void a_factory_reset_and_a_firmware_update_write_unsaved_runs_first() {
  ShowSim s;
  s.press(Input::Trig1, 200);
  s.step(500);
  TEST_ASSERT_EQUAL(0, s.runsFlash().saves);
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset"})").isOk());
  s.step(500);
  TEST_ASSERT_EQUAL(1, lifetime(s));
  // a firmware update: the count is written before the board restarts into the new image
  s.addExample();
  s.step(3000);
  s.press(Input::Trig1, 200);
  s.step(6000);
  int saves = s.runsFlash().saves;
  std::string img = sim_image::build("0.3.0", "abc1234", true, 50 * 1024);
  uint8_t d[32];
  sha256((const uint8_t*)img.data(), img.size(), d);
  UpdateRequest q;
  q.has_len = true;
  q.len = img.size();
  q.octet_stream = true;
  q.has_sha256 = true;
  q.sha256 = toHex(d, 32);
  std::string a;
  TEST_ASSERT_TRUE_MESSAGE(s.ctl().updateBegin(q, a), a.c_str());
  TEST_ASSERT_TRUE_MESSAGE(s.ctl().updateWrite((const uint8_t*)img.data(), img.size(), a), a.c_str());
  std::string out = s.ctl().updateFinish();
  TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.c_str());
  uint32_t boots = s.boots;
  s.step(3000);  // the restart delay, then the new image
  TEST_ASSERT_TRUE(s.boots > boots);
  TEST_ASSERT_EQUAL(saves + 1, s.runsFlash().saves);
  TEST_ASSERT_EQUAL(2, lifetime(s));
}

void reset_runs_sets_the_lifetime_count_to_0_behind_the_page_lock() {
  ShowSim s;
  s.press(Input::Trig1, 200);
  s.step(500);
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_params","values":[{"key":"pin","value":"123456"},{"key":"page_lock","value":true}]})").isOk());
  Response r = s.request(R"({"op":"reset_runs"})");
  TEST_ASSERT_EQUAL_STRING("pin", code(r).c_str());
  TEST_ASSERT_EQUAL(1, lifetime(s));
  TEST_ASSERT_TRUE(s.request(R"({"op":"reset_runs","pin":"123456"})").isOk());
  TEST_ASSERT_EQUAL(0, lifetime(s));
  TEST_ASSERT_EQUAL(1, tonight(s));  // tonight's count stays
  s.step(100);
  TEST_ASSERT_EQUAL(1, s.runsFlash().saves);  // written soon, whatever the throttle
  s.powerCycle();
  s.step(100);
  TEST_ASSERT_EQUAL(0, lifetime(s));
}

// ---------------------------------------------------------------- MQTT

void mqtt_publishes_a_run_event_and_the_counters() {
  ShowSim s;
  (void)s.mqttSubscriptions();
  const std::string p = s.ctl().mqttPrefix();
  auto first = s.mqttOnConnect();
  bool status_at_connect = false;
  for (auto& m : first) status_at_connect |= m.topic == p + "/status/runs_lifetime" && m.retain;
  TEST_ASSERT_TRUE(status_at_connect);
  (void)s.mqttPoll();
  s.press(Input::Trig1, 200);
  s.step(20);
  bool run = false, counter = false;
  for (auto& m : s.mqttPoll()) {
    if (m.topic == p + "/event/run") {
      run = true;
      TEST_ASSERT_TRUE(has(m.payload, "\"source\":\"input\"") && has(m.payload, "\"runs_since_power_on\":1"));
      TEST_ASSERT_EQUAL('{', m.payload.front());
      TEST_ASSERT_EQUAL('}', m.payload.back());
    }
    if (m.topic == p + "/status/runs_since_power_on" && m.payload == "1") counter = true;
  }
  TEST_ASSERT_TRUE(run);
  TEST_ASSERT_TRUE(counter);
  // a profile can give the counters a project's names (status aliases)
  Response pr = s.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"shows_tonight","status":"runs_since_power_on"}]}})");
  TEST_ASSERT_TRUE_MESSAGE(pr.isOk(), pr.json().c_str());
  TEST_ASSERT_EQUAL_STRING("1", statusGet(s, "shows_tonight").c_str());
  // with the effect off as well
  TEST_ASSERT_EQUAL_STRING("1", statusGet(s, "runs_lifetime").c_str());
}

// ---------------------------------------------------------------- the counter on its own

void the_throttle_on_its_own() {
  RunCounter c;
  TEST_ASSERT_FALSE(c.saveDue(0, true));
  c.count(1000);
  TEST_ASSERT_FALSE(c.saveDue(1000 + RUNS_SAVE_PERIOD_MS - 1, true));
  TEST_ASSERT_TRUE(c.saveDue(1000 + RUNS_SAVE_PERIOD_MS, true));
  TEST_ASSERT_FALSE(c.saveDue(1000 + RUNS_SAVE_PERIOD_MS, false));  // busy
  TEST_ASSERT_FALSE(c.saveDue(1000 + 100 * RUNS_SAVE_PERIOD_MS, false));  // busy: never, however long
  c.saved(61000, false, 1);  // a failed write: tried again later
  TEST_ASSERT_FALSE(c.saveDue(61000 + RUNS_SAVE_RETRY_MS - 1, true));
  TEST_ASSERT_TRUE(c.saveDue(61000 + RUNS_SAVE_RETRY_MS, true));
  c.saved(400000, true, 1);
  TEST_ASSERT_EQUAL(0, c.unsaved());
  c.count(400001);
  c.flushSoon();
  TEST_ASSERT_TRUE(c.saveDue(400002, false));  // a clean restart: whatever runs
  uint64_t n = 0;
  TEST_ASSERT_TRUE(RunCounter::parse(c.record(), n));
  TEST_ASSERT_EQUAL(2, (int)n);
  TEST_ASSERT_FALSE(RunCounter::parse("{}", n));
  // a run counted while the record is written stays waiting, its period from the write
  RunCounter w;
  w.count(1000);
  const uint32_t written = w.unsaved();
  w.count(1500);
  w.saved(2000, true, written);
  TEST_ASSERT_EQUAL(1, w.unsaved());
  TEST_ASSERT_FALSE(w.saveDue(2000 + RUNS_SAVE_PERIOD_MS - 1, true));
  TEST_ASSERT_TRUE(w.saveDue(2000 + RUNS_SAVE_PERIOD_MS, true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(a_show_run_counts_from_an_input_and_from_a_request);
  RUN_TEST(an_ignored_trigger_a_restart_and_a_loop_are_not_runs);
  RUN_TEST(the_effect_counts_local_and_remote_runs_but_not_a_recover);
  RUN_TEST(a_show_script_effect_counts_each_start);
  RUN_TEST(a_show_script_effect_started_from_the_page_or_mqtt_is_an_api_run);
  RUN_TEST(a_script_effect_counts_its_runs_after_stop_all);
  RUN_TEST(a_press_after_stop_all_is_an_input_run_and_a_web_start_an_api_run);
  RUN_TEST(a_press_after_a_pause_is_an_input_run_and_a_web_start_an_api_run);
  RUN_TEST(a_script_effect_counts_its_runs_after_an_input_type_change);
  RUN_TEST(a_cancelled_queued_start_is_not_a_run);
  RUN_TEST(a_queued_start_that_stop_all_drops_is_not_a_run);
  RUN_TEST(a_queued_start_between_two_presses_is_an_api_run);
  RUN_TEST(a_run_started_on_a_sample_just_before_stop_all_is_counted);
  RUN_TEST(a_run_started_on_a_sample_just_before_set_script_is_counted);
  RUN_TEST(a_run_started_on_a_sample_just_before_put_backup_is_counted);
  RUN_TEST(the_lifetime_count_survives_a_power_cycle_and_tonight_starts_at_0);
  RUN_TEST(the_write_waits_for_the_period_or_ten_runs_and_a_quiet_board);
  RUN_TEST(nothing_is_written_while_a_show_runs);
  RUN_TEST(a_sudden_power_cut_loses_only_the_runs_not_yet_written);
  RUN_TEST(a_run_counted_while_the_count_is_written_is_written_later);
  RUN_TEST(a_clean_restart_writes_at_once);
  RUN_TEST(a_backup_restore_and_a_factory_reset_keep_the_lifetime_count);
  RUN_TEST(a_factory_reset_and_a_firmware_update_write_unsaved_runs_first);
  RUN_TEST(reset_runs_sets_the_lifetime_count_to_0_behind_the_page_lock);
  RUN_TEST(mqtt_publishes_a_run_event_and_the_counters);
  RUN_TEST(the_throttle_on_its_own);
  return UNITY_END();
}
