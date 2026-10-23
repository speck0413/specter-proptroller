// The profile's alias table (firmware/STATE-MACHINE.md §11, SM-AL0-7, E27-E31): a project's own names for keys,
// controls, status values and terminals, on every interface, with the reference's example table (generic
// names only; a project's own table lives with that project).
#include <unity.h>
#include "ProptrollerAdvanced.h"
#define PT_TEST_NO_OUT_NAMES
#include "../effect_common.h"

void setUp() {}
void tearDown() {}

// The reference's example table, plus a scaled alias (seconds for a time in ms).
static const char* const TABLE = R"({"topic_prefix": "myprop", "aliases": [
  {"alias": "valve_time", "key": "out1_on_ms", "label": "Valve open time", "unit": "ms"},
  {"alias": "skip_after_output", "key": "out3_enabled", "invert": true},
  {"alias": "rest_s", "key": "out2_on_ms", "label": "Rest", "unit": "s", "scale": 1000},
  {"alias": "go", "control": "trigger"},
  {"alias": "presses", "status": "run_count"},
  {"alias": "VALVE", "terminal": "OUT1", "label": "Valve"},
  {"alias": "REFILL", "terminal": "TRIG2", "label": "Refill button"}]})";

static Response load(Sim& s, const std::string& table) { return s.request(std::string(R"({"op":"set_profile","data":)") + table + "}"); }
static std::string settingsJson(Sim& s) {
  JsonDocument d;
  s.ctl().settings().toJson(d.to<JsonObject>());
  d.remove("profile");
  std::string j;
  serializeJson(d, j);
  return j;
}
static const MqttMsg* find(const std::vector<MqttMsg>& v, const std::string& topic) {
  for (auto& m : v)
    if (m.topic == topic) return &m;
  return nullptr;
}

void E27_SM_AL1_SM_AL2_every_alias_on_every_interface() {
  FxSim s;
  s.serial("mqtt_broker=127.0.0.1");
  s.step(2500);
  TEST_ASSERT_TRUE(load(s, TABLE).isOk());
  // web (set_params / get_params): the alias, its label and unit
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_params","values":[{"key":"valve_time","value":300}]})").isOk());
  TEST_ASSERT_EQUAL(300, s.ctl().settings().fx.out1_on_ms);
  Response p = s.request(R"({"op":"get_params"})");
  bool listed = false, valued = false;
  JsonArray params = p.doc()["result"]["params"], values = p.doc()["result"]["values"];
  for (JsonVariant d : params)
    if (d["key"] == "valve_time") listed = d["label"] == "Valve open time" && d["unit"] == "ms";
  for (JsonVariant v : values)
    if (v["key"] == "valve_time") valued = v["value"] == 300;
  TEST_ASSERT_TRUE_MESSAGE(listed && valued, "listed under the alias with its label and unit");
  // the generic name stays accepted
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_params","values":[{"key":"out1_on_ms","value":310}]})").isOk());
  TEST_ASSERT_EQUAL_STRING("valve_time=310", s.serial("valve_time").c_str());
  // serial
  TEST_ASSERT_EQUAL_STRING("valve_time=320", s.serial("valve_time=320").c_str());
  TEST_ASSERT_EQUAL_STRING("out1_on_ms=320", s.serial("out1_on_ms").c_str());
  // MQTT, under the new prefix
  auto subs = s.ctl().mqttSubscriptions();
  const std::string pre = "myprop/fx";
  auto r = s.ctl().mqttOnMessage(pre + "/config/set/valve_time", "330");
  TEST_ASSERT_TRUE(r.size() == 1 && r[0].topic == pre + "/config/valve_time" && r[0].payload == "330");
  r = s.ctl().mqttOnMessage(pre + "/config/get/valve_time", "");
  TEST_ASSERT_EQUAL_STRING("330", r[0].payload.c_str());
  TEST_ASSERT_EQUAL(330, s.ctl().settings().fx.out1_on_ms);
  // a control alias: web, MQTT, serial
  s.serial("remote_trigger_enable=true");
  TEST_ASSERT_TRUE(s.request(R"({"op":"action","key":"go"})").isOk());
  s.step(3000);
  s.ctl().mqttOnMessage(pre + "/control/go", "");
  s.step(8000);
  TEST_ASSERT_EQUAL_STRING("ok", s.serial("go").c_str());
  s.step(8000);
  TEST_ASSERT_EQUAL(3, runs(s, 0));
  // status under the alias name: the state's status lines and MQTT
  TEST_ASSERT_EQUAL(3, statusInt(s, "presses"));
  Value v;
  TEST_ASSERT_FALSE_MESSAGE(s.status("run_count", v), "published under the alias only");
  auto hello = s.ctl().mqttOnConnect();
  TEST_ASSERT_NOT_NULL(find(hello, pre + "/status/presses"));
  TEST_ASSERT_EQUAL_STRING("3", find(hello, pre + "/status/presses")->payload.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(find(hello, pre + "/config/valve_time"), "settings published under the alias");
  TEST_ASSERT_NULL(find(hello, pre + "/config/out1_on_ms"));
  // the action list keeps the generic keys; the alias works as a key
  bool trigger = false;
  for (auto& a : s.ctl().info().actions) trigger |= a.key == "trigger";
  TEST_ASSERT_TRUE(trigger);
}

void E28_SM_AL3_inverted_and_scaled_aliases() {
  FxSim s;
  TEST_ASSERT_TRUE(load(s, TABLE).isOk());
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_params","values":[{"key":"skip_after_output","value":true}]})").isOk());
  TEST_ASSERT_FALSE_MESSAGE(s.ctl().settings().fx.out3_enabled, "E28: the inverted alias set true, its key reads false");
  TEST_ASSERT_EQUAL_STRING("skip_after_output=false", s.serial("skip_after_output=false").c_str());
  TEST_ASSERT_TRUE(s.ctl().settings().fx.out3_enabled);
  // scale: seconds for a time in ms; the range in the alias's units
  TEST_ASSERT_EQUAL_STRING("rest_s=7", s.serial("rest_s=7").c_str());
  TEST_ASSERT_EQUAL(7000, s.ctl().settings().fx.out2_on_ms);
  Response r = s.request(R"({"op":"set_params","values":[{"key":"rest_s","value":90}]})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("rest_s: Rest is set to 90 s but the board allows 0-60 s; lower it in Settings") == 0, r.errorMessage().c_str());
  TEST_ASSERT_EQUAL(7000, s.ctl().settings().fx.out2_on_ms);
}

void E29_SM_AL4_bad_tables_are_refused_and_nothing_changes() {
  FxSim s;
  TEST_ASSERT_TRUE(load(s, TABLE).isOk());
  std::string before = settingsJson(s);
  Response p0 = s.request(R"({"op":"get_profile"})");
  struct Bad { const char* table; const char* reason; };
  const Bad bad[] = {
      {R"({"aliases":[{"alias":"a","key":"out1_on_ms"},{"alias":"a","key":"out2_on_ms"}]})", "used twice"},
      {R"({"aliases":[{"alias":"a","key":"no_such_key"}]})", "there is no setting \"no_such_key\""},
      {R"({"aliases":[{"alias":"secret","key":"pin"}]})", "set-only"},
      {R"({"aliases":[{"alias":"secret","key":"wifi_pass"}]})", "set-only"},
      {R"({"aliases":[{"alias":"out2_on_ms","key":"out1_on_ms"}]})", "already a setting"},
      {R"({"aliases":[{"alias":"x","key":"out1_on_ms","invert":true}]})", "invert is for on/off settings"},
      {R"({"aliases":[{"alias":"x","control":"launch"}]})", "there is no control launch"},
      {R"({"aliases":[{"alias":"x","status":"nope"}]})", "there is no status value nope"},
      {R"({"aliases":[{"alias":"x"}]})", "exactly one of"},
      {R"({"aliases":[{"alias":"OUT2","terminal":"OUT1"}]})", "that is the name of a terminal"},
      {R"({"aliases":[{"alias":"broadcast","key":"out1_on_ms"}]})", "broadcast"},
      {R"({"topic_prefix":"a/b"})", "topic_prefix"},
  };
  for (auto& b : bad) {
    Response r = load(s, b.table);
    TEST_ASSERT_FALSE_MESSAGE(r.isOk(), b.table);
    TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find(b.reason) != std::string::npos, r.errorMessage().c_str());
  }
  Response p1 = s.request(R"({"op":"get_profile"})");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(p0.json().c_str(), p1.json().c_str(), "the loaded table stays");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), settingsJson(s).c_str(), "no value changed");
}

void E30_SM_AL6_topic_prefix_everywhere() {
  FxSim s;
  s.serial("mqtt_broker=127.0.0.1");
  auto subs = s.ctl().mqttSubscriptions();
  TEST_ASSERT_EQUAL_STRING("proptroller/broadcast/getName", subs[0].c_str());
  uint32_t epoch = s.ctl().mqttEpoch();
  TEST_ASSERT_TRUE(load(s, R"({"topic_prefix":"myprop"})").isOk());
  s.ctl().mqttPoll();
  TEST_ASSERT_TRUE_MESSAGE(s.ctl().mqttEpoch() != epoch, "a new prefix ends the session, as a rename does");
  subs = s.ctl().mqttSubscriptions();
  for (auto& f : subs) TEST_ASSERT_TRUE_MESSAGE(f.compare(0, 7, "myprop/") == 0, f.c_str());
  TEST_ASSERT_EQUAL_STRING("myprop/fx/online", s.ctl().mqttWill().topic.c_str());
  auto r = s.ctl().mqttOnMessage("myprop/broadcast/getName", "");
  TEST_ASSERT_TRUE(r.size() == 1 && r[0].topic == "myprop/broadcast/name" && r[0].payload == "fx");
  TEST_ASSERT_TRUE_MESSAGE(s.ctl().mqttOnMessage("proptroller/broadcast/getName", "").empty(), "the old prefix is ignored");
  TEST_ASSERT_TRUE(s.ctl().mqttOnMessage("proptroller/fx/config/set/out1_on_ms", "500").empty());
  TEST_ASSERT_EQUAL(1000, s.ctl().settings().fx.out1_on_ms);
  Response p = s.request(R"({"op":"get_profile"})");
  TEST_ASSERT_EQUAL_STRING("myprop", p.doc()["result"]["topic_prefix"] | "");
}

void E31_SM_AL0_terminal_aliases() {
  Sim s("t");  // the effect off: TRIG2 and OUT1 free for shows
  TEST_ASSERT_TRUE(load(s, TABLE).isOk());
  // a show triggered by REFILL, pulsing VALVE: stored under the board names
  Response r = s.request(R"({"op":"put_show","data":{"id":5,"name":"Refill","triggers":[{"on":"input","input":"REFILL"}],
    "tracks":[{"target":"VALVE","cues":[{"at":0,"do":"pulse","ms":500}]}]}})");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  Response g = s.request(R"({"op":"get_show","show":5})");
  TEST_ASSERT_EQUAL_STRING("TRIG2", g.doc()["result"]["data"]["triggers"][0]["input"] | "");
  TEST_ASSERT_EQUAL_STRING("OUT1", g.doc()["result"]["data"]["tracks"][0]["target"] | "");
  s.step(2000);
  Millis t = s.now();
  s.press(Input::Trig2, TAP);
  s.step(700);
  TEST_ASSERT_EQUAL_MESSAGE(1, runs(s, t), "works exactly as TRIG2 / OUT1");
  // a manual test of VALVE; the board name keeps working
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_output","channel":"VALVE","level":1000,"ms":600000})").isOk());
  s.step(20);
  TEST_ASSERT_EQUAL(1000, s.level(Channel::Out1));
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_output","channel":"OUT1"})").isOk());
  // displays: "REFILL (TRIG2)" on the wiring view and in the events
  bool shown = false;
  for (auto& term : s.ctl().info().terminals)
    if (term.id == "TRIG2") shown = term.alias == "REFILL" && term.display() == "REFILL (TRIG2)";
  TEST_ASSERT_TRUE(shown);
  std::vector<std::string> ev;
  s.ctl().eventsSince(0, ev);
  bool in_events = false;
  for (auto& e : ev) in_events |= e.find("REFILL (TRIG2) pressed") != std::string::npos;
  TEST_ASSERT_TRUE_MESSAGE(in_events, "the press is logged with the alias");
  // case does not matter for terminal names; an alias equal to another terminal is refused
  TEST_ASSERT_TRUE(s.request(R"({"op":"sim_input","input":"refill","active":false})").isOk());
  Response bad = load(s, R"({"aliases":[{"alias":"OUT2","terminal":"TRIG2"}]})");
  TEST_ASSERT_TRUE(bad.errorMessage().find("that is the name of a terminal") != std::string::npos);
}

void E31_beginner_layer_when_REFILL() {
  Proptroller prop;
  prop.begin();
  TEST_ASSERT_TRUE(prop.advanced().loadProfile(R"({"aliases":[{"alias":"REFILL","terminal":"TRIG2"},{"alias":"VALVE","terminal":"OUT1"}]})"));
  TEST_ASSERT_EQUAL(TRIG2, prop.advanced().input("REFILL"));
  TEST_ASSERT_EQUAL(TRIG2, prop.advanced().input("TRIG2"));
  prop.advanced().when("REFILL").turnOn(OUT1, seconds(1));
  Sim& sim = prop.hostSim();
  for (int i = 0; i < 200; i++) prop.update();
  Millis t = sim.now();
  sim.setInput(Input::Trig2, true);
  for (int i = 0; i < 20; i++) prop.update();
  sim.setInput(Input::Trig2, false);
  for (int i = 0; i < 150; i++) prop.update();
  TEST_ASSERT_EQUAL(1, runs(sim, t));
  TEST_ASSERT_FALSE(prop.advanced().loadProfile(R"({"aliases":[{"alias":"OUT2","terminal":"TRIG2"}]})"));
}

void SM_AL5_stored_kept_through_a_power_cut_and_values_untouched() {
  FxSim s;
  s.serial("out1_on_ms=444");
  std::string before = settingsJson(s);
  TEST_ASSERT_TRUE(load(s, TABLE).isOk());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), settingsJson(s).c_str(), "loading changes no value");
  s.step(2000);
  s.powerCycle(200);
  Response p = s.request(R"({"op":"get_profile"})");
  TEST_ASSERT_EQUAL(7, p.doc()["result"]["aliases"].size());
  TEST_ASSERT_EQUAL_STRING("valve_time=444", s.serial("valve_time").c_str());
  TEST_ASSERT_TRUE(load(s, R"({"aliases":[]})").isOk());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), settingsJson(s).c_str(), "removing changes no value");
  TEST_ASSERT_TRUE(s.serial("valve_time").find("error") == 0);
}

// fuzz (profile): a scale near the int64 limit overflowed working out the alias's range (UB); now the range is
// empty and every interface refuses a value instead
void SM_AL3_a_huge_scale_has_an_empty_range() {
  FxSim s;
  TEST_ASSERT_TRUE(load(s, R"({"aliases":[{"alias":"big","key":"out1_on_ms","scale":9223372036854775807}]})").isOk());
  Response g = s.request(R"({"op":"get_params"})");
  TEST_ASSERT_TRUE(g.isOk());
  uint32_t before = s.ctl().settings().fx.out1_on_ms;
  TEST_ASSERT_FALSE(s.request(R"({"op":"set_params","values":[{"key":"big","value":1}]})").isOk());
  TEST_ASSERT_TRUE(s.serial("big=1").find("error") == 0);
  auto m = s.ctl().mqttOnMessage(s.ctl().mqttPrefix() + "/config/set/big", "1");
  TEST_ASSERT_EQUAL(1, m.size());
  TEST_ASSERT_TRUE_MESSAGE(m[0].topic.find("/error") != std::string::npos, m[0].payload.c_str());
  TEST_ASSERT_EQUAL(before, s.ctl().settings().fx.out1_on_ms);
  TEST_ASSERT_EQUAL(0, pt::aliasMin(0, 1000));
  TEST_ASSERT_EQUAL(1, pt::aliasMin(1, 1000));
  TEST_ASSERT_EQUAL(-1, pt::aliasMin(-1999, 1000));
  TEST_ASSERT_EQUAL(-2, pt::aliasMax(-1001, 1000));
  TEST_ASSERT_EQUAL(60, pt::aliasMax(60999, 1000));
}

void SM_P2_the_page_lock_guards_set_profile() {
  FxSim s;
  s.request(R"({"op":"set_params","values":[{"key":"pin","value":"door-42"},{"key":"page_lock","value":true}]})");
  Response r = load(s, TABLE);
  TEST_ASSERT_EQUAL_STRING("pin", code(r).c_str());
  TEST_ASSERT_TRUE(s.request(R"({"op":"get_profile"})").isOk());
  TEST_ASSERT_TRUE(s.request(std::string(R"({"op":"set_profile","pin":"door-42","data":)") + TABLE + "}").isOk());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(E27_SM_AL1_SM_AL2_every_alias_on_every_interface);
  RUN_TEST(E28_SM_AL3_inverted_and_scaled_aliases);
  RUN_TEST(E29_SM_AL4_bad_tables_are_refused_and_nothing_changes);
  RUN_TEST(E30_SM_AL6_topic_prefix_everywhere);
  RUN_TEST(E31_SM_AL0_terminal_aliases);
  RUN_TEST(E31_beginner_layer_when_REFILL);
  RUN_TEST(SM_AL5_stored_kept_through_a_power_cut_and_values_untouched);
  RUN_TEST(SM_P2_the_page_lock_guards_set_profile);
  RUN_TEST(SM_AL3_a_huge_scale_has_an_empty_range);
  return UNITY_END();
}
