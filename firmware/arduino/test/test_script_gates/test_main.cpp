// Show scripts: the lines after the beginner ones (firmware/SHOW-SCRIPT.md): sound lines before the first wait for,
// "limit", "pulse", settings named for times, the start rules (trig1_wait / trig2_wait), effects that take turns
// by the outputs they use, remote starts by name, and the background loop. The checker's messages, and runs on a
// simulated board against the rules the pulse-and-recover effect follows (firmware/STATE-MACHINE.md SM-T, SM-L,
// SM-Q, SM-R, SM-PR, SM-F6), with generic names only.
#include <unity.h>
#include <memory>
#include <random>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static ScriptBoard board() {
  ScriptBoard b;
  b.buttons_are_triggers = true;
  b.roles[2] = b.roles[3] = 0;  // CC1 / CC2 run shows
  return b;
}
static ScriptCheck check(const std::string& text, const ScriptBoard& b = board()) {
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  return scriptCompile(text, SoundList(), b, *p);
}
static std::string firstError(const std::string& text, const ScriptBoard& b = board()) {
  ScriptCheck c = check(text, b);
  return c.errors.empty() ? std::string("(ok)") : c.errors[0].text;
}
static std::string tableOf(const std::string& text, const ScriptBoard& b = board()) {
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  ScriptCheck c = scriptCompile(text, SoundList(), b, *p);
  JsonDocument d;
  scriptCheckJson(c, p.get(), d.to<JsonObject>());
  std::string j;
  if (d["table"].isNull()) serializeJson(d, j);
  else serializeJson(d["table"], j);
  return j;
}

// A run on a press (the "ready" rule), a limit, a refill that shares OUT2 (the "return" rule: remembered while
// the run holds OUT2), pulses: the shape the runtime tests use.
static const char* const PULSER =
    "effect pulser:\n"
    "  wait for trig1\n"
    "  limit 3 runs per 60\n"
    "  pulse cc1 for 200 ms\n"
    "  out1 = on\n"
    "  sleep 1\n"
    "  out1 = off\n"
    "  sleep 10 ms\n"
    "  out2 = on\n"
    "  sleep 2\n"
    "  out2 = off\n"
    "effect refill:\n"
    "  wait for trig2\n"
    "  sleep 1\n"
    "  out2 = on\n"
    "  sleep 2\n"
    "  out2 = off\n";

// ------------------------------------------------------------------ the checker

void the_lines_check_clean_and_give_their_table() {
  ScriptCheck c = check(PULSER);
  TEST_ASSERT_TRUE_MESSAGE(c.ok(), c.errors.empty() ? "" : c.errors[0].text.c_str());
  TEST_ASSERT_EQUAL(0, c.warnings.size());
  std::string t = tableOf(PULSER);
  TEST_ASSERT_TRUE_MESSAGE(t.find(R"({"name":"pulser","line":1,"limit":{"runs":3,"ms":60000},"steps":[{"line":2,"op":"wait_for","input":"TRIG1"},{"line":4,"op":"pulse","target":"CC1","ms":200})") != std::string::npos, t.c_str());
  // sound lines before the first wait for, and the loop turned off three ways
  t = tableOf("effect a:\n  audio_loop = 3\n  wait for trig1\n  audio_loop = off\n  audio_loop = \"\"\n  audio_loop = FALSE\n");
  TEST_ASSERT_TRUE_MESSAGE(t.find(R"({"line":2,"op":"audio_loop","sound":3},{"line":3,"op":"wait_for","input":"TRIG1"},{"line":4,"op":"audio_loop_off"},{"line":5,"op":"audio_loop_off"},{"line":6,"op":"audio_loop_off"})") != std::string::npos, t.c_str());
  TEST_ASSERT_EQUAL_STRING("line 1: effect \"a\" must start with \"wait for\", for example \"wait for trig1\".",
                           firstError("effect a:\n  audio_single = 1\n  out1 = on\n  wait for trig1\n").c_str());
}

void settings_name_times_and_counts() {
  std::string t = tableOf("effect a:\n wait for trig1\n limit runs_per_cooldown runs per cooldown_ms\n out1 = on\n sleep out1_on_ms\n out1 = off\n pulse cc1 for trigger_out_pulse_ms\n");
  TEST_ASSERT_TRUE_MESSAGE(t.find(R"("limit":{"runs":"runs_per_cooldown","ms":"cooldown_ms"})") != std::string::npos, t.c_str());
  TEST_ASSERT_TRUE_MESSAGE(t.find(R"({"line":5,"op":"sleep","setting":"out1_on_ms"})") != std::string::npos, t.c_str());
  TEST_ASSERT_TRUE_MESSAGE(t.find(R"({"line":7,"op":"pulse","target":"CC1","setting":"trigger_out_pulse_ms"})") != std::string::npos, t.c_str());
  ScriptBoard b = board();
  b.setting_aliases.push_back({"valve_time", "out1_on_ms"});
  t = tableOf("effect a:\n wait for trig1\n out1 = on\n sleep Valve_Time\n out1 = off\n", b);
  TEST_ASSERT_TRUE_MESSAGE(t.find(R"("setting":"out1_on_ms")") != std::string::npos, t.c_str());
}

void every_message_of_the_added_lines() {
  struct C { const char* text; const char* want; };
  const C cases[] = {
      {"effect a:\n wait for trig1\n limit 3 per 60\n", "line 3: write it as \"limit 3 runs per 3 min\"."},
      {"effect a:\n wait for trig1\n limit three runs per 60\n", "line 3: \"three\" is not a number of runs. Use 1 to 20, or the setting runs_per_cooldown."},
      {"effect a:\n wait for trig1\n limit 21 runs per 60\n", "line 3: \"21\" is not a number of runs. Use 1 to 20, or the setting runs_per_cooldown."},
      {"effect a:\n wait for trig1\n limit 3 runs per soon\n", "line 3: \"soon\" is not a time. Write seconds (wait 2.5) or add a unit: ms, s or min."},
      {"effect a:\n wait for trig1\n limit 3 runs per 61 min\n", "line 3: a limit's time is at most 60 minutes."},
      {"effect a:\n wait for trig1\n limit 3 runs per 1 min\n limit 1 run per 5\n", "line 4: effect \"a\" already has a limit (line 3). An effect has one."},
      {"effect a:\n wait for trig1\n sleep runs_per_cooldown\n", "line 3: runs_per_cooldown is a number of runs, not a time."},
      {"effect a:\n wait for trig1\n pulse cc1\n", "line 3: write a pulse as \"pulse cc1 for 200 ms\"."},
      {"effect a:\n wait for trig1\n pulse dmx1 for 1\n", "line 3: dmx1 cannot pulse. Pulses work on out1 to out4, rly1, rly2, cc1 and cc2."},
      {"effect a:\n wait for trig1\n pulse rly1 for 50 ms\n", "line 3: rly1 is a relay or a contact: pulse it for at least 0.1 s."},
      {"effect a:\n wait for trig1\n pulse out9 for 1\n", "line 3: there is no out9 on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2."},
  };
  for (auto& c : cases) TEST_ASSERT_EQUAL_STRING_MESSAGE(c.want, firstError(c.text).c_str(), c.text);
  // two effects may set the same output (they take turns, SS-R8)
  TEST_ASSERT_EQUAL_STRING("(ok)", firstError("effect a:\n wait for trig1\n out1 = on\n sleep 1\n out1 = off\neffect b:\n wait for trig2\n out1 = on\n sleep 1\n out1 = off\n").c_str());
  ScriptBoard b = board();
  b.ready_lamp = true;
  TEST_ASSERT_EQUAL_STRING("line 3: out4 is the ready lamp (setting out4_ready_lamp). Turn it off to use out4 in a script.",
                           firstError("effect a:\n wait for trig1\n out4 = on\n out4 = off\n", b).c_str());
}

// ------------------------------------------------------------------ on a board

class GateSim : public Sim {
 public:
  explicit GateSim(const char* text = PULSER, std::initializer_list<const char*> settings = {"trig2_wait=return", "remote_free_effects=refill"}) : Sim("gates") {
    for (const char* l : {"cc1_role=shows", "cc2_role=shows", "trig_debounce_ms=100", "trig_release_ms=100", "mqtt_broker=127.0.0.1", "name=gates"})
      TEST_ASSERT_TRUE(serial(l).find("error") == std::string::npos);
    for (const char* l : settings) TEST_ASSERT_TRUE_MESSAGE(serial(l).find("error") == std::string::npos, l);
    JsonDocument q;
    q["op"] = "set_script";
    q["text"] = text;
    std::string j;
    serializeJson(q, j);
    Response r = request(j);
    TEST_ASSERT_TRUE_MESSAGE(r.isOk() && r.doc()["result"]["ok"].as<bool>(), r.json().c_str());
    step(2500);
  }
  size_t rises(Channel c, Millis from) const {
    size_t n = 0;
    Level last = 0;
    for (auto& x : trace) {
      if (x.channel != c) continue;
      if (x.t >= from && x.level > 0 && last == 0) n++;
      last = x.level;
    }
    return n;
  }
  Millis firstRise(Channel c, Millis from) const {
    Level last = 0;
    for (auto& x : trace) {
      if (x.channel != c) continue;
      if (x.t >= from && x.level > 0 && last == 0) return x.t;
      last = x.level;
    }
    return 0;
  }
  std::string action(const char* key, uint32_t session = 0) {
    std::string j = std::string(R"({"op":"action","key":")") + key + "\"}";
    Response r = session ? requestFrom(session, j) : request(j);
    return r.isOk() ? "ok" : r.errorMessage();
  }
  bool st(const char* key, Value& v) { return status(key, v); }
};


// SM-T3 / E2 (the "ready" rule, the default): pressed during the run (after a release) and held, it starts exactly
// once, the debounce after the effect can start again; taps during the run never queue (SM-T2, E1).
void a_press_held_through_the_run_starts_once_when_ready_taps_never_queue() {
  GateSim s;
  Millis t0 = s.now();
  s.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL(1, s.rises(Channel::Out1, t0));
  s.step(500);
  s.press(Input::Trig1, 60);
  s.step(300);
  s.press(Input::Trig1, 150);
  s.step(4000);
  TEST_ASSERT_EQUAL_MESSAGE(1, s.rises(Channel::Out1, t0), "taps during a run never queue");
  Millis t1 = s.now();
  s.press(Input::Trig1, 150);
  Millis run2 = s.firstRise(Channel::Out1, t1);
  TEST_ASSERT_TRUE(run2 > 0);
  s.step(500);
  s.setInput(Input::Trig1, true);
  s.step(5000);
  s.setInput(Input::Trig1, false);
  TEST_ASSERT_EQUAL_MESSAGE(2, s.rises(Channel::Out1, t1), "held through the run: one more run");
  Millis run3 = s.firstRise(Channel::Out1, run2 + 1);
  TEST_ASSERT_TRUE_MESSAGE(run3 >= run2 + 3010 + 100 && run3 <= run2 + 3010 + 100 + 20, std::to_string(run3 - run2).c_str());
}

// SM-T5: held continuously since the last run never starts another.
void held_since_the_last_run_never_starts_again() {
  GateSim s;
  Millis t0 = s.now();
  s.setInput(Input::Trig1, true);
  s.step(10000);
  s.setInput(Input::Trig1, false);
  TEST_ASSERT_EQUAL(1, s.rises(Channel::Out1, t0));
}

// SM-L: 3 runs per window from the first; the 4th press ignored; a remote start "cooling down m:ss left"; a press
// held across the window's end does not start (SM-T7, E5).
void three_runs_per_window_then_cooling_down() {
  GateSim s;
  Millis t0 = s.now();
  for (int i = 0; i < 3; i++) { s.press(Input::Trig1, 150); s.step(3500); }
  TEST_ASSERT_EQUAL(3, s.rises(Channel::Out1, t0));
  Value v;
  TEST_ASSERT_TRUE(s.st("runs_left", v) && v.i == 0);
  TEST_ASSERT_TRUE(s.st("cooldown_remaining_ms", v) && v.i > 40000);
  s.press(Input::Trig1, 150);
  s.step(3500);
  TEST_ASSERT_EQUAL_MESSAGE(3, s.rises(Channel::Out1, t0), "the 4th press is ignored");
  s.serial("remote_trigger_enable=true");
  std::string why = s.action("pulser");
  TEST_ASSERT_TRUE_MESSAGE(why.find("cooling down, ") == 0 && why.find(" left") != std::string::npos, why.c_str());
  s.setInput(Input::Trig1, true);
  s.step(60000);
  TEST_ASSERT_EQUAL_MESSAGE(3, s.rises(Channel::Out1, t0), "held across the end of the window");
  s.setInput(Input::Trig1, false);
  s.step(300);
  s.press(Input::Trig1, 150);
  s.step(100);
  TEST_ASSERT_EQUAL(4, s.rises(Channel::Out1, t0));
}

// SS-R10 / SM-Q / SM-R: an effect starts by its name with remote_trigger_enable; one start waits while it is busy,
// a second is "already queued", cancel and the enable going off empty the queue.
void an_effect_starts_by_its_name_with_the_enable() {
  GateSim s;
  Millis t0 = s.now();
  std::string why = s.action("pulser");
  TEST_ASSERT_TRUE_MESSAGE(why.find("the remote trigger is not enabled") == 0, why.c_str());
  s.serial("remote_trigger_enable=true");
  TEST_ASSERT_EQUAL_STRING("ok", s.action("pulser").c_str());
  s.step(100);
  TEST_ASSERT_EQUAL(1, s.rises(Channel::Out1, t0));
  TEST_ASSERT_EQUAL_STRING("ok", s.action("pulser").c_str());
  Value v;
  TEST_ASSERT_TRUE(s.st("queued", v) && v.b);
  TEST_ASSERT_EQUAL_STRING("already queued: one run is waiting for the effect to be ready", s.action("pulser").c_str());
  s.step(3500);
  TEST_ASSERT_EQUAL_MESSAGE(2, s.rises(Channel::Out1, t0), "the queued start runs when it can, without TRIG1");
  s.action("pulser");
  TEST_ASSERT_EQUAL_STRING("ok", s.action("cancel").c_str());
  s.step(3500);
  TEST_ASSERT_EQUAL(2, s.rises(Channel::Out1, t0));
  s.step(3500);
  Millis t1 = s.now();
  s.action("pulser");
  s.step(100);
  s.action("pulser");
  s.serial("remote_trigger_enable=false");
  s.step(4000);
  TEST_ASSERT_EQUAL(1, s.rises(Channel::Out1, t1));
  // MQTT by the name too (a fresh board: this one has used its runs); a name that is no effect is unknown
  GateSim m;
  m.serial("remote_trigger_enable=true");
  Millis t2 = m.now();
  {
    auto r = m.ctl().mqttOnMessage("proptroller/gates/control/pulser", "");
    TEST_ASSERT_TRUE_MESSAGE(r.empty(), r.empty() ? "" : (r[0].topic + " " + r[0].payload).c_str());
  }
  m.step(100);
  TEST_ASSERT_EQUAL(1, m.rises(Channel::Out1, t2));
  TEST_ASSERT_TRUE(s.action("nothing").find("no such action") == 0);
}

void a_phone_enable_ends_with_its_page() {
  GateSim s;
  TEST_ASSERT_TRUE(s.requestFrom(7, R"({"op":"set_params","values":[{"key":"remote_trigger_enable","value":true}]})").isOk());
  Value v;
  TEST_ASSERT_TRUE(s.st("remote_trigger_by", v) && v.s == "web:7");
  Millis t0 = s.now();
  s.action("pulser", 7);
  s.step(100);
  s.action("pulser", 7);
  s.sessionClosed(7);
  TEST_ASSERT_FALSE(s.ctl().settings().fx.remote_trigger_enable);
  s.step(4000);
  TEST_ASSERT_EQUAL_MESSAGE(1, s.rises(Channel::Out1, t0), "the queued start went with the page");
}

// remote_free_effects: refill starts remotely without the enable (SM-PR2); a refill pressed during the run (it
// shares OUT2, "return" rule) is remembered once and runs after it; a pulser tap during a refill is not saved
// ("ready" rule); run_count counts both, the window only the pulser.
void a_free_effect_and_a_remembered_start() {
  GateSim s;
  Millis t0 = s.now();
  s.press(Input::Trig1, 150);
  s.step(200);
  s.press(Input::Trig2, 150);  // during the run and let go: not run (memory with check)
  s.step(4000);
  TEST_ASSERT_EQUAL_MESSAGE(1, s.rises(Channel::Out2, t0), "a remembered press let go before the run ended");
  t0 = s.now();
  s.press(Input::Trig1, 150);
  s.step(200);
  s.setInput(Input::Trig2, true);  // during the run, held until it ends: remembered once, runs right after
  s.step(3500);
  s.setInput(Input::Trig2, false);
  s.step(6000);
  TEST_ASSERT_EQUAL(1, s.rises(Channel::Out1, t0));
  TEST_ASSERT_EQUAL_MESSAGE(2, s.rises(Channel::Out2, t0), "the run's OUT2, then the refill's");
  Value v;
  TEST_ASSERT_TRUE(s.st("run_count", v) && v.i == 3);
  TEST_ASSERT_TRUE(s.st("runs_left", v) && v.i == 1);
  Millis t1 = s.now();
  TEST_ASSERT_EQUAL_STRING("ok", s.action("refill").c_str());  // no enable needed
  s.step(300);
  s.press(Input::Trig1, 150);  // a tap during the refill: not saved
  s.step(5000);
  TEST_ASSERT_EQUAL(1, s.rises(Channel::Out2, t1));
  TEST_ASSERT_EQUAL(0, s.rises(Channel::Out1, t1));
}

// SS-R8, automatic turns: effects with no output in common run together; pressed together, the lower input (then
// the first in the script) goes first and the other is remembered and runs right after if its input is still held
// then; an effect starts only on its own trigger and may run again and again.
void effects_take_turns_by_their_outputs() {
  GateSim both("effect lamp:\n  wait for trig1\n  out1 = on\n  sleep 1\n  out1 = off\neffect fan:\n  wait for trig1\n  out2 = on\n  sleep 1\n  out2 = off\n");
  Millis t0 = both.now();
  both.press(Input::Trig1, 150);
  both.step(100);
  TEST_ASSERT_TRUE(both.firstRise(Channel::Out1, t0) > 0);
  TEST_ASSERT_EQUAL_MESSAGE(both.firstRise(Channel::Out1, t0), both.firstRise(Channel::Out2, t0), "no shared output: together");
  GateSim pair("effect one:\n  wait for trig1\n  out1 = on\n  sleep 1\n  out1 = off\neffect two:\n  wait for trig1\n  out1 = 50%\n  out2 = on\n  sleep 1\n  out2 = off\n  out1 = off\neffect three:\n  wait for trig2\n  out3 = on\n  sleep 1\n  out3 = off\n");
  t0 = pair.now();
  pair.press(Input::Trig1, 150);  // a tap: "two" is remembered but its input is let go when "one" ends
  pair.step(3000);
  TEST_ASSERT_EQUAL_MESSAGE(0, pair.rises(Channel::Out2, t0), "a remembered start with its input let go");
  t0 = pair.now();
  pair.setInput(Input::Trig1, true);  // held until "one" ends: "two" runs right after
  pair.step(1300);
  pair.setInput(Input::Trig1, false);
  pair.step(2000);
  Millis one = pair.firstRise(Channel::Out1, t0), two = pair.firstRise(Channel::Out2, t0);
  TEST_ASSERT_TRUE_MESSAGE(one > 0 && two >= one + 1000 && two <= one + 1010, (std::to_string(one) + " " + std::to_string(two)).c_str());
  TEST_ASSERT_EQUAL_MESSAGE(0, pair.rises(Channel::Out3, t0), "trig1 never starts the trig2 effect");
  t0 = pair.now();
  for (int i = 0; i < 3; i++) { pair.press(Input::Trig2, 150); pair.step(1500); }
  TEST_ASSERT_EQUAL_MESSAGE(3, pair.rises(Channel::Out3, t0), "the same effect again and again on its own trigger");
  TEST_ASSERT_EQUAL(0, pair.rises(Channel::Out2, t0));
  // the "return" rule: pressed while another effect holds its outputs, remembered (one deep) and run right after
  GateSim ret("effect fill:\n  wait for trig1\n  out2 = on\n  sleep 2\n  out2 = off\neffect drain:\n  wait for trig2\n  out1 = on\n  sleep 1\n  out1 = off\n  out2 = 50%\n  sleep 1\n  out2 = off\n",
              {"trig2_wait=return"});
  t0 = ret.now();
  ret.press(Input::Trig1, 150);
  ret.step(300);
  ret.setInput(Input::Trig2, true);
  ret.step(2000);
  ret.setInput(Input::Trig2, false);
  ret.step(3000);
  Millis fill = ret.firstRise(Channel::Out2, t0), drain = ret.firstRise(Channel::Out1, t0);
  TEST_ASSERT_TRUE_MESSAGE(drain >= fill + 2000 && drain <= fill + 2010, (std::to_string(fill) + " " + std::to_string(drain)).c_str());
}

// A pulse does not hold the effect back.
void a_pulse_does_not_hold_the_effect_back() {
  GateSim s;
  Millis t0 = s.now();
  s.press(Input::Trig1, 150);
  s.step(1500);
  Millis cc = s.firstRise(Channel::Cc1, t0), o1 = s.firstRise(Channel::Out1, t0);
  TEST_ASSERT_TRUE(cc > 0 && cc == o1);
  Millis cc_off = 0;
  for (auto& x : s.trace)
    if (x.channel == Channel::Cc1 && x.t > cc && x.level == 0) { cc_off = x.t; break; }
  TEST_ASSERT_TRUE_MESSAGE(cc_off >= cc + 200 && cc_off <= cc + 210, std::to_string(cc_off - cc).c_str());
}

void a_setting_named_for_a_time_is_read_when_the_line_runs() {
  GateSim s("effect a:\n wait for trig1\n out1 = on\n sleep out1_on_ms\n out1 = off\n");
  s.serial("out1_on_ms=300");
  Millis t0 = s.now();
  s.press(Input::Trig1, 150);
  s.step(1000);
  Millis on = s.firstRise(Channel::Out1, t0), off = 0;
  for (auto& x : s.trace)
    if (x.channel == Channel::Out1 && x.t > on && x.level == 0) { off = x.t; break; }
  TEST_ASSERT_TRUE_MESSAGE(off - on >= 300 && off - on <= 310, std::to_string(off - on).c_str());
}

// Status under the effect's keys, the controls on the web page, and OUT4 as the first effect's ready lamp.
void status_controls_and_the_ready_lamp() {
  GateSim s(PULSER, {"trig2_wait=return", "remote_free_effects=refill", "out4_ready_lamp=true"});
  Value v;
  TEST_ASSERT_TRUE(s.st("ready", v) && v.b);
  TEST_ASSERT_TRUE(s.st("run_count", v) && v.i == 0);
  TEST_ASSERT_TRUE(s.st("runs_left", v) && v.i == 3);
  TEST_ASSERT_EQUAL_MESSAGE(FULL, s.level(Channel::Out4), "the lamp is on while a press would start a run");
  std::string info = s.infoJson();
  TEST_ASSERT_TRUE(info.find(R"("key":"pulser")") != std::string::npos);
  TEST_ASSERT_TRUE(info.find(R"("key":"refill")") != std::string::npos);
  TEST_ASSERT_TRUE(info.find(R"("key":"cancel")") != std::string::npos);
  s.press(Input::Trig1, 150);
  s.step(50);
  TEST_ASSERT_TRUE(s.st("pulsing", v) && v.b);
  TEST_ASSERT_TRUE(s.st("ready", v) && !v.b);
  TEST_ASSERT_EQUAL(0, s.level(Channel::Out4));
  s.step(1100);
  TEST_ASSERT_TRUE(s.st("recovering", v) && v.b);
  s.step(3000);
  TEST_ASSERT_TRUE(s.st("ready", v) && v.b);
  Sim plain("plain");
  TEST_ASSERT_FALSE(plain.request(R"({"op":"action","key":"trigger"})").isOk());
}

// SS-R7: the background loop plays from power-on (a sound line before the first wait for), a sound played over it
// takes over at trigger_volume and the loop comes back by itself at ambient_volume; audio_loop = off ends it.
static std::vector<std::string> playsOf(Sim& s, size_t from) {
  std::vector<std::string> v;
  for (size_t i = from; i < s.world().df.plays.size(); i++) {
    auto& p = s.world().df.plays[i];
    v.push_back(p.what + "@" + std::to_string(p.volume));
  }
  return v;
}

void the_background_loop_resumes_by_itself() {
  Sim s("loop");
  s.world().df.setCard({std::make_tuple(1, 1, 30000), std::make_tuple(2, 1, 1500)});
  s.powerCycle();
  JsonDocument q;
  q["op"] = "set_script";
  q["text"] = "effect run:\n  audio_loop = 1\n  wait for trig1\n  audio_single = 2\n  out1 = on\n  sleep 1\n  out1 = off\n"
              "effect quiet:\n  wait for trig2\n  audio_loop = off\n";
  std::string j;
  serializeJson(q, j);
  TEST_ASSERT_TRUE(s.request(j).doc()["result"]["ok"].as<bool>());
  s.step(3000);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("/01/001", s.world().df.nowPlaying().c_str(), "the loop from the top of the effect");
  size_t n0 = s.world().df.plays.size();
  s.press(Input::Trig1, 150);
  s.step(4000);
  auto p = playsOf(s, n0);
  std::string all;
  for (auto& x : p) all += x + " ";
  TEST_ASSERT_EQUAL_MESSAGE(2, p.size(), all.c_str());
  TEST_ASSERT_EQUAL_STRING("/02/001@25", p[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/01/001@15", p[1].c_str());
  s.press(Input::Trig2, 150);
  s.step(1000);
  TEST_ASSERT_EQUAL_STRING("", s.world().df.nowPlaying().c_str());
}

// audio_volume: the script's sounds play at it from that line on (sound 2: folder 02); the sound op's loop_mp3
// plays /mp3/NNNN again each time it ends (the mp3 folder has no loop command), and play_mp3 plays it once.
void audio_volume_and_the_mp3_folder() {
  Sim s("vol");
  s.world().df.setCard({std::make_tuple(2, 1, 1000), std::make_tuple(DfModel::MP3, 3, 800)});
  s.powerCycle();
  JsonDocument q;
  q["op"] = "set_script";
  q["text"] = "effect a:\n  wait for trig1\n  audio_single = 2\n  wait 2\n  audio_volume = 40%\n  audio_single = 2\n";
  std::string j;
  serializeJson(q, j);
  TEST_ASSERT_TRUE(s.request(j).doc()["result"]["ok"].as<bool>());
  s.step(3000);
  size_t n0 = s.world().df.plays.size();
  s.press(Input::Trig1, 150);
  s.step(4000);
  auto p = playsOf(s, n0);
  TEST_ASSERT_EQUAL(2, p.size());
  TEST_ASSERT_EQUAL_STRING("/02/001@25", p[0].c_str());  // trigger_volume until a script sets one
  TEST_ASSERT_EQUAL_STRING("/02/001@12", p[1].c_str());  // round(40 x 30 / 100)
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_script","text":""})").isOk());
  n0 = s.world().df.plays.size();
  TEST_ASSERT_TRUE(s.request(R"({"op":"sound","cmd":"loop_mp3","track":3})").isOk());
  s.step(2600);  // 0.8 s a play: three plays
  p = playsOf(s, n0);
  TEST_ASSERT_TRUE_MESSAGE(p.size() >= 3, std::to_string(p.size()).c_str());
  for (auto& x : p) TEST_ASSERT_EQUAL_STRING("/mp3/0003", x.substr(0, 9).c_str());
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_params","values":[{"key":"ambient","value":false}]})").isOk());
  TEST_ASSERT_TRUE(s.request(R"({"op":"sound","cmd":"stop"})").isOk());
  s.step(2000);
  size_t n1 = s.world().df.plays.size();
  TEST_ASSERT_TRUE(s.request(R"({"op":"sound","cmd":"play_mp3","track":3})").isOk());
  s.step(700);
  TEST_ASSERT_EQUAL(1, s.world().df.plays.size() - n1);
  TEST_ASSERT_EQUAL_STRING("/mp3/0003", s.world().df.plays.back().what.c_str());
}

// A run that turns the loop off: back at the effect's top, its sound lines run again and the loop is back.
void the_sound_lines_at_the_top_run_after_each_run() {
  Sim s("loop");
  s.world().df.setCard({std::make_tuple(1, 1, 30000), std::make_tuple(2, 1, 1500)});
  s.powerCycle();
  JsonDocument q;
  q["op"] = "set_script";
  q["text"] = "effect run:\n  audio_loop = 1\n  wait for trig1\n  audio_loop = off\n  out1 = on\n  sleep 1\n  out1 = off\n";
  std::string j;
  serializeJson(q, j);
  TEST_ASSERT_TRUE(s.request(j).doc()["result"]["ok"].as<bool>());
  s.step(3000);
  TEST_ASSERT_EQUAL_STRING("/01/001", s.world().df.nowPlaying().c_str());
  s.press(Input::Trig1, 150);
  s.step(500);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", s.world().df.nowPlaying().c_str(), "the run turned the loop off");
  s.step(2000);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("/01/001", s.world().df.nowPlaying().c_str(), "back at the top, the loop again");
  // a start that waited for the outputs goes after those sound lines: its sound is not cut by the loop
  q["text"] = "effect run:\n  audio_loop = 1\n  wait for trig1\n  audio_loop = off\n  out1 = on\n  sleep 1\n  out1 = off\n"
              "effect other:\n  wait for trig2\n  audio_single = 2\n  out1 = on\n  sleep 1\n  out1 = off\n";
  serializeJson(q, j = "");
  TEST_ASSERT_TRUE(s.request(j).doc()["result"]["ok"].as<bool>());
  TEST_ASSERT_TRUE(s.serial("trig2_wait=return").find("error") == std::string::npos);
  s.step(3000);
  s.press(Input::Trig1, 150);
  s.step(300);
  s.setInput(Input::Trig2, true);  // held until the run ends: the remembered start runs
  s.step(1000);
  s.setInput(Input::Trig2, false);
  s.step(300);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("/02/001", s.world().df.nowPlaying().c_str(), "the remembered start's sound");
}

// A profile names the page's buttons and status lines (control and status aliases with a label), a time left
// shows as m:ss under any name, refusals use a key alias's label and group, STOP can be hidden and the start
// buttons held; `pulsing` covers a refill's wait before its OUT2; MQTT status changes go out within 0.1 s.
void a_profile_names_the_page_and_the_status() {
  GateSim s(PULSER, {"trig2_wait=return", "remote_free_effects=refill", "stop_button=false", "start_buttons=hold"});
  Response r = s.request(R"({"op":"set_profile","data":{"topic_prefix":"gates","aliases":[)"
                         R"({"alias":"pulser","control":"trigger","label":"Go"},{"alias":"cancel_it","control":"cancel","label":"Cancel the waiting start"},)"
                         R"({"alias":"busy","status":"pulsing","label":"Busy"},{"alias":"time_left","status":"cooldown_remaining_ms"},)"
                         R"({"alias":"net_on","status":"remote_trigger_enable","label":"Remote on"},{"alias":"tonight","status":"runs_since_power_on","label":"Presses tonight"},)"
                         R"({"alias":"open_time","key":"out1_on_ms","label":"Valve open time","unit":"ms","group":"Timing"}]}})");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  std::string info = s.ctl().infoJson();
  TEST_ASSERT_TRUE_MESSAGE(info.find(R"("key":"pulser","label":"Go","confirm":false,"momentary":false,"hold":true)") != std::string::npos, info.c_str());
  TEST_ASSERT_TRUE_MESSAGE(info.find(R"("key":"refill","label":"Start refill")") != std::string::npos, info.c_str());
  TEST_ASSERT_TRUE_MESSAGE(info.find(R"("key":"cancel","label":"Cancel the waiting start")") != std::string::npos, info.c_str());
  TEST_ASSERT_TRUE_MESSAGE(info.find(R"("stop_button":false)") != std::string::npos, info.c_str());
  s.press(Input::Trig1, 150);
  s.step(300);
  std::string st = s.stateJson();
  TEST_ASSERT_TRUE_MESSAGE(st.find(R"({"key":"busy","value":true,"label":"Busy"})") != std::string::npos, st.c_str());
  TEST_ASSERT_TRUE_MESSAGE(st.find(R"("key":"time_left","value":)") != std::string::npos && st.find(R"(,"unit":"ms"})") != std::string::npos, st.c_str());
  TEST_ASSERT_TRUE_MESSAGE(st.find(R"({"key":"net_on","value":false,"label":"Remote on"})") != std::string::npos, st.c_str());
  TEST_ASSERT_TRUE_MESSAGE(st.find(R"("run_labels":{"runs_since_power_on":"Presses tonight"})") != std::string::npos, st.c_str());
  r = s.request(R"({"op":"set_params","values":[{"key":"open_time","value":99999}]})");
  TEST_ASSERT_EQUAL_STRING("Valve open time is set to 99999 ms but the board allows 10-60000 ms; lower it in Settings \xe2\x86\x92 Timing", r.errorMessage().c_str());
  auto m = s.ctl().mqttOnMessage("gates/gates/config/set/open_time", "99999");
  TEST_ASSERT_TRUE(m.size() == 1 && m[0].payload == "open_time: Valve open time is set to 99999 ms but the board allows 10-60000 ms; lower it in Settings \xe2\x86\x92 Timing");
  // the refill's wait before its OUT2 is "pulsing" too; MQTT sees OUT2 go off for the 0.3 s between the run's
  // recovery and the refill (both held until the run ends: memory with check)
  s.step(8000);
  s.ctl().mqttOnConnect();
  Millis t0 = s.now();
  std::vector<std::pair<Millis, std::string>> out2;
  s.press(Input::Trig1, 150);
  s.step(200);
  s.setInput(Input::Trig2, true);
  bool refill_busy = false;
  for (int i = 0; i < 7000; i++) {
    s.step(1);
    if (s.now() - t0 == 3500) s.setInput(Input::Trig2, false);
    for (auto& x : s.ctl().mqttPoll())
      if (x.topic == "gates/gates/status/recovering") out2.push_back({s.now() - t0, x.payload});
    if (s.ctl().scriptRunner()->running(1) && s.level(Channel::Out2) == 0) {
      Value v;
      refill_busy = s.st("busy", v) && v.b;
    }
  }
  std::string seen;
  for (auto& x : out2) seen += std::to_string(x.first) + ":" + x.second + " ";
  TEST_ASSERT_EQUAL_MESSAGE(4, out2.size(), seen.c_str());  // on, off, on, off
  TEST_ASSERT_TRUE_MESSAGE(refill_busy, "busy (pulsing) during the refill's wait before its OUT2");
}

// The pulse-and-recover effect written as a script (no added syntax: the start rules are settings, the turns come
// from the shared OUT2) gives the effect's output edges.
static const char* const LIKE_THE_EFFECT =
    "effect run:\n"
    "  wait for trig1\n"
    "  limit runs_per_cooldown runs per cooldown_ms\n"
    "  pulse cc1 for trigger_out_pulse_ms\n"
    "  pulse cc2 for trigger_out_pulse_ms\n"
    "  out1 = on\n"
    "  sleep out1_on_ms\n"
    "  out1 = off\n"
    "  sleep 10 ms\n"
    "  out2 = on\n"
    "  sleep out2_on_ms\n"
    "  out2 = off\n"
    "effect refill:\n"
    "  wait for trig2\n"
    "  sleep out1_on_ms\n"
    "  sleep 10 ms\n"
    "  out2 = on\n"
    "  sleep out2_on_ms\n"
    "  out2 = off\n";

static std::vector<std::pair<Millis, bool>> edgesOf(const Sim& s, Channel c) {
  std::vector<std::pair<Millis, bool>> v;
  bool last = false;
  for (auto& x : s.trace)
    if (x.channel == c && (x.level > 0) != last) { last = x.level > 0; v.push_back({x.t, last}); }
  return v;
}

void the_effect_written_as_a_script_gives_the_effects_edges() {
  const uint32_t SEEDS = 40;
  size_t edges = 0, matched = 0, runs_differ = 0;
  for (uint32_t seed = 1; seed <= SEEDS; seed++) {
    std::mt19937 rng(seed);
    auto chance = [&](int per_100k) { return (int)(rng() % 100000) < per_100k; };
    const char* common[] = {"out1_on_ms=200", "out2_on_ms=400", "cooldown_ms=6000", "runs_per_cooldown=3", "trig_debounce_ms=100",
                            "trig_release_ms=100", "trigger_out_pulse_ms=200"};
    Sim fx("fx");
    for (auto l : common) fx.serial(l);
    fx.serial("effect=pulse_and_recover");
    fx.serial("remote_trigger_enable=true");
    GateSim sc(LIKE_THE_EFFECT, {"out1_on_ms=200", "out2_on_ms=400", "cooldown_ms=6000", "runs_per_cooldown=3", "trigger_out_pulse_ms=200",
                                 "trig2_wait=return", "remote_free_effects=refill"});
    sc.serial("remote_trigger_enable=true");
    fx.step(2500);
    // the two boards are 2.5 s apart in time (the script board stepped in its constructor): compare from now on
    Millis f0 = fx.now(), s0 = sc.now();
    bool trig = false, rec = false;
    Millis changed = 0, chatter_until = 0;
    for (int ms = 0; ms < 60000; ms++) {
      Millis now = (Millis)ms;
      if (now < chatter_until) {
        if (chance(40000)) trig = !trig;
      } else if (trig) {
        if (chance(now - changed < 100 ? 3000 : 400)) { trig = false; changed = now; }
      } else {
        if (chance(150)) { trig = true; changed = now; }
        if (chance(20)) chatter_until = now + 5 + rng() % 60;
      }
      // TRIG2, the one known difference left out: the effect remembers a recover pressed while it cannot run and
      // runs it later even when TRIG2 was let go; a script effect runs a remembered press only if its input is
      // still held then (memory with check, SS-R8). So TRIG2 is pressed only while neither effect runs, and let go
      // only while neither runs (a press confirmed as a run starts is then still held when it can go).
      if (chance(20)) {
        const bool idle = !sc.ctl().scriptRunner()->running(0) && !sc.ctl().scriptRunner()->running(1) &&
                          fx.ctl().effect()->phase == EffectPhase::Idle;
        if (idle) rec = !rec;
      }
      bool remote = chance(10), remote_rec = chance(5) && !rec;  // (a press being confirmed: as above)
      fx.setInput(Input::Trig1, trig);
      sc.setInput(Input::Trig1, trig);
      fx.setInput(Input::Trig2, rec);
      sc.setInput(Input::Trig2, rec);
      if (remote) { fx.serial("trigger"); sc.serial("run"); }  // the effect's control, the script effect's name
      if (remote_rec) { fx.serial("recover"); sc.serial("refill"); }
      fx.tick1();
      sc.tick1();
      if (getenv("FX_TRACE") && seed == (uint32_t)atoi(getenv("FX_TRACE")) && ms >= atoi(getenv("FX_FROM")) && ms <= atoi(getenv("FX_TO"))) {
        const EffectMachine* m = fx.ctl().effect();
        const ScriptRunner* r = sc.ctl().scriptRunner();
        static bool lt = false, lr = false;
        if (trig != lt || rec != lr || remote || remote_rec || ms % 100 == 0)
          printf("ms %d trig %d rec %d rem %d rrec %d | fx phase %d q %d qr %d | sc run %d/%d q %d/%d pend %d/%d\n", ms, (int)trig, (int)rec, (int)remote, (int)remote_rec,
                 (int)m->phase, (int)m->queued_remote, (int)m->queued_recover, (int)r->running(0), (int)r->running(1), (int)r->queued(0), (int)r->queued(1),
                 (int)r->remembered(0), (int)r->remembered(1));
        lt = trig; lr = rec;
      }
    }
    bool differ = false;
    for (Channel c : {Channel::Out1, Channel::Out2, Channel::Cc1, Channel::Cc2}) {
      auto a = edgesOf(fx, c), b = edgesOf(sc, c);
      std::vector<std::pair<Millis, bool>> a2, b2;
      for (auto& x : a) if (x.first >= f0) a2.push_back({x.first - f0, x.second});
      for (auto& x : b) if (x.first >= s0) b2.push_back({x.first - s0, x.second});
      if (a2.size() != b2.size()) differ = true;
      if (getenv("FX_DEBUG") && seed == (uint32_t)atoi(getenv("FX_DEBUG"))) {
        size_t i = 0;
        while (i < a2.size() && i < b2.size() && a2[i].second == b2[i].second && (long)b2[i].first - (long)a2[i].first >= -10 && (long)b2[i].first - (long)a2[i].first <= 10) i++;
        printf("%s: %zu vs %zu, first mismatch %zu:", name(c), a2.size(), b2.size(), i);
        for (size_t k = i > 2 ? i - 2 : 0; k < i + 3; k++)
          printf(" [%ld %d | %ld %d]", k < a2.size() ? (long)a2[k].first : -1, k < a2.size() ? (int)a2[k].second : -1, k < b2.size() ? (long)b2[k].first : -1, k < b2.size() ? (int)b2[k].second : -1);
        printf("\n");
      }
      edges += a2.size();
      for (size_t i = 0; i < a2.size() && i < b2.size(); i++) {
        long dt = (long)b2[i].first - (long)a2[i].first;
        if (a2[i].second == b2[i].second && dt >= -10 && dt <= 10) matched++;
        else differ = true;
      }
    }
    Value v1, v2;
    if (!fx.status("run_count", v1) || !sc.status("run_count", v2) || v1.i != v2.i) differ = true;
    if (differ) {
      runs_differ++;
      TEST_MESSAGE(("differs: seed " + std::to_string(seed)).c_str());
    }
  }
  char b[160];
  snprintf(b, sizeof b, "%u runs x 60 s: %u of %u edges within one tick, %u runs differ", (unsigned)SEEDS, (unsigned)matched, (unsigned)edges, (unsigned)runs_differ);
  TEST_MESSAGE(b);
  TEST_ASSERT_TRUE(edges > 400);
  TEST_ASSERT_EQUAL(edges, matched);
  TEST_ASSERT_EQUAL(0, runs_differ);
}

// ------------------------------------------------------------------ test presses (op test_input)

static Response testPress(GateSim& s, const char* input, bool active, uint32_t session = 7, uint32_t ms = 0) {
  std::string j = std::string(R"({"op":"test_input","input":")") + input + R"(","active":)" + (active ? "true" : "false") +
                  (ms ? ",\"ms\":" + std::to_string(ms) : std::string()) + "}";
  return s.requestFrom(session, j);
}
static bool shownAsTest(GateSim& s, const char* input, bool starts) {
  std::string st = s.stateJson();
  std::string want = std::string(R"("input":")") + input + R"(")";
  size_t at = st.find(want);
  if (at == std::string::npos) return false;
  size_t end = st.find('}', at);
  std::string x = st.substr(at, end - at);
  return x.find(R"("test":true)") != std::string::npos && x.find(starts ? R"("test_starts":true)" : R"("test_starts":false)") != std::string::npos;
}

// with remote start off a test press shows on the input and starts nothing, except an effect named in
// remote_free_effects (it starts remotely without the enable, so a test press starts it too)
void a_test_press_with_remote_start_off_starts_only_a_free_effect() {
  GateSim s;
  Millis t0 = s.now();
  TEST_ASSERT_TRUE(testPress(s, "TRIG1", true).isOk());
  s.step(300);
  TEST_ASSERT_TRUE_MESSAGE(shownAsTest(s, "TRIG1", false), s.stateJson().c_str());
  TEST_ASSERT_TRUE(testPress(s, "TRIG1", false).isOk());
  s.step(3000);
  TEST_ASSERT_EQUAL_MESSAGE(0, s.rises(Channel::Out1, t0), "remote start is off: the pulser does not start");
  TEST_ASSERT_TRUE(s.stateJson().find(R"("test":true)") == std::string::npos);
  t0 = s.now();
  TEST_ASSERT_TRUE(testPress(s, "TRIG2", true).isOk());
  s.step(300);
  TEST_ASSERT_TRUE(testPress(s, "TRIG2", false).isOk());
  s.step(4000);
  TEST_ASSERT_EQUAL_MESSAGE(1, s.rises(Channel::Out2, t0), "refill is in remote_free_effects: the test press starts it");
}

// with remote start on a test press is a press like a real one (the same input path)
void a_test_press_with_remote_start_on_is_a_real_press() {
  GateSim s;
  TEST_ASSERT_TRUE(s.requestFrom(7, R"({"op":"set_params","values":[{"key":"remote_trigger_enable","value":true}]})").isOk());
  Millis t0 = s.now();
  TEST_ASSERT_TRUE(testPress(s, "TRIG1", true).isOk());
  s.step(200);
  TEST_ASSERT_TRUE(shownAsTest(s, "TRIG1", true));
  TEST_ASSERT_TRUE(testPress(s, "TRIG1", false).isOk());
  s.step(4000);
  TEST_ASSERT_EQUAL(1, s.rises(Channel::Out1, t0));
}

// a test press lets go by itself: after its "ms", after TEST_PRESS_MAX_MS at the most, and when its page closes
void a_test_press_always_lets_go() {
  GateSim s;
  TEST_ASSERT_TRUE(testPress(s, "USER1", true, 7, 200).isOk());
  s.step(100);
  TEST_ASSERT_TRUE(shownAsTest(s, "USER1", false));
  s.step(200);
  TEST_ASSERT_FALSE(shownAsTest(s, "USER1", false));
  TEST_ASSERT_TRUE(testPress(s, "USER2", true).isOk());
  s.step(TEST_PRESS_MAX_MS - 100);
  TEST_ASSERT_TRUE(shownAsTest(s, "USER2", false));
  s.step(200);
  TEST_ASSERT_FALSE_MESSAGE(shownAsTest(s, "USER2", false), "let go after TEST_PRESS_MAX_MS");
  TEST_ASSERT_TRUE(testPress(s, "TRIG2", true, 9).isOk());
  s.step(50);
  s.sessionClosed(9);
  s.step(20);
  TEST_ASSERT_FALSE_MESSAGE(shownAsTest(s, "TRIG2", false), "let go when the page holding it closes");
  // too long, or not an input
  TEST_ASSERT_FALSE(testPress(s, "TRIG1", true, 7, TEST_PRESS_MAX_MS + 1).isOk());
  TEST_ASSERT_FALSE(testPress(s, "OUT1", true).isOk());
}


int main() {
  UNITY_BEGIN();
  RUN_TEST(the_lines_check_clean_and_give_their_table);
  RUN_TEST(settings_name_times_and_counts);
  RUN_TEST(every_message_of_the_added_lines);
  RUN_TEST(a_press_held_through_the_run_starts_once_when_ready_taps_never_queue);
  RUN_TEST(held_since_the_last_run_never_starts_again);
  RUN_TEST(three_runs_per_window_then_cooling_down);
  RUN_TEST(an_effect_starts_by_its_name_with_the_enable);
  RUN_TEST(a_phone_enable_ends_with_its_page);
  RUN_TEST(a_free_effect_and_a_remembered_start);
  RUN_TEST(effects_take_turns_by_their_outputs);
  RUN_TEST(a_pulse_does_not_hold_the_effect_back);
  RUN_TEST(a_setting_named_for_a_time_is_read_when_the_line_runs);
  RUN_TEST(status_controls_and_the_ready_lamp);
  RUN_TEST(the_background_loop_resumes_by_itself);
  RUN_TEST(the_sound_lines_at_the_top_run_after_each_run);
  RUN_TEST(audio_volume_and_the_mp3_folder);
  RUN_TEST(a_profile_names_the_page_and_the_status);
  RUN_TEST(the_effect_written_as_a_script_gives_the_effects_edges);
  RUN_TEST(a_test_press_with_remote_start_off_starts_only_a_free_effect);
  RUN_TEST(a_test_press_with_remote_start_on_is_a_real_press);
  RUN_TEST(a_test_press_always_lets_go);
  return UNITY_END();
}
