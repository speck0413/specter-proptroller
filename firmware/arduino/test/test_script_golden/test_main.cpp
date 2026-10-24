// The shared show-script golden set (firmware/show-script-golden/, the README is the contract): every case's
// check byte for byte, and every trace on the simulated board within the README's tolerances.
#include <unity.h>
#include <dirent.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static const char* const CASES = "../show-script-golden/cases/";
static const Millis EDGE_SLACK_MS = 20, DMX_SLACK_MS = 30, SOUND_SLACK_MS = 300, FLICKER_SETTLE_MS = 20;
static const Millis BOARD_UP_MS = 3000;  // time 0: the board has been up at least 3 s

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

static std::vector<std::string> caseNames() {
  std::vector<std::string> v;
  DIR* d = opendir(CASES);
  if (!d) return v;
  while (dirent* e = readdir(d))
    if (e->d_name[0] != '.') v.push_back(e->d_name);
  closedir(d);
  std::sort(v.begin(), v.end());
  return v;
}

static Value valueOf(JsonVariantConst v) {
  if (v.is<bool>()) return Value::boolean(v.as<bool>());
  if (v.is<long>()) return Value::integer(v.as<long>());
  return Value::text(v.as<std::string>());
}

static std::string failures;
static int checks_run = 0, checks_ok = 0, traces_run = 0, traces_ok = 0;

static void fail(const std::string& c, const std::string& why) { failures += c + ": " + why + "\n"; }

// ------------------------------------------------------------------ checks

static bool runCheck(const std::string& name, JsonDocument& cs, const std::string& text) {
  Settings s;
  // `buttons` is a setting of the checker's board only (golden README, `wait for`): this firmware has no learn mode and
  // its USER buttons are always triggers, but the shared set still checks the message other checkers give
  bool buttons_are_triggers = true;
  for (JsonPairConst kv : cs["settings"].as<JsonObjectConst>()) {
    if (!strcmp(kv.key().c_str(), "buttons")) { buttons_are_triggers = strcmp(kv.value() | "", "learn") != 0; continue; }
    if (s.set(kv.key().c_str(), valueOf(kv.value()))) { fail(name, std::string("bad setting ") + kv.key().c_str()); return false; }
  }
  SoundList sounds;
  std::string sj, why;
  serializeJson(cs["sounds"], sj);
  if (!soundListParse(sj, sounds, why)) { fail(name, why); return false; }
  ScriptProgram* table = new ScriptProgram();
  ScriptBoard board = scriptBoardFor(s);
  board.buttons_are_triggers = buttons_are_triggers;
  ScriptCheck c = scriptCompile(text, sounds, board, *table);
  JsonDocument got;
  scriptCheckJson(c, table, got.to<JsonObject>());
  delete table;
  JsonObjectConst want = cs["check"].as<JsonObjectConst>();
  if (want["table"].isNull()) got.remove("table");
  std::string a, b;
  serializeJson(want, a);
  serializeJson(got, b);
  if (a != b) {
    fail(name, "check differs:\n  want " + a + "\n  got  " + b);
    return false;
  }
  return true;
}

// ------------------------------------------------------------------ traces

namespace {
struct GoldenBoard {
  Sim sim{"golden"};
  Millis t0 = 0;
  // collected across power cycles, in ms from time 0
  std::vector<std::pair<Millis, std::pair<uint8_t, uint16_t>>> sound;
  std::vector<std::pair<Millis, std::vector<uint8_t>>> dmx;
  size_t df_seen = 0, dmx_seen = 0;
  Millis bootAt() const { return sim.now() - sim.uptime(); }
  void harvest() {
    auto& fr = sim.world().df.frames;
    for (; df_seen < fr.size(); df_seen++) {
      Millis t = fr[df_seen].t + bootAt();
      uint8_t c = fr[df_seen].cmd;
      uint16_t prm = fr[df_seen].param;
      // a loop in the mp3 folder is the same 0x12 again when its track ends (sound.h): not a frame of its own here
      bool again = false;
      if (c == 0x12)
        for (auto& f : sim.world().df.finished) again |= f.second == prm && fr[df_seen].t >= f.first && fr[df_seen].t - f.first <= 100;
      if (t >= t0 && !again && (c == 0x12 || c == 0x03 || c == 0x08 || c == 0x16)) sound.push_back({t - t0, {c, prm}});
    }
    auto& dm = sim.world().dmx_frames;
    for (; dmx_seen < dm.size(); dmx_seen++) {
      Millis t = dm[dmx_seen].first + bootAt();
      if (t >= t0) dmx.push_back({t - t0, dm[dmx_seen].second});
    }
  }
  void powerCycle() {
    harvest();
    sim.powerCycle(0);
    df_seen = 0;  // a fresh module model
  }
};
}  // namespace

static Channel channelNamed(const std::string& n) {
  Channel c = Channel::Out1;
  (void)parseChannel(n.c_str(), c);
  return c;
}

static Level levelAt(const Sim& sim, Channel c, Millis abs_t) {
  Level l = 0;
  for (auto& x : sim.trace)
    if (x.channel == c && x.t <= abs_t) l = x.level;
  return l;
}

static bool runTrace(const std::string& name, JsonDocument& cs, const std::string& text) {
  JsonObjectConst tr = cs["trace"].as<JsonObjectConst>();
  GoldenBoard b;
  Sim& sim = b.sim;
  // settings: the case's, then the trace's, the README's trigger times
  JsonDocument req;
  req["op"] = "set_params";
  JsonArray vals = req["values"].to<JsonArray>();
  auto add = [&](const char* k, JsonVariantConst v) {
    if (!strcmp(k, "buttons")) return;  // the checker's board only: USER buttons are always triggers here
    JsonObject o = vals.add<JsonObject>();
    o["key"] = k;
    o["value"] = v;
  };
  for (JsonPairConst kv : cs["settings"].as<JsonObjectConst>()) add(kv.key().c_str(), kv.value());
  for (JsonPairConst kv : tr["settings"].as<JsonObjectConst>()) add(kv.key().c_str(), kv.value());
  JsonDocument tms;
  tms["d"] = 100;
  add("trig_debounce_ms", tms["d"]);
  add("trig_release_ms", tms["d"]);
  std::string rs;
  serializeJson(req, rs);
  Response pr = sim.request(rs);
  if (!pr.isOk()) { fail(name, "settings refused: " + pr.errorMessage()); return false; }
  JsonDocument sr;
  sr["op"] = "set_script";
  sr["text"] = text;
  sr["sounds"] = cs["sounds"];
  std::string ss;
  serializeJson(sr, ss);
  Response r = sim.request(ss);
  if (!r.isOk() || !(r.doc()["result"]["ok"] | false)) { fail(name, "script refused: " + std::string(r.doc().as<std::string>())); return false; }
  sim.step(BOARD_UP_MS);
  b.t0 = sim.now();
  b.harvest();
  b.sound.clear();
  b.dmx.clear();
  size_t trace_from = sim.trace.size();
  (void)trace_from;
  // events
  Millis until = tr["until"] | 0;
  std::vector<std::pair<Millis, JsonObjectConst>> evs;
  for (JsonObjectConst e : tr["events"].as<JsonArrayConst>()) evs.push_back({e["t"] | 0, e});
  for (auto& e : evs) {
    Millis at = b.t0 + e.first;
    if (at > sim.now()) sim.step(at - sim.now());
    if (e.second["power_on"] | false) b.powerCycle();
    else if (!e.second["control"].isNull()) {
      // a control from the web, MQTT or serial (README "The trace": trigger, recover, cancel)
      JsonDocument q;
      q["op"] = "action";
      q["key"] = e.second["control"];
      std::string qs;
      serializeJson(q, qs);
      (void)sim.request(qs);
    } else if (!e.second["set"].isNull()) {
      // a setting changed (as on the serial console: key=value)
      for (JsonPairConst kv : e.second["set"].as<JsonObjectConst>()) {
        std::string v;
        if (kv.value().is<bool>()) v = kv.value().as<bool>() ? "true" : "false";
        else if (kv.value().is<long>()) v = std::to_string(kv.value().as<long>());
        else v = kv.value().as<std::string>();
        std::string out = sim.serial(std::string(kv.key().c_str()) + "=" + v);
        if (out.compare(0, 6, "error:") == 0) { fail(name, "set refused: " + out); return false; }
      }
    } else {
      Input in;
      (void)parseInput(e.second["in"] | "", in);
      sim.setInput(in, e.second["closed"] | false);
    }
  }
  if (b.t0 + until > sim.now()) sim.step(b.t0 + until - sim.now());
  b.harvest();

  JsonObjectConst ex = tr["expect"].as<JsonObjectConst>();
  bool ok = true;
  std::set<int> listed;
  // edges
  for (JsonPairConst kv : ex["edges"].as<JsonObjectConst>()) {
    Channel c = channelNamed(kv.key().c_str());
    listed.insert(index(c));
    std::vector<std::pair<Millis, Level>> got;
    for (auto& x : sim.trace)
      if (x.channel == c && x.t > b.t0 && x.t <= b.t0 + until) got.push_back({x.t - b.t0, x.level});
    JsonArrayConst want = kv.value().as<JsonArrayConst>();
    bool same = got.size() == want.size();
    for (size_t i = 0; same && i < got.size(); i++) {
      Millis t = want[i][0];
      Level l = want[i][1];
      same = got[i].second == l && got[i].first >= t && got[i].first <= t + EDGE_SLACK_MS;
    }
    if (!same) {
      std::string g;
      for (auto& x : got) g += "[" + std::to_string(x.first) + "," + std::to_string(x.second) + "]";
      std::string w;
      serializeJson(want, w);
      fail(name, std::string("edges of ") + kv.key().c_str() + ": want " + w + " got " + g);
      ok = false;
    }
  }
  // samples
  for (JsonObjectConst s : ex["samples"].as<JsonArrayConst>()) {
    Channel c = channelNamed(s["out"] | "");
    listed.insert(index(c));
    Millis t = s["t"];
    int want = s["level"], within = s["within"];
    int got = levelAt(sim, c, b.t0 + t);
    if (got < want - within || got > want + within) {
      fail(name, std::string("sample ") + (s["out"] | "") + " at " + std::to_string(t) + ": want " + std::to_string(want) + " got " + std::to_string(got));
      ok = false;
    }
  }
  // flicker
  for (JsonObjectConst f : ex["flicker"].as<JsonArrayConst>()) {
    Channel c = channelNamed(f["out"] | "");
    listed.insert(index(c));
    Millis from = f["from"], to = f["to"];
    int then = f["then"];
    std::set<Level> seen;
    bool over = false;
    for (auto& x : sim.trace)
      if (x.channel == c && x.t >= b.t0 + from && x.t <= b.t0 + to) { seen.insert(x.level); over |= x.level > then; }
    Level after = levelAt(sim, c, b.t0 + to + FLICKER_SETTLE_MS);
    if (seen.size() < 3 || over || after != then) {
      fail(name, std::string("flicker ") + (f["out"] | "") + ": " + std::to_string(seen.size()) + " levels, above: " + (over ? "yes" : "no") + ", then " + std::to_string(after));
      ok = false;
    }
  }
  // everything else stays off
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    if (listed.count(i)) continue;
    for (auto& x : sim.trace)
      if (index(x.channel) == i && x.t > b.t0 && x.t <= b.t0 + until && x.level != 0) {
        fail(name, std::string(name_of(ALL_CHANNELS[i])) + " came on at " + std::to_string(x.t - b.t0));
        ok = false;
        break;
      }
  }
  // DMX
  for (JsonPairConst kv : ex["dmx"].as<JsonObjectConst>()) {
    int ch = atoi(kv.key().c_str());
    std::vector<std::pair<Millis, int>> got;
    int last = 0;
    for (auto& f : b.dmx) {
      if (f.first > until) break;
      int v = f.second[ch - 1];
      if (v != last) { got.push_back({f.first, v}); last = v; }
    }
    JsonArrayConst want = kv.value().as<JsonArrayConst>();
    bool same = got.size() == want.size();
    for (size_t i = 0; same && i < got.size(); i++) {
      Millis t = want[i][0];
      same = got[i].second == want[i][1].as<int>() && got[i].first >= t && got[i].first <= t + DMX_SLACK_MS;
    }
    if (!same) {
      std::string g;
      for (auto& x : got) g += "[" + std::to_string(x.first) + "," + std::to_string(x.second) + "]";
      fail(name, "dmx " + std::to_string(ch) + ": got " + g + " (" + std::to_string(b.dmx.size()) + " frames)");
      ok = false;
    }
  }
  // sound
  if (!ex["sound"].isNull()) {
    JsonArrayConst want = ex["sound"].as<JsonArrayConst>();
    std::vector<std::pair<Millis, std::pair<uint8_t, uint16_t>>> got;
    for (auto& s : b.sound)
      if (s.first <= until) got.push_back(s);
    bool same = got.size() == want.size();
    for (size_t i = 0; same && i < got.size(); i++) {
      Millis t = want[i][0];
      std::string kind = want[i][1];
      uint8_t c = got[i].second.first;
      uint16_t p = got[i].second.second;
      // sound N: 0x12 N in the mp3 folder (0x03 N to play, 0x08 N to loop with SoundMode::Root)
      bool mp3 = SOUND_MODE == SoundMode::Mp3;
      bool k = (kind == "play" && c == (mp3 ? 0x12 : 0x03) && p == want[i][2].as<int>()) ||
               (kind == "loop" && c == (mp3 ? 0x12 : 0x08) && p == want[i][2].as<int>()) || (kind == "stop" && c == 0x16);
      same = k && got[i].first >= t && got[i].first <= t + SOUND_SLACK_MS;
    }
    if (!same) {
      std::string g;
      for (auto& x : got) {
        char buf[48];
        snprintf(buf, sizeof buf, "[%llu,0x%02X,%u]", (unsigned long long)x.first, x.second.first, x.second.second);
        g += buf;
      }
      std::string w;
      serializeJson(want, w);
      fail(name, "sound: want " + w + " got " + g);
      ok = false;
    }
  }
  return ok;
}

void every_golden_case() {
  std::vector<std::string> names = caseNames();
  TEST_ASSERT_TRUE_MESSAGE(names.size() > 0, "no cases: run from firmware/arduino (pio test)");
  for (auto& n : names) {
    std::string dir = std::string(CASES) + n + "/";
    std::string text = readFile(dir + "script.txt");
    JsonDocument cs;
    if (deserializeJson(cs, readFile(dir + "case.json"))) { fail(n, "case.json does not parse"); continue; }
    if (!cs["check"].isNull()) { checks_run++; checks_ok += runCheck(n, cs, text); }
    if (!cs["trace"].isNull()) { traces_run++; traces_ok += runTrace(n, cs, text); }
  }
  char sum[160];
  snprintf(sum, sizeof sum, "%zu cases: checks %d/%d, traces %d/%d", names.size(), checks_ok, checks_run, traces_ok, traces_run);
  TEST_MESSAGE(sum);
  if (!failures.empty()) TEST_MESSAGE(failures.c_str());
  TEST_ASSERT_TRUE_MESSAGE(failures.empty(), sum);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(every_golden_case);
  return UNITY_END();
}
