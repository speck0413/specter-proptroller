// Real threads against one controller, as on an ESP32 running ProptrollerAuto: a service task (network and serial
// requests through Controller::call), the sketch's loop task (Proptroller::setting / nameOutput: setParams and
// set_profile) and the step, all behind one recursive lock with the board's release / retake hooks. A request
// gives the lock back while it parses, checks a script or writes the flash (Controller::Unlocked); nothing another
// task does meanwhile may change what that work reads, or break the rule "a stored script's outputs keep the
// role 'shows'". Run it under ThreadSanitizer too: pio test -e native_tsan -f test_threads.
#include <unity.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <fstream>
#include <sstream>
#include "proptroller/sim/sim.h"
#include "proptroller/window_lock.h"

using namespace pt;
void setUp() {}
void tearDown() {}

// A recursive mutex as the board's (FreeRTOS recursive mutex / pico recursive_mutex): the Raw of window_lock.h.
struct HostRaw {
  using Id = std::thread::id;
  std::mutex m;
  std::condition_variable cv;
  std::thread::id owner;
  int count = 0;
  void take() {
    std::unique_lock<std::mutex> g(m);
    auto me = std::this_thread::get_id();
    if (count && owner == me) { count++; return; }
    cv.wait(g, [&] { return count == 0; });
    owner = me;
    count = 1;
  }
  void give() {
    std::unique_lock<std::mutex> g(m);
    if (--count == 0) { owner = std::thread::id(); cv.notify_all(); }
  }
  int releaseAll() {
    std::unique_lock<std::mutex> g(m);
    if (!count || owner != std::this_thread::get_id()) return 0;
    int n = count;
    count = 0;
    owner = std::thread::id();
    cv.notify_all();
    return n;
  }
  bool mine() {
    std::unique_lock<std::mutex> g(m);
    return count && owner == std::this_thread::get_id();
  }
  Id self() const { return std::this_thread::get_id(); }
  void pause() { std::this_thread::sleep_for(std::chrono::microseconds(200)); }
};

// The board's lock: the window rule over the mutex (what Proptroller installs on the boards).
struct BoardLock {
  WindowLock<HostRaw> l;
  void take() { l.take(); }
  void takeStep() { l.takeStep(); }
  void give() { l.give(); }
  static int release(void* u) { return ((BoardLock*)u)->l.releaseForWindow(); }
  static void retake(void* u, int n) { ((BoardLock*)u)->l.retakeAfterWindow(n); }
};
// Only the mutex: every task gets the lock while a window is open (as before the window rule). The controller
// must still refuse every change then, and never break the rule.
struct PlainLock {
  HostRaw r;
  void take() { r.take(); }
  void takeStep() { r.take(); }
  void give() { r.give(); }
  static int release(void* u) { return ((PlainLock*)u)->r.releaseAll(); }
  static void retake(void* u, int n) {
    for (int i = 0; i < n; i++) ((PlainLock*)u)->r.take();
  }
};

static const char* const USES_RLY1 = "effect a:\n  wait for trig1\n  rly1 = on\n  sleep 1\n  rly1 = off\n";
static const char* const NO_RLY1 = "effect b:\n  wait for trig2\n  out1 = on\n  sleep 2\n  out1 = off\n";

static std::string scriptBody(const char* text) {
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = text;
  std::string s;
  serializeJson(d, s);
  return s;
}
static bool ok(const std::string& reply) { return reply.find("\"ok\":true") == 6 || reply.find("{\"v\":1,\"ok\":true") == 0; }
static bool stored(const std::string& reply) { return reply.find("\"kind\":\"script_check\",\"ok\":true") != std::string::npos; }
static bool busy(const std::string& message) { return message.find("busy") != std::string::npos; }

// the rule every task checks after its own work, under the lock
static bool ruleHolds(Controller& c) {
  return c.scriptText().find("rly1") == std::string::npos || c.settings().role(Channel::Rly1) == OutputRole::Shows;
}

static void slowFlash() { std::this_thread::sleep_for(std::chrono::microseconds(300)); }

struct Counts {
  std::atomic<int> broken{0}, scripts{0}, settings{0}, busy_outside_step{0}, busy_step{0};
};

// Three tasks for N rounds: the service (set_script with and without rly1, get_script, an aliased output, MQTT's
// settings), the sketch (rly1_role back and forth, names through set_profile), the step (ticks, and a sketch task
// in the step changing a setting now and then).
template <class L>
static void threeTasks(L& lk, Counts& n, int rounds) {
  Sim sim("t");
  sim.step(2500);
  Controller& c = sim.ctl();
  sim.scriptFlash().save_hook = slowFlash;
  sim.flash().save_hook = slowFlash;
  {
    std::string b = R"({"op":"set_profile","data":{"aliases":[{"alias":"FOG","terminal":"OUT1","label":"fog"},{"alias":"DOOR","terminal":"TRIG1"}]}})";
    TEST_ASSERT_TRUE(ok(c.call(b.data(), b.size())));
    // without a second task: the rule is kept (the setting is refused while the script uses rly1)
    std::string a = scriptBody(USES_RLY1);
    TEST_ASSERT_TRUE(stored(c.call(a.data(), a.size())));
    Request q;
    TEST_ASSERT_FALSE(c.setParams(q, {ParamValue{"rly1_role", Value::text("trigger_repeat")}}).isOk());
  }
  c.setLockHooks(L::release, L::retake, &lk);
  std::atomic<bool> done{false};
  std::thread step([&] {
    for (int i = 0; !done; i++) {
      lk.takeStep();
      sim.tick();
      if (i % 7 == 0) {
        Controller::StepScope in_step(&c);
        Request r;
        Response x = c.setParams(r, {ParamValue{"ambient_volume", Value::integer(i % 20)}});
        if (!x.isOk() && busy(x.errorMessage())) n.busy_step++;
        if (!ruleHolds(c)) n.broken++;
      }
      lk.give();
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });
  std::thread service([&] {
    std::string a = scriptBody(USES_RLY1), b = scriptBody(NO_RLY1);
    const char* get = R"({"op":"get_script"})";
    const char* fog = R"({"op":"set_output","channel":"FOG","level":0})";
    for (int i = 0; i < rounds; i++) {
      const std::string& s = i % 2 ? b : a;
      lk.take();
      std::string r = c.call(s.data(), s.size());
      if (stored(r)) n.scripts++;
      if (r.find("busy") != std::string::npos) n.busy_outside_step++;
      if (!ruleHolds(c)) n.broken++;
      (void)c.call(get, strlen(get));
      (void)c.call(fog, strlen(fog));
      (void)c.mqttOnMessage(c.mqttPrefix() + "/config/set/ambient_volume", std::to_string(i % 20));
      lk.give();
    }
  });
  std::thread sketch([&] {
    for (int i = 0; i < rounds * 2; i++) {
      lk.take();
      Request r;
      Response x = c.setParams(r, {ParamValue{"rly1_role", Value::text(i % 2 ? "shows" : "trigger_repeat")}});
      if (x.isOk()) n.settings++;
      else if (busy(x.errorMessage())) n.busy_outside_step++;
      if (!ruleHolds(c)) n.broken++;
      std::string p = std::string(R"({"op":"set_profile","data":{"aliases":[{"alias":"FOG","terminal":"OUT1","label":"fog machine )") +
                      std::to_string(i) + R"( at the door"},{"alias":"DOOR","terminal":"TRIG1"})" + (i % 2 ? R"(,{"alias":"LAMP","terminal":"OUT2"})" : "") + "]}}";
      std::string pr = c.call(p.data(), p.size());
      if (pr.find("busy") != std::string::npos) n.busy_outside_step++;
      lk.give();
    }
  });
  service.join();
  sketch.join();
  done = true;
  step.join();
  c.setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_TRUE(ruleHolds(c));
}

// The boards' lock: the other tasks wait out every window, so they never see "busy"; only the step can be refused.
void three_tasks_with_the_board_lock() {
  static BoardLock lk;
  Counts n;
  threeTasks(lk, n, 300);
  printf("board lock: %d scripts and %d settings accepted; busy: %d outside the step, %d in the step; rule broken %d times\n",
         n.scripts.load(), n.settings.load(), n.busy_outside_step.load(), n.busy_step.load(), n.broken.load());
  TEST_ASSERT_EQUAL(0, n.broken.load());
  TEST_ASSERT_EQUAL(0, n.busy_outside_step.load());
  TEST_ASSERT_TRUE(n.scripts.load() > 0);
  TEST_ASSERT_TRUE(n.settings.load() > 0);
}

// Only the mutex (every task gets in during a window): the controller alone keeps the rule, refusing changes
// while a window is open. Before the window rule this broke the rule (and ThreadSanitizer saw the alias list and
// the stored script freed under a parse and a script check).
void three_tasks_with_a_plain_lock() {
  static PlainLock lk;
  Counts n;
  threeTasks(lk, n, 300);
  printf("plain lock: %d scripts and %d settings accepted; busy: %d outside the step, %d in the step; rule broken %d times\n",
         n.scripts.load(), n.settings.load(), n.busy_outside_step.load(), n.busy_step.load(), n.broken.load());
  TEST_ASSERT_EQUAL(0, n.broken.load());
}

// The review's case, without threads: a sketch's setting() arrives while set_script writes the record with the
// lock given back (and the stored script empty). It is refused, and the script that uses rly1 keeps rly1's role.
static Sim* g_sim = nullptr;
static bool g_held = false, g_tried = false;
static std::string g_answer;
void a_setting_during_the_script_write_is_refused() {
  Sim sim("t");
  g_sim = &sim;
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_script","text":"effect a:\n  wait for trig1\n  rly1 = on\n"})").isOk());
  sim.ctl().setLockHooks(
      [](void*) -> int {
        if (!g_held) return 0;
        g_held = false;
        if (!g_tried && g_sim->ctl().scriptText().empty()) {
          g_tried = true;
          Request r;
          Response x = g_sim->ctl().setParams(r, {ParamValue{"rly1_role", Value::text("trigger_repeat")}});
          g_answer = x.isOk() ? "ok" : x.errorMessage();
        }
        return 1;
      },
      [](void*, int) { g_held = true; }, nullptr);
  g_held = true;
  std::string body = R"({"op":"set_script","text":"effect b:\n  wait for trig2\n  rly1 = on\n"})";
  std::string out = sim.ctl().call(body.data(), body.size());
  g_held = false;
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_TRUE(g_tried);
  TEST_ASSERT_TRUE_MESSAGE(busy(g_answer), g_answer.c_str());
  TEST_ASSERT_TRUE(stored(out));
  TEST_ASSERT_TRUE(sim.ctl().settings().role(Channel::Rly1) == OutputRole::Shows);
  TEST_ASSERT_TRUE(ruleHolds(sim.ctl()));
}

// SS-N: a script that calls RLY1 by its profile alias. A sketch's nameOutput() / loadProfile() (set_profile)
// arriving while set_script writes the record with the lock given back is refused, so the stored script never
// loses the name it uses; without the window the same change is refused by the stored-script check.
static std::string g_profile_answer;
void a_name_change_during_the_script_write_is_refused() {
  Sim sim("t");
  g_sim = &sim;
  std::string prof = R"({"op":"set_profile","data":{"aliases":[{"alias":"FOG","terminal":"RLY1"}]}})";
  TEST_ASSERT_TRUE(ok(sim.ctl().call(prof.data(), prof.size())));
  g_tried = false;
  sim.ctl().setLockHooks(
      [](void*) -> int {
        if (!g_held) return 0;
        g_held = false;
        if (!g_tried && g_sim->ctl().scriptText().empty()) {
          g_tried = true;
          std::string none = R"({"op":"set_profile","data":{"aliases":[]}})";
          g_profile_answer = g_sim->ctl().call(none.data(), none.size());
        }
        return 1;
      },
      [](void*, int) { g_held = true; }, nullptr);
  g_held = true;
  std::string body = scriptBody("effect a:\n  wait for trig1\n  fog = on\n  sleep 1\n  fog = off\n");
  std::string out = sim.ctl().call(body.data(), body.size());
  g_held = false;
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_TRUE_MESSAGE(stored(out), out.c_str());
  TEST_ASSERT_TRUE(g_tried);
  TEST_ASSERT_TRUE_MESSAGE(g_profile_answer.find("busy") != std::string::npos, g_profile_answer.c_str());
  TEST_ASSERT_EQUAL(1, (int)sim.ctl().settings().profile.aliases.size());
  // and afterwards, with the lock held: refused by the stored-script check
  std::string none = R"({"op":"set_profile","data":{"aliases":[]}})";
  std::string r = sim.ctl().call(none.data(), none.size());
  TEST_ASSERT_TRUE_MESSAGE(r.find("the stored script uses fog") != std::string::npos, r.c_str());
}

// The window rule itself: another task waits for the window to end; the owner and the step do not.
void the_window_lock_lets_only_the_step_in() {
  static WindowLock<HostRaw> lk;
  std::atomic<int> phase{0};
  std::atomic<bool> other_in{false}, step_in{false};
  lk.take();
  lk.take();  // two levels, both given back
  int n = lk.releaseForWindow();
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL(0, lk.releaseForWindow());  // not held: nothing to give
  std::thread other([&] { lk.take(); other_in = true; lk.give(); });
  std::thread step([&] { lk.takeStep(); step_in = true; lk.give(); });
  step.join();
  TEST_ASSERT_TRUE(step_in.load());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  TEST_ASSERT_FALSE(other_in.load());  // still waiting: the window is open
  lk.take();                           // the window's owner may take it inside its own window (a log line)
  lk.give();
  lk.retakeAfterWindow(n);
  lk.give();
  lk.give();
  other.join();
  TEST_ASSERT_TRUE(other_in.load());
  (void)phase;
}

// LED patterns across the board's tasks: the pixel side on a thread of its own without the lock (as the core-0 task
// on the ESP32), the step pressing buttons, and requests sending, listing and deleting patterns and changing the
// script meanwhile. ThreadSanitizer (native_tsan) checks that the pixel side only ever touches its own copy.
void patterns_across_the_tasks() {
  static BoardLock lk;
  Sim sim("t");
  sim.pixels_by_caller = true;
  Controller& c = sim.ctl();
  std::string prog;
  {
    std::ifstream f("../pattern-golden/cases/puzzle-out-of-order.json", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, ss.str()));
    prog = d["program"].as<std::string>();
  }
  auto put = [&](const char* name) {
    JsonDocument d;
    d["op"] = "put_pattern";
    d["name"] = name;
    d["program"] = prog;
    std::string b;
    serializeJson(d, b);
    return c.call(b.data(), b.size());
  };
  std::string pc = R"({"op":"set_params","values":[{"key":"pixel_count","value":60}]})";
  TEST_ASSERT_TRUE(ok(c.call(pc.data(), pc.size())));
  TEST_ASSERT_TRUE(ok(put("clue puzzle")));
  std::string sc = scriptBody("effect p:\n  pixels = pattern clue puzzle\n  wait for user3\neffect c:\n  wait for trig1\n  pixels press clue1\n  pixels press reset\n  pixels press clue2\n");
  TEST_ASSERT_TRUE(stored(c.call(sc.data(), sc.size())));
  sim.step(2500);
  c.setLockHooks(BoardLock::release, BoardLock::retake, &lk);
  std::atomic<bool> done{false};
  std::atomic<int> frames{0};
  std::atomic<uint64_t> up{sim.uptime()};
  std::thread step([&] {
    for (int i = 0; !done; i++) {
      lk.takeStep();
      if (i % 20 == 0) sim.setInput(Input::Trig1, true);
      if (i % 20 == 10) sim.setInput(Input::Trig1, false);
      if (i % 50 == 25) sim.setInput(Input::User3, true);
      if (i % 50 == 35) sim.setInput(Input::User3, false);
      sim.tick();
      up = sim.uptime();
      lk.give();
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });
  std::thread pixel([&] {
    while (!done) {
      c.pixelService(up.load());  // no lock, as the board's pixel task
      frames++;
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  });
  std::thread service([&] {
    const char* list = R"({"op":"list_patterns"})";
    const char* del = R"({"op":"delete_pattern","name":"clue puzzle"})";
    const char* test = R"({"op":"pixel","action":{"target":"PIXEL","do":"pattern","name":"clue puzzle"}})";
    const char* untest = R"({"op":"pixel","action":null})";
    for (int i = 0; i < 150; i++) {
      lk.take();
      (void)put(i % 3 ? "clue puzzle" : "other");
      (void)c.call(list, strlen(list));
      if (i % 5 == 0) (void)c.call(del, strlen(del));
      if (i % 7 == 0) (void)c.call(test, strlen(test));
      if (i % 7 == 3) (void)c.call(untest, strlen(untest));
      c.background();
      lk.give();
      std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
  });
  service.join();
  done = true;
  step.join();
  pixel.join();
  c.setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_TRUE(frames.load() > 100);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(the_window_lock_lets_only_the_step_in);
  RUN_TEST(a_setting_during_the_script_write_is_refused);
  RUN_TEST(a_name_change_during_the_script_write_is_refused);
  RUN_TEST(three_tasks_with_the_board_lock);
  RUN_TEST(three_tasks_with_a_plain_lock);
  RUN_TEST(patterns_across_the_tasks);
  return UNITY_END();
}
