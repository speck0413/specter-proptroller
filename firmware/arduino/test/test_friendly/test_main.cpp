// The beginner layer on the simulated board: sequences become shows, direct control, tasks, hooks,
// plain-word errors.
#include <unity.h>
#include <chrono>
#include <mutex>
#include <thread>
#include "ProptrollerAdvanced.h"
#include "proptroller/sim/sim.h"
#include <string.h>
#include <type_traits>
#include <utility>

// C7: a bare number where a time belongs picks the friendly overload (its static_assert says "write seconds(2)")
// instead of "no matching function". decltype never instantiates the body, so these compile; a sketch that calls
// one does not.
static_assert(std::is_same<decltype(std::declval<Sequence&>().turnOn(OUT1, 2)), Sequence&>::value, "friendly turnOn(OUT1, 2)");
static_assert(std::is_same<decltype(std::declval<Sequence&>().wait(2.5)), Sequence&>::value, "friendly wait(2.5)");
static_assert(std::is_same<decltype(std::declval<AdvancedSequence&>().turnOn(OUT1, 2)), AdvancedSequence&>::value, "friendly advanced turnOn");
static_assert(std::is_same<decltype(std::declval<AdvancedSequence&>().wait(2)), AdvancedSequence&>::value, "friendly advanced wait");
static_assert(std::is_same<decltype(std::declval<AdvancedSequence&>().cooldown(30)), AdvancedSequence&>::value, "friendly cooldown");

using namespace pt;
void setUp() {}
void tearDown() {}

static std::vector<std::pair<Millis, Level>> changes(Sim& s, Channel c, Millis from = 0) {
  std::vector<std::pair<Millis, Level>> v;
  for (auto& x : s.trace)
    if (x.channel == c && x.t >= from) v.push_back({x.t, x.level});
  return v;
}

void the_ten_line_sketch() {
  Proptroller prop;
  prop.begin();
  prop.when(TRIG1).playSound(1).turnOn(OUT1, seconds(2)).then().turnOn(OUT2, seconds(5));  // beginner calls only
  Sim& sim = prop.hostSim();
  TEST_ASSERT_NULL_MESSAGE(prop.advanced().engine().show(1), "the example show goes when the sketch has its own");
  TEST_ASSERT_NOT_NULL(prop.advanced().engine().show(101));
  for (int i = 0; i < 200; i++) prop.update();  // 2 s
  Millis t0 = sim.now();
  sim.setInput(Input::Trig1, true);
  for (int i = 0; i < 15; i++) prop.update();  // 0.15 s: past the 0.1 s it must be held
  sim.setInput(Input::Trig1, false);
  for (int i = 0; i < 800; i++) prop.update();
  auto o1 = changes(sim, Channel::Out1, t0), o2 = changes(sim, Channel::Out2, t0);
  TEST_ASSERT_EQUAL(2, o1.size());
  TEST_ASSERT_EQUAL(2000, o1[1].first - o1[0].first);
  TEST_ASSERT_EQUAL(2, o2.size());
  TEST_ASSERT_EQUAL(o1[1].first, o2[0].first);  // then(): OUT2 starts when OUT1 ends
  TEST_ASSERT_EQUAL(5000, o2[1].first - o2[0].first);
  bool played = false;
  for (auto& p : sim.world().df.plays) played |= p.what == "/02/001";
  TEST_ASSERT_TRUE(played);
}

// USER1-3 are triggers by default: prop.when(USER1) works with no setting; held at power-on it never fires.
void user_buttons_are_triggers_by_default() {
  Proptroller prop;
  prop.begin();
  Sim& sim = prop.hostSim();
  sim.setInput(Input::User2, true);  // held from power-on
  prop.when(USER1).turnOn(OUT1, seconds(1));
  prop.when(USER2).turnOn(OUT2, seconds(1));
  for (int i = 0; i < 200; i++) prop.update();
  Millis t0 = sim.now();
  sim.setInput(Input::User1, true);
  for (int i = 0; i < 10; i++) prop.update();
  sim.setInput(Input::User1, false);
  for (int i = 0; i < 150; i++) prop.update();
  TEST_ASSERT_EQUAL_MESSAGE(2, changes(sim, Channel::Out1, t0).size(), "USER1 started its sequence");
  TEST_ASSERT_EQUAL_MESSAGE(0, changes(sim, Channel::Out2, 0).size(), "USER2 held at power-on started nothing");
  sim.setInput(Input::User2, false);
  for (int i = 0; i < 10; i++) prop.update();
  sim.setInput(Input::User2, true);
  for (int i = 0; i < 10; i++) prop.update();
  sim.setInput(Input::User2, false);
  for (int i = 0; i < 150; i++) prop.update();
  TEST_ASSERT_EQUAL_MESSAGE(2, changes(sim, Channel::Out2, 0).size(), "released and pressed again: it runs");
}

// Without prop.begin(): the first call starts the controller and it runs in the background, so a loop() that
// blocks (nobody calls prop.update() for 1.5 s) stops neither the trigger, the output nor the protocol.
void runs_without_begin_and_a_blocked_loop_stops_nothing() {
  Proptroller prop;
  prop.when(TRIG1).turnOn(OUT1, seconds(1));  // the first call: the controller starts
  std::recursive_mutex& mu = proptrollerHostMutex();
  auto now = [&] { std::lock_guard<std::recursive_mutex> g(mu); return prop.hostSim().now(); };
  auto waitUntil = [&](Millis t) { while (now() < t) std::this_thread::sleep_for(std::chrono::milliseconds(5)); };
  waitUntil(2000);  // past the boot lockout
  Millis t0 = now();
  { std::lock_guard<std::recursive_mutex> g(mu); prop.hostSim().setInput(Input::Trig1, true); }
  waitUntil(t0 + 200);
  { std::lock_guard<std::recursive_mutex> g(mu); prop.hostSim().setInput(Input::Trig1, false); }
  waitUntil(t0 + 1500);
  std::lock_guard<std::recursive_mutex> g(mu);
  TEST_ASSERT_EQUAL_MESSAGE(2, changes(prop.hostSim(), Channel::Out1, t0).size(), "OUT1 on for 1 s while loop() was blocked");
  std::string q = R"({"op":"state"})";
  TEST_ASSERT_TRUE(prop.advanced().controller().call(q.data(), q.size()).find("\"ok\":true") != std::string::npos);
  Millis before = prop.hostSim().now();
  prop.update();  // the background does this: a no-op
  TEST_ASSERT_EQUAL(before, prop.hostSim().now());
}

// the USB serial port reaches the console and counts as local (no page lock)
void the_serial_console_answers() {
  Proptroller prop;
  prop.begin();
  pt::NetApp& app = prop.hostApp();
  TEST_ASSERT_TRUE(app.consoleLine("name").find("name=") == 0);
  std::string lock = R"({"op":"set_params","values":[{"key":"pin","value":"123456"},{"key":"page_lock","value":true}]})";
  TEST_ASSERT_TRUE(app.callSerial(lock.data(), lock.size()).find("\"ok\":true") != std::string::npos);
  std::string change = R"({"op":"set_params","values":[{"key":"name","value":"Porch"}]})";
  TEST_ASSERT_TRUE(app.callSerial(change.data(), change.size()).find("\"ok\":true") != std::string::npos);
  TEST_ASSERT_TRUE(app.call(change.data(), change.size()).find("\"ok\":false") != std::string::npos);
}

void rules_and_more_triggers() {
  Proptroller prop;
  prop.begin();
  prop.advanced().when(TRIG2).named("Prop").turnOn(RLY1, seconds(1)).cooldown(minutes(3)).allowRunsPerCooldown(3).alsoOnCue("prop");
  const Show* s = prop.advanced().engine().show(101);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_STRING("Prop", s->name.c_str());
  TEST_ASSERT_EQUAL(180000, s->cooldown_ms);
  TEST_ASSERT_EQUAL(3, s->runs_per_cooldown);
  TEST_ASSERT_EQUAL(2, s->triggers.size());
  prop.advanced().every(minutes(1)).blink(OUT3, 3, milliseconds(200), milliseconds(300)).wait(seconds(1)).setPixels(ORANGE).then().pixelsOff();
  const Show* b = prop.advanced().engine().show(102);
  TEST_ASSERT_EQUAL(1500, b->length());  // wait() counts from the start of the blink, then() from its end
  prop.advanced().atPowerOn().fadeIn(OUT4, seconds(1)).repeatForever().priority(0);
  TEST_ASSERT_TRUE(prop.advanced().engine().show(103)->looping);
}

void plain_word_errors() {
  Proptroller prop;
  prop.begin();
  // a relay cannot dim, CC1 is busy with its default role, 21 runs is too many
  prop.advanced().when(TRIG1).dim(RLY1, percent(50));
  prop.advanced().when(TRIG1).turnOn(CC1);
  prop.advanced().when(TRIG1).turnOn(OUT1).allowRunsPerCooldown(21);
  TEST_ASSERT_EQUAL(0, prop.advanced().engine().showCount() - (prop.advanced().engine().show(1) ? 1 : 0));
  std::vector<std::string> ev;
  prop.advanced().controller().eventsSince(0, ev);
  std::string all;
  for (auto& e : ev) all += e;
  TEST_ASSERT_TRUE(all.find("does not fit that output") != std::string::npos);
  TEST_ASSERT_TRUE(all.find("has another job") != std::string::npos);
  TEST_ASSERT_TRUE(all.find("1 to 20") != std::string::npos);
}

struct Counter : PropTask {
  int presses = 0;
  void update(PropAdvanced& prop, uint64_t) override {
    if (prop.wasPressed(USER3)) {
      presses++;
      prop.turnOn(OUT4);
    }
  }
};

static int g_changes = 0;
static void seen(PropOutput, int) { g_changes++; }

void tasks_direct_control_and_hooks() {
  Proptroller prop;
  prop.begin();
  Counter c;
  prop.advanced().addTask(c);
  prop.advanced().afterOutputChange(seen);
  Sim& sim = prop.hostSim();
  for (int i = 0; i < 20; i++) prop.update();
  sim.setInput(Input::User3, true);
  for (int i = 0; i < 10; i++) prop.update();
  sim.setInput(Input::User3, false);
  for (int i = 0; i < 10; i++) prop.update();
  TEST_ASSERT_EQUAL(1, c.presses);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out4));
  TEST_ASSERT_TRUE(g_changes >= 1);
  prop.advanced().dim(OUT2, percent(25));
  prop.update();
  TEST_ASSERT_EQUAL(250, sim.level(Channel::Out2));
  prop.advanced().dim(RLY1, percent(25));  // refused with a message
  prop.update();
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Rly1));
  prop.advanced().stopEverything();
  prop.update();
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out2));
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out4));
}

void held_trigger_rule_and_opt_out() {
  Proptroller prop;
  prop.begin();
  prop.advanced().when(TRIG1).turnOn(OUT1, seconds(1));
  Sim& sim = prop.hostSim();
  sim.setInput(Input::Trig1, true);  // held from power-on
  for (int i = 0; i < 100; i++) prop.update();
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  TEST_ASSERT_FALSE(prop.advanced().wasPressed(TRIG1));
  Proptroller expert;
  expert.begin();
  expert.when(TRIG1).turnOn(OUT1, seconds(1));
  expert.advanced().allowTriggerHeldAtPowerOn(TRIG1);
  expert.hostSim().setInput(Input::Trig1, true);
  for (int i = 0; i < 15; i++) expert.update();
  TEST_ASSERT_EQUAL(1000, expert.hostSim().level(Channel::Out1));
}

static bool echoOp(Controller& c, Request& r, Response& out, void*) {
  if (r.op != "echo_uptime") return false;
  out = Response::ok(r, "echo");
  out.result()["uptime_ms"] = (unsigned long long)c.now();
  return true;
}

void custom_requests_reach_the_hook() {
  Proptroller prop;
  prop.begin();
  Sim& sim = prop.hostSim();
  const char* q = R"({"op":"echo_uptime","id":4})";
  std::string before = sim.call(q, strlen(q));
  TEST_ASSERT_TRUE(before.find("bad_request") != std::string::npos);
  prop.advanced().onRequest(echoOp);
  for (int i = 0; i < 5; i++) prop.update();
  std::string after = sim.call(q, strlen(q));
  TEST_ASSERT_EQUAL_STRING(R"({"v":1,"id":4,"ok":true,"result":{"kind":"echo","uptime_ms":50}})", after.c_str());
  std::string other = prop.advanced().controller().call(R"({"op":"nope"})", 13);
  TEST_ASSERT_TRUE(other.find("unknown op `nope`") != std::string::npos);
}

// A whole firmware of its own on the same board parts.
struct Tiny : Firmware {
  BoardParts parts;
  int ticks = 0;
  explicit Tiny(BoardParts& p) : parts(p) {}
  void tick(Millis now) override {
    ticks++;
    parts.pins->setDigital(0, (now / 1000) % 2 == 1);  // RLY1 follows the seconds
  }
  bool rebootRequested() const override { return false; }
  std::string call(const char*, size_t) override { return R"({"v":1,"ok":true,"result":{"kind":"done"}})"; }
  std::string infoJson() override { return "{}"; }
  std::string stateJson() override { return "{}"; }
  uint32_t eventsSince(uint32_t, std::vector<std::string>&) override { return 0; }
  std::string announceJson(uint16_t) override { return "{}"; }
  std::vector<std::pair<std::string, std::string>> txt() override { return {}; }
  void netChanged(const NetState&) override {}
  NetConfig netConfig() override { return NetConfig(); }
  bool serialIn(const uint8_t* d, size_t n, std::string& out) override { out.assign((const char*)d, n); return true; }
};
static Tiny* g_tiny = nullptr;
static Firmware* makeTiny(BoardParts& p) { return g_tiny = new Tiny(p); }

void a_firmware_of_its_own() {
  Proptroller prop;
  prop.advanced().begin(makeTiny);
  for (int i = 0; i < 150; i++) prop.update();
  TEST_ASSERT_EQUAL(150, g_tiny->ticks);
  TEST_ASSERT_TRUE(prop.hostWorld().digital[0]);
  TEST_ASSERT_EQUAL(1500, prop.advanced().timeMs());
  prop.advanced().turnOn(OUT1);  // only with the generic controller: a message, nothing else
  TEST_ASSERT_FALSE(prop.advanced().isPressed(TRIG1));
  uint16_t raw = 0;
  prop.hostWorld().analog = 1234;
  TEST_ASSERT_TRUE(g_tiny->parts.analog->readRaw(raw) && raw == 1234);
}

// example 09: the built-in effect, with names for two terminals
void built_in_pulse_and_recover_sketch() {
  Proptroller prop;
  prop.begin();
  prop.advanced().withPulseAndRecover(PulseAndRecover().pulseFor(milliseconds(500)).recoverFor(seconds(1)).runsBeforeRest(3).restFor(minutes(3)));
  prop.advanced().nameInput(TRIG2, "REFILL", "Refill button");
  prop.advanced().nameOutput(OUT1, "VALVE", "Valve");
  Sim& sim = prop.hostSim();
  TEST_ASSERT_NOT_NULL(prop.advanced().controller().effect());
  TEST_ASSERT_NULL_MESSAGE(prop.advanced().engine().show(1), "the example show made way for the effect");
  TEST_ASSERT_EQUAL(TRIG2, prop.advanced().input("REFILL"));
  for (int i = 0; i < 250; i++) prop.update();
  Millis t0 = sim.now();
  sim.script(Input::Trig1, {{1, true}, {41, false}});  // a 40 ms tap: nothing
  for (int i = 0; i < 30; i++) prop.update();
  sim.setInput(Input::Trig1, true);
  for (int i = 0; i < 20; i++) prop.update();
  sim.setInput(Input::Trig1, false);
  for (int i = 0; i < 200; i++) prop.update();
  size_t n = 0;
  for (auto& c : changes(sim, Channel::Out1, t0)) n += c.second > 0;
  TEST_ASSERT_EQUAL_MESSAGE(1, n, "the tap does nothing, the hold starts one run");
  bool valve = false;
  for (auto& t : prop.advanced().controller().info().terminals) valve |= t.id == "OUT1" && t.display() == "VALVE (OUT1)";
  TEST_ASSERT_TRUE(valve);
}

// The beginner layer offers only the beginner calls; the advanced ones are on prop.advanced() (access is part of
// the substitution, so a protected or private call counts as missing).
#define PT_HAS(name, T, call)                                                                     \
  template <class X, class = void> struct name : std::false_type {};                              \
  template <class X> struct name<X, decltype(void(std::declval<X&>().call))> : std::true_type {};
PT_HAS(SeqCooldown, Sequence, cooldown(Duration{0}))
PT_HAS(SeqRunsPerCooldown, Sequence, allowRunsPerCooldown(1))
PT_HAS(SeqTurnOn, Sequence, turnOn(OUT1, Duration{0}))
PT_HAS(SeqWait, Sequence, wait(Duration{0}))
PT_HAS(PropSetting, Proptroller, setting("k", true))
PT_HAS(PropLoadProfile, Proptroller, loadProfile("{}"))
PT_HAS(PropTriggerHoldTime, Proptroller, triggerHoldTime(Duration{0}))
PT_HAS(PropNameInput, Proptroller, nameInput(TRIG1, "X"))
PT_HAS(PropMemoryReport, Proptroller, memoryReport())
PT_HAS(PropWhenTrig, Proptroller, when(TRIG1))
PT_HAS(PropAdvancedCall, Proptroller, advanced())
PT_HAS(SeqShow, Sequence, show())
PT_HAS(SeqCommit, Sequence, commit())

void beginner_layer_has_only_the_beginner_calls() {
  TEST_ASSERT_TRUE(SeqTurnOn<Sequence>::value);
  TEST_ASSERT_TRUE(SeqWait<Sequence>::value);
  TEST_ASSERT_TRUE(PropWhenTrig<Proptroller>::value);
  TEST_ASSERT_TRUE(PropAdvancedCall<Proptroller>::value);
  TEST_ASSERT_FALSE(SeqCooldown<Sequence>::value);
  TEST_ASSERT_FALSE(SeqRunsPerCooldown<Sequence>::value);
  TEST_ASSERT_FALSE(PropSetting<Proptroller>::value);
  TEST_ASSERT_FALSE(PropLoadProfile<Proptroller>::value);
  TEST_ASSERT_FALSE(PropTriggerHoldTime<Proptroller>::value);
  TEST_ASSERT_FALSE(PropNameInput<Proptroller>::value);
  TEST_ASSERT_FALSE(PropMemoryReport<Proptroller>::value);
  TEST_ASSERT_FALSE(SeqShow<Sequence>::value);
  TEST_ASSERT_FALSE(SeqCommit<Sequence>::value);
  TEST_ASSERT_FALSE(std::is_copy_constructible<Proptroller>::value);
  // the same calls are there on the advanced layer
  TEST_ASSERT_TRUE(SeqCooldown<AdvancedSequence>::value);
  TEST_ASSERT_TRUE(PropSetting<PropAdvanced>::value);
  TEST_ASSERT_TRUE(PropLoadProfile<PropAdvanced>::value);
  TEST_ASSERT_TRUE(PropMemoryReport<PropAdvanced>::value);
  TEST_ASSERT_TRUE(SeqShow<AdvancedSequence>::value);
  TEST_ASSERT_TRUE(SeqCommit<AdvancedSequence>::value);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(the_serial_console_answers);
  RUN_TEST(the_ten_line_sketch);
  RUN_TEST(beginner_layer_has_only_the_beginner_calls);
  RUN_TEST(user_buttons_are_triggers_by_default);
  RUN_TEST(runs_without_begin_and_a_blocked_loop_stops_nothing);
  RUN_TEST(rules_and_more_triggers);
  RUN_TEST(plain_word_errors);
  RUN_TEST(tasks_direct_control_and_hooks);
  RUN_TEST(held_trigger_rule_and_opt_out);
  RUN_TEST(custom_requests_reach_the_hook);
  RUN_TEST(a_firmware_of_its_own);
  RUN_TEST(built_in_pulse_and_recover_sketch);
  return UNITY_END();
}
