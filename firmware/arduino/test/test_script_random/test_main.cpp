// SS-T3: random valid show scripts from the grammar (several effects, every line kind, shared inputs, audio,
// DMX, a wait for in the middle) on the simulated board, with random presses, seeded and reproducible. On every
// 10 ms tick:
//   SS-R2  a run starts only from the first wait for, never while the effect runs;
//   SS-R4  (and SS-R5) a wait for passes only after its input read open for trig_release_ms since the effect
//          arrived there, then closed for trig_debounce_ms, never in the boot lockout;
//   SS-R8  a script that sets one target from two effects is refused; outputs no effect sets stay off;
//   SS-R9  no output stays on longer than its maximum on-time.
#include <unity.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static const int RUNS = 120;                // seeds 1 ... RUNS
static const Millis RUN_MS = 60000;         // simulated time per run, after the boot lockout
static const Millis DEBOUNCE_MS = 100, RELEASE_MS = 100;
static const Millis SAMPLE_SLACK_MS = 2;    // an input change is seen on the next 1 ms sample
static const uint32_t MAX_ON_MS[CHANNEL_COUNT] = {1500, 0, 2500, 0, 1200, 0, 0, 900};  // 0: no limit

namespace {
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed * 2654435761u + 1) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  uint32_t below(uint32_t n) { return next() % n; }
  uint32_t range(uint32_t a, uint32_t b) { return a + below(b - a + 1); }
};

const char* const INPUTS[INPUT_COUNT] = {"trig1", "trig2", "user1", "user2", "user3"};
const char* const OUTS[CHANNEL_COUNT] = {"out1", "out2", "out3", "out4", "rly1", "rly2", "cc1", "cc2"};

struct Gen {
  std::string text;
  std::vector<std::vector<std::string>> targets;  // per effect
  int lines_of_kind[10] = {0};                    // set, dmx, sleep, fade, flicker, single, loop, stop, wait, midwait
};

std::string time(Rng& r, uint32_t lo, uint32_t hi) {
  uint32_t ms = r.range(lo, hi);
  switch (r.below(3)) {
    case 0: return std::to_string(ms) + " ms";
    case 1: { char b[16]; snprintf(b, sizeof b, "%u.%03u", ms / 1000, ms % 1000); return b; }
    default: { char b[16]; snprintf(b, sizeof b, "%u.%03us", ms / 1000, ms % 1000); return b; }
  }
}

std::string level(Rng& r, bool dimmable) {
  if (!dimmable || r.below(3) == 0) return r.below(2) ? "on" : "off";
  return std::to_string(r.range(0, 100)) + "%";
}

Gen generate(Rng& r) {
  Gen g;
  // targets: the 8 outputs and 12 DMX channels, dealt out so no two effects share one
  std::vector<std::string> pool(OUTS, OUTS + CHANNEL_COUNT);
  for (int i = 1; i <= 12; i++) pool.push_back("dmx" + std::to_string(i * 7));
  for (size_t i = pool.size() - 1; i > 0; i--) std::swap(pool[i], pool[r.below((uint32_t)i + 1)]);
  int effects = (int)r.range(2, 5);
  size_t at = 0;
  for (int e = 0; e < effects; e++) {
    std::vector<std::string> mine;
    int n = (int)r.range(1, 4);
    for (int k = 0; k < n && at < pool.size(); k++) mine.push_back(pool[at++]);
    g.targets.push_back(mine);
    g.text += "effect e" + std::to_string(e) + ":\n";
    g.text += std::string("  wait for ") + INPUTS[r.below(INPUT_COUNT)] + "\n";
    g.lines_of_kind[8]++;
    g.text += "  sleep " + time(r, 20, 400) + "\n";  // a pass always shows on the tick
    g.lines_of_kind[2]++;
    int blocks = (int)r.range(2, 7);
    for (int b = 0; b < blocks; b++) {
      std::string t = mine.empty() ? std::string() : mine[r.below((uint32_t)mine.size())];
      bool dim = !t.empty() && (t[0] == 'd' || t.compare(0, 3, "out") == 0);
      switch (r.below(9)) {
        case 0: case 1:
          if (t.empty()) break;
          if (!dim) g.text += "  sleep 0.1\n";  // relays and contacts switch at most every 100 ms
          g.text += "  " + t + " = " + level(r, dim) + "\n";
          g.lines_of_kind[t[0] == 'd' ? 1 : 0]++;
          break;
        case 2:
          g.text += "  sleep " + time(r, 10, 1500) + "\n";
          g.lines_of_kind[2]++;
          break;
        case 3:
          if (!dim) break;
          g.text += "  fade " + t + " to " + std::to_string(r.range(0, 100)) + "% over " + time(r, 50, 1000) + "\n";
          g.lines_of_kind[3]++;
          break;
        case 4:
          if (!dim) break;
          g.text += "  flicker " + t + " for " + time(r, 50, 800) + "\n";
          g.lines_of_kind[4]++;
          break;
        case 5:
          g.text += "  audio_single = " + std::to_string(r.range(1, 99)) + "\n";
          g.lines_of_kind[5]++;
          break;
        case 6:
          g.text += "  audio_loop = " + std::to_string(r.range(1, 99)) + "_loop.mp3\n";
          g.lines_of_kind[6]++;
          break;
        case 7:
          g.text += "  audio_stop\n";
          g.lines_of_kind[7]++;
          break;
        case 8:
          g.text += std::string("  wait for ") + INPUTS[r.below(INPUT_COUNT)] + "\n  sleep " + time(r, 20, 300) + "\n";
          g.lines_of_kind[9]++;
          g.lines_of_kind[2]++;
          break;
      }
    }
    // usually everything off at the end; sometimes left on (allowed, a warning)
    if (r.below(4))
      for (auto& t : mine) g.text += std::string(t[0] == 'r' || t[0] == 'c' ? "  sleep 0.1\n" : "") + "  " + t + " = off\n";
    g.text += "  sleep " + time(r, 20, 500) + "\n";
    g.lines_of_kind[2]++;
  }
  return g;
}

// the input's changes, (ms, closed), for the release / debounce check
struct History {
  std::vector<std::pair<Millis, bool>> ch[INPUT_COUNT];
  bool closedAt(int k, Millis t) const {
    bool c = false;
    for (auto& x : ch[k]) { if (x.first > t) break; c = x.second; }
    return c;
  }
  // Since when input k has been closed without a break at t (it is closed at t).
  Millis closedSince(int k, Millis t) const {
    Millis s = 0;
    for (auto& x : ch[k]) { if (x.first > t) break; if (x.second) s = x.first; }
    return s;
  }
  // An open stretch of at least `need` ms inside [from, to) (counted from `from` when it was open already).
  bool openFor(int k, Millis from, Millis to, Millis need) const {
    bool c = true;  // before the first sample the board takes every input as held
    Millis since = 0;
    auto check = [&](Millis end) {
      if (c) return false;
      Millis a = since > from ? since : from, b = end < to ? end : to;
      return b > a && b - a + SAMPLE_SLACK_MS >= need;
    };
    for (auto& x : ch[k]) {
      if (x.first >= to) break;
      if (x.second != c) {
        if (check(x.first)) return true;
        c = x.second;
        since = x.first;
      }
    }
    return check(to);
  }
};
}  // namespace

static int g_runs = 0, g_ticks = 0, g_starts = 0, g_passes = 0, g_refused = 0;
static int g_kinds[10] = {0};

static std::string request(const char* op, const std::string& text) {
  JsonDocument d;
  d["op"] = op;
  d["text"] = text;
  std::string j;
  serializeJson(d, j);
  return j;
}

void random_scripts_never_break_the_rules() {
  for (int seed = 1; seed <= RUNS; seed++) {
    Rng r((uint32_t)seed);
    Gen g = generate(r);
    for (int k = 0; k < 10; k++) g_kinds[k] += g.lines_of_kind[k];
    Sim sim("random");
    std::string params = R"({"op":"set_params","values":[{"key":"dmx_port","value":"s4"},)"
                         R"({"key":"rly1_role","value":"shows"},{"key":"rly2_role","value":"shows"},{"key":"cc1_role","value":"shows"},{"key":"cc2_role","value":"shows"},{"key":"interlock_relays","value":true})";
    for (int c = 0; c < CHANNEL_COUNT; c++)
      if (MAX_ON_MS[c]) params += std::string(",{\"key\":\"") + OUTS[c] + "_max_on_ms\",\"value\":" + std::to_string(MAX_ON_MS[c]) + "}";
    params += "]}";
    TEST_ASSERT_TRUE(sim.request(params).isOk());
    Response saved = sim.request(request("set_script", g.text));
    if (!(saved.doc()["result"]["ok"] | false)) {
      std::string b;
      serializeJson(saved.doc(), b);
      printf("seed %d script:\n%s\n%s\n", seed, g.text.c_str(), b.c_str());
      TEST_FAIL_MESSAGE("a generated script was refused");
    }
    const ScriptProgram* prog = sim.ctl().scriptProgram();
    const ScriptRunner* run = sim.ctl().scriptRunner();
    TEST_ASSERT_NOT_NULL(prog);
    TEST_ASSERT_NOT_NULL(run);
    // SS-R8: a target effect 0 sets, set again from effect 1, is refused
    std::string taken;
    for (int i = 0; i < prog->fx[0].count && taken.empty(); i++) {
      const ScriptStep& s = prog->step[prog->fx[0].first + i];
      if (s.op == ScriptStep::Op::Set || s.op == ScriptStep::Op::Fade || s.op == ScriptStep::Op::Flicker)
        taken = scriptIsDmx(s.target) ? "dmx" + std::to_string(s.target - SCRIPT_DMX + 1) : std::string(OUTS[s.target]);
    }
    size_t at = g.text.find("effect e1:\n");
    if (at != std::string::npos && !taken.empty()) {
      std::string bad = g.text;
      size_t after_wait = bad.find('\n', at + 11) + 1;
      bad.insert(after_wait, "  " + taken + " = off\n");
      Response c = sim.request(request("check_script", bad));
      TEST_ASSERT_FALSE_MESSAGE(c.doc()["result"]["ok"] | true, "two effects on one target");
      std::string m = c.doc()["result"]["errors"][0]["message"] | "";
      TEST_ASSERT_TRUE_MESSAGE(m.find("is already used by effect \"e0\"") != std::string::npos, m.c_str());
      g_refused++;
    }
    const Millis boot_end = SCRIPT_BOOT_LOCKOUT_MS;  // the sim boots at 0

    History h;
    for (int k = 0; k < INPUT_COUNT; k++) h.ch[k].push_back({1, false});  // open from power-on, seen at the first sample
    // per effect: the line it is on, since when, and the run count
    struct Fx { uint16_t line; Millis since; uint32_t runs; bool running; };
    std::vector<Fx> fx;
    for (int e = 0; e < prog->effects; e++) fx.push_back({run->line(e), 0, 0, false});
    auto waitInput = [&](int e, uint16_t line) -> int {
      const ScriptEffect& f = prog->fx[e];
      for (int i = 0; i < f.count; i++) {
        const ScriptStep& s = prog->step[f.first + i];
        if (s.line == line && s.op == ScriptStep::Op::Wait) return s.target;
      }
      return -1;
    };
    bool used[CHANNEL_COUNT] = {false};
    for (int c = 0; c < CHANNEL_COUNT; c++) used[c] = prog->channels & (1u << c);
    Millis next_toggle[INPUT_COUNT];
    for (int k = 0; k < INPUT_COUNT; k++) next_toggle[k] = r.range(0, 3000);
    Millis on_since[CHANNEL_COUNT] = {0};
    bool on[CHANNEL_COUNT] = {false};
    Millis end = boot_end + RUN_MS;
    while (sim.now() < end) {
      Millis t = sim.now();
      for (int k = 0; k < INPUT_COUNT; k++)
        if (t >= next_toggle[k]) {
          bool closed = !h.closedAt(k, t);
          sim.setInput(ALL_INPUTS[k], closed);
          h.ch[k].push_back({t, closed});
          // mostly presses of 0.05-0.4 s and gaps of 0.05-2 s; now and then a long hold
          next_toggle[k] = t + (closed ? (r.below(8) ? r.range(30, 400) : r.range(1000, 6000)) : r.range(30, 2000));
        }
      sim.tick();
      g_ticks++;
      Millis now = sim.now();
      for (int e = 0; e < prog->effects; e++) {
        Fx& f = fx[e];
        uint16_t line = run->line(e);
        uint32_t runs = run->runs(e);
        if (runs != f.runs) {
          // SS-R2: a new run only from the first wait for, never while running
          TEST_ASSERT_FALSE_MESSAGE(f.running, "a run started while the effect was running");
          TEST_ASSERT_EQUAL_MESSAGE(prog->step[prog->fx[e].first].line, f.line, "a run started away from the first wait for");
          TEST_ASSERT_EQUAL(f.runs + 1, runs);
          f.runs = runs;
          g_starts++;
        }
        if (line != f.line) {
          int k = waitInput(e, f.line);
          if (k >= 0) {
            // SS-R4 / SS-R5: it left a wait for at `now`: released since it arrived, then held for the debounce
            char why[200];
            snprintf(why, sizeof why, "seed %d effect %d line %u (input %d) passed at %llu, arrived %llu", seed, e, f.line, k,
                     (unsigned long long)now, (unsigned long long)f.since);
            TEST_ASSERT_TRUE_MESSAGE(now >= boot_end + DEBOUNCE_MS, why);
            TEST_ASSERT_TRUE_MESSAGE(h.closedAt(k, now), why);
            Millis c = h.closedSince(k, now);
            Millis from = c > boot_end ? c : boot_end;
            TEST_ASSERT_TRUE_MESSAGE(now - from + SAMPLE_SLACK_MS >= DEBOUNCE_MS, why);
            TEST_ASSERT_TRUE_MESSAGE(h.openFor(k, f.since, c, RELEASE_MS), why);
            g_passes++;
          }
          f.line = line;
          f.since = now;
        }
        f.running = run->running(e);
      }
      TEST_ASSERT_FALSE_MESSAGE(sim.level(Channel::Rly1) && sim.level(Channel::Rly2), "an interlocked pair is never both on");
      // SS-R8 on the pins: an output no effect sets stays off; SS-R9: the maximum on-time
      for (int c = 0; c < CHANNEL_COUNT; c++) {
        Level l = sim.level(ALL_CHANNELS[c]);
        if (!used[c]) TEST_ASSERT_EQUAL_MESSAGE(0, l, "an output the script does not set came on");
        if (l > 0 && !on[c]) { on[c] = true; on_since[c] = now; }
        if (l == 0) on[c] = false;
        if (MAX_ON_MS[c] && on[c]) {
          char why[96];
          snprintf(why, sizeof why, "seed %d %s on for %llu ms", seed, OUTS[c], (unsigned long long)(now - on_since[c]));
          TEST_ASSERT_TRUE_MESSAGE(now - on_since[c] <= MAX_ON_MS[c] + Sim::TICK_MS, why);
        }
      }
    }
    g_runs++;
  }
  char m[400];
  snprintf(m, sizeof m,
           "%d seeded runs x %llu s, %d ticks checked; %d runs started, %d wait for lines passed, %d shared-target scripts refused; "
           "lines generated: set %d, dmx %d, sleep %d, fade %d, flicker %d, audio_single %d, audio_loop %d, audio_stop %d, first wait %d, mid wait %d",
           g_runs, (unsigned long long)(RUN_MS / 1000), g_ticks, g_starts, g_passes, g_refused, g_kinds[0], g_kinds[1], g_kinds[2],
           g_kinds[3], g_kinds[4], g_kinds[5], g_kinds[6], g_kinds[7], g_kinds[8], g_kinds[9]);
  TEST_MESSAGE(m);
  TEST_ASSERT_TRUE(g_starts > RUNS);   // the presses do start runs
  TEST_ASSERT_TRUE(g_kinds[9] > 0 && g_kinds[3] > 0 && g_kinds[4] > 0 && g_kinds[1] > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(random_scripts_never_break_the_rules);
  return UNITY_END();
}
