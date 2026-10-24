#include <cstdlib>
// Protocol golden test: one scripted session on the C++ simulator; every message must match the committed
// golden files in test/golden byte for byte. The files are the contract (PROTOCOL.md); this test writes them. With GOLDEN_WRITE set
// to a directory (tools/golden.sh) every message is written there instead of compared.
#include <unity.h>
#include <fstream>
#include <sstream>
#include <string.h>
#include "proptroller/script_record.h"
#include "proptroller/sim/sim.h"
#include "proptroller/password.h"
#include <algorithm>

using namespace pt;
void setUp() {}
void tearDown() {}

static int n = 0;
static int compared = 0;
static std::string goldenName(const std::string& name) {
  char b[16];
  snprintf(b, sizeof b, "%02d-", ++n);
  return std::string(b) + name + ".json";
}
static void check(const std::string& name, const std::string& got) {
  std::string file = goldenName(name);
  compared++;
  if (const char* dir = getenv("GOLDEN_WRITE")) {
    std::ofstream(std::string(dir) + "/" + file, std::ios::binary) << got;
    return;
  }
  std::ifstream f("test/golden/" + file, std::ios::binary);
  if (!f.good()) TEST_FAIL_MESSAGE(("missing golden file " + file).c_str());
  std::stringstream ss;
  ss << f.rdbuf();
  std::string want = ss.str();
  if (want != got) {
    size_t i = 0;
    while (i < want.size() && i < got.size() && want[i] == got[i]) i++;
    size_t from = i > 80 ? i - 80 : 0;
    std::string m = name + " differs at byte " + std::to_string(i) + ": want ..." + want.substr(from, 200) + " got ..." + got.substr(from, 200);
    TEST_FAIL_MESSAGE(m.c_str());
  }
}

#define CALL(name, req) do { std::string q = req; check(name, sim.call(q.data(), q.size())); } while (0)
#define CALL_S(name, req) do { std::string q = req; check(name, sim.callFrom(1, q.data(), q.size())); } while (0)  // a phone page
#define PUT(name, s) check(name, s)

// ---------------------------------------------------------------- the show script (proptroller_sim::golden)
static const char* const SC_SCRIPT =
    "effect porch light:\n  wait for trig1\n  out1 = on\n  audio_single = 3\n  sleep 2\n  fade out1 to 0% over 1\n\n"
    "effect prop:\n  wait for trig2\n  rly1 = on   # the prop's try-me button\n  sleep 0.5\n  rly1 = off\n";
static const char* const SC_BAD_TEXT = "effect a:\n  wait for trig1\n  out5 = on\n";

static std::string withScript(int id, const char* op, const char* text) {
  JsonDocument d;
  d["id"] = id;
  d["op"] = op;
  d["text"] = text;
  d["sounds"]["chime.mp3"] = 3;
  std::string j;
  serializeJson(d, j);
  return j;
}

// the MQTT messages one incoming message gives: [{"topic","payload"}], a JSON payload as it is, text as a string
static std::string mqttJson(Sim& sim, const std::string& topic, const std::string& payload) {
  std::string out = "[";
  bool first = true;
  for (auto& m : sim.ctl().mqttOnMessage(topic, payload)) {
    if (!first) out += ",";
    first = false;
    JsonDocument t;
    t.set(m.topic);
    std::string ts;
    serializeJson(t, ts);
    out += "{\"topic\":" + ts + ",\"payload\":";
    JsonDocument pj;
    if (!deserializeJson(pj, m.payload)) out += m.payload;
    else {
      JsonDocument ps;
      ps.set(m.payload);
      std::string x;
      serializeJson(ps, x);
      out += x;
    }
    out += "}";
  }
  return out + "]";
}

static void scriptSession(Sim& sim) {
  CALL("sc-effect-off", R"__({"id":39,"op":"set_params","values":[{"key":"effect","value":"none"}]})__");
  CALL("sc-get-none", R"__({"id":40,"op":"get_script"})__");
  CALL("sc-check-errors", R"__({"id":41,"op":"check_script","text":"out1 = on\neffect fog\n  wait for trig9\n  rly1 = 50%\n  fade rly1 to bright over soon\n  sleep later\n"})__");
  CALL("sc-check-ok", withScript(42, "check_script", SC_SCRIPT));
  CALL("sc-set-refused", R"__({"id":43,"op":"set_script","text":"effect a:\n  wait for trig1\n  out5 = on\n"})__");
  CALL("sc-set", withScript(44, "set_script", SC_SCRIPT));
  CALL("sc-get", R"__({"id":45,"op":"get_script"})__");
  CALL("sc-params-refused", R"__({"id":46,"op":"set_params","values":[{"key":"rly1_role","value":"none"}]})__");
  sim.step(3000);
  sim.press(Input::Trig1, 200);
  sim.step(500);
  CALL("sc-state-running", R"__({"id":47,"op":"state"})__");
  sim.step(3000);
  std::string p = sim.ctl().mqttPrefix();
  PUT("sc-mqtt-check", mqttJson(sim, p + "/script/check", R"__({"text":"effect a:\n  wait for trig1\n  out5 = on\n"})__"));
  {
    JsonDocument d;
    d["sounds"]["chime.mp3"] = 3;
    d["text"] = SC_SCRIPT;
    std::string j;
    serializeJson(d, j);
    PUT("sc-mqtt-set", mqttJson(sim, p + "/script/set", j));
  }
  PUT("sc-mqtt-bad", mqttJson(sim, p + "/script/set", "not json"));
  CALL("sc-lock", R"__({"id":48,"op":"set_params","values":[{"key":"pin","value":"654321"},{"key":"page_lock","value":true}]})__");
  CALL("sc-set-locked", R"__({"id":49,"op":"set_script","text":""})__");
  PUT("sc-mqtt-locked", mqttJson(sim, p + "/script/set", R"__({"text":""})__"));
  CALL("sc-unlock", R"__({"id":50,"op":"set_params","pin":"654321","values":[{"key":"page_lock","value":false}]})__");
  CALL("sc-set-empty", R"__({"id":51,"op":"set_script","text":"# nothing yet\n"})__");
  CALL("sc-get-after-empty", R"__({"id":52,"op":"get_script"})__");
  CALL("sc-state-none", R"__({"id":53,"op":"state"})__");
}

// a fresh board whose stored script fails the power-on check
static void scriptBootError() {
  Sim sim("a0b1c2d3e4f5");
  TEST_ASSERT_TRUE(sim.scriptFlash().save(scriptRecordEncode(SC_BAD_TEXT, "", nullptr, 0)));
  sim.powerCycle(0);
  CALL("sc-boot-error-state", R"__({"id":60,"op":"state"})__");
  CALL("sc-boot-error-get", R"__({"id":61,"op":"get_script"})__");
}

// ---------------------------------------------------------------- backups and updates (proptroller_sim::golden::fleet_session)
static std::string withData(int id, const std::string& data_json, const char* extra = "") {
  return "{\"id\":" + std::to_string(id) + ",\"op\":\"put_backup\",\"data\":" + data_json + extra + "}";
}

// the backup document with one top-level field replaced
static std::string withField(const std::string& data_json, const char* key, const char* value_json) {
  JsonDocument d, v;
  deserializeJson(d, data_json);
  deserializeJson(v, value_json);
  d[key] = v.as<JsonVariantConst>();
  std::string s;
  serializeJson(d, s);
  return s;
}

static std::string shaHex(const std::string& b) {
  uint8_t d[32];
  sha256((const uint8_t*)b.data(), b.size(), d);
  return toHex(d, 32);
}

static std::string beginJson(Sim& sim, bool has_len, size_t len, bool octet, const std::string& sha) {
  UpdateRequest q;
  q.has_len = has_len;
  q.len = len;
  q.octet_stream = octet;
  q.has_sha256 = true;
  q.sha256 = sha;
  std::string answer;
  return sim.ctl().updateBegin(q, answer) ? "null" : answer;
}

// one update as the HTTP layer drives it: 1024-byte writes; the answer of the first step that refuses, else the finish
static std::string sendImage(Sim& sim, const std::string& image, const std::string& sha) {
  UpdateRequest q;
  q.has_len = true;
  q.len = image.size();
  q.octet_stream = true;
  q.has_sha256 = true;
  q.sha256 = sha;
  std::string answer;
  if (!sim.ctl().updateBegin(q, answer)) return answer;
  for (size_t i = 0; i < image.size(); i += 1024)
    if (!sim.ctl().updateWrite((const uint8_t*)image.data() + i, std::min<size_t>(1024, image.size() - i), answer)) return answer;
  return sim.ctl().updateFinish();
}

static std::string eventsAfter(Sim& sim, uint32_t seq) {
  std::vector<std::string> ev;
  sim.ctl().eventsSince(seq, ev);
  std::string all;
  for (size_t i = 0; i < ev.size(); i++) all += (i ? "\n" : "") + ev[i];
  return all;
}

// a second board takes the backup
static void otherBoardSession(const std::string& data) {
  Sim sim("b0b1b2b3b4b5");
  CALL("fl-put-backup", withData(73, data));
  CALL("fl-get-backup-restored", R"__({"id":74,"op":"get_backup"})__");
  CALL("fl-put-backup-name", withData(75, data, ",\"keep_name\":false"));
  CALL("fl-put-not-backup", withData(76, withField(data, "format", "\"something-else\"")));
  CALL("fl-put-newer", withData(77, withField(data, "format_version", "2")));
  CALL("fl-put-bad-setting", withData(78, withField(data, "settings", R"([{"key":"trig_debounce_ms","value":5000}])")));
  CALL("fl-put-bad-show", withData(79, withField(data, "shows", R"([{"id":4,"name":"Bad","tracks":[{"target":"RLY1","cues":[{"at":0,"do":"level","level":500}]}]}])")));
  CALL("fl-put-bad-script", withData(80, withField(data, "script", R"({"text":"effect a:\n  wait for trig1\n  out5 = on\n","sounds":{}})")));
  CALL("fl-put-skipped", withData(81, withField(data, "settings", R"([{"key":"nope","value":1},{"key":"wifi_ssid","value":"x"},{"key":"trig_release_ms","value":150}])")));
  CALL("fl-lock", R"__({"id":82,"op":"set_params","values":[{"key":"pin","value":"654321"},{"key":"page_lock","value":true}]})__");
  CALL("fl-put-locked", withData(83, data));
}

static void fleetSession(Sim& sim) {
  CALL("fl-set-script", withScript(70, "set_script", SC_SCRIPT));
  CALL("fl-set-params", R"__({"id":71,"op":"set_params","values":[{"key":"out2_name","value":"fog machine"},{"key":"wifi_ssid","value":"show-net"},{"key":"wifi_pass","value":"secret-pass"}]})__");
  CALL("fl-get-backup", R"__({"id":72,"op":"get_backup"})__");
  std::string data;
  {
    JsonDocument b;
    std::string q = R"({"op":"get_backup"})";
    deserializeJson(b, sim.call(q.data(), q.size()));
    serializeJson(b["result"]["data"], data);
  }
  otherBoardSession(data);
  // updates on the first board
  std::string good = sim_image::build("0.2.0", "new0001", true, 300 * 1024);
  std::string failing = sim_image::build("0.2.1", "bad0001", false, 300 * 1024);
  TEST_ASSERT_EQUAL(307298, good.size());
  std::string sha = shaHex(good);
  uint32_t seq = sim.ctl().events().seq();
  PUT("up-no-length", beginJson(sim, false, 0, true, sha));
  PUT("up-content-type", beginJson(sim, true, good.size(), false, sha));
  PUT("up-bad-sha-header", beginJson(sim, true, good.size(), true, "1234"));
  PUT("up-too-big", beginJson(sim, true, 3000000, true, sha));
  PUT("up-empty", beginJson(sim, true, 0, true, sha));
  CALL("up-manual-on", R"__({"id":84,"op":"set_output","channel":"OUT3","level":500,"ms":600000})__");
  PUT("up-refused-manual", beginJson(sim, true, good.size(), true, sha));
  CALL("up-stop-all", R"__({"id":85,"op":"stop_all"})__");
  std::string esp(4096, '\0');
  esp[0] = (char)0xE9;
  PUT("up-wrong-board", sendImage(sim, esp, shaHex(esp)));
  PUT("up-bad-sha", sendImage(sim, good, shaHex("something else")));
  {
    std::string answer;
    UpdateRequest q;
    q.has_len = true;
    q.len = good.size();
    q.octet_stream = true;
    q.has_sha256 = true;
    q.sha256 = sha;
    TEST_ASSERT_TRUE(sim.ctl().updateBegin(q, answer));
    TEST_ASSERT_TRUE(sim.ctl().updateWrite((const uint8_t*)good.data(), 200 * 1024, answer));
  }
  sim.step(100);
  PUT("up-state-writing", sim.stateJson());
  CALL("up-busy-run", R"__({"id":86,"op":"run","show":2})__");
  PUT("up-again", beginJson(sim, true, good.size(), true, sha));
  sim.ctl().updateAbort();
  PUT("up-events-failed", eventsAfter(sim, seq));
  seq = sim.ctl().events().seq();
  PUT("up-done", sendImage(sim, good, sha));
  PUT("up-state-restarting", sim.stateJson());
  PUT("up-events-done", eventsAfter(sim, seq));
  sim.step(1100);
  CALL("up-info-pending", R"__({"id":87,"op":"info"})__");
  sim.step(UPDATE_SELF_CHECK_MS);
  CALL("up-info-valid", R"__({"id":88,"op":"info"})__");
  PUT("up-done-failing", sendImage(sim, failing, shaHex(failing)));
  sim.step(1100);
  CALL("up-info-failing-pending", R"__({"id":89,"op":"info"})__");
  sim.step(UPDATE_SELF_CHECK_MS + 500);
  CALL("up-info-rolled-back", R"__({"id":90,"op":"info"})__");
}

// ---------------------------------------------------------------- add / replace / delete, link
// On a fresh board (no shows): the example added on request, an "on while held" link show, replace, delete.
static void showsSession() {
  Sim sim("a0b1c2d3e4f5");
  sim.step(500);
  CALL("sh-add-example", R"__({"op":"add_example"})__");
  CALL("sh-put-link", R"__({"op":"put_show","data":{"id":2,"name":"Link: TRIG1 -> OUT2","triggers":[{"on":"input","input":"TRIG1","until_release":true}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"on"}]}],"length_ms":60000}})__");
  CALL("sh-list", R"__({"op":"list_shows"})__");
  CALL("sh-get-link", R"__({"op":"get_show","show":2})__");
  CALL("sh-bad-until-release", R"__({"op":"put_show","data":{"id":3,"name":"x","triggers":[{"on":"release","input":"TRIG1","until_release":true}]}})__");
  sim.setInput(Input::Trig1, true);
  sim.step(500);
  CALL("sh-state-held", R"__({"op":"state"})__");
  sim.setInput(Input::Trig1, false);
  sim.step(200);
  CALL("sh-state-released", R"__({"op":"state"})__");
  CALL("sh-put-replace", R"__({"op":"put_show","data":{"id":2,"name":"Two","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"OUT3","cues":[{"at":0,"do":"on"},{"at":300,"do":"off"}]}]}})__");
  CALL("sh-delete", R"__({"op":"delete_show","show":2})__");
  CALL("sh-delete-again", R"__({"op":"delete_show","show":2})__");
  std::vector<std::string> ev;
  sim.ctl().eventsSince(0, ev);
  std::string all;
  for (size_t i = 0; i < ev.size(); i++) all += (i ? "\n" : "") + ev[i];
  check("sh-events", all);
}

// ---------------------------------------------------------------- the network list (op wifi_scan)
// On a fresh board: the first ask starts a scan, the simulator's fixed list comes in SIM_SCAN_MS later, a rescan
// too soon is refused with the time left, a mistyped field is a bad request.
static void wifiSession() {
  Sim sim("a0b1c2d3e4f5");
  sim.step(500);
  CALL("wf-scan-start", R"__({"id":90,"op":"wifi_scan"})__");
  sim.step(SIM_SCAN_MS + 100);
  CALL("wf-scan", R"__({"id":91,"op":"wifi_scan"})__");
  sim.step(1000);
  CALL("wf-rescan-soon", R"__({"id":92,"op":"wifi_scan","rescan":true})__");
  CALL("wf-bad-rescan", R"__({"id":93,"op":"wifi_scan","rescan":1})__");
}

// ---------------------------------------------------------------- board packages (get_board / set_board)
// On a fresh board: what runs and the presets, a built-in exported, a package refused in plain words, one stored
// (applied by the restart), the board after the restart, a preset, forget, and the bad requests.
static void boardSession() {
  Sim sim("a0b1c2d3e4f5");
  sim.step(500);
  CALL("bd-get", R"__({"id":100,"op":"get_board"})__");
  CALL("bd-get-preset", R"__({"id":101,"op":"get_board","preset":"relay4-button"})__");
  CALL("bd-get-unknown", R"__({"id":102,"op":"get_board","preset":"big-board"})__");
  CALL("bd-set-bad-pin", R"__({"id":103,"op":"set_board","package":{"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"OUT3":{"pin":35}}}})__");
  CALL("bd-set-bad-format", R"__({"id":104,"op":"set_board","package":{"board_package":2,"id":"x","name":"X"}})__");
  CALL("bd-set-two", R"__({"id":105,"op":"set_board","preset":"proptroller","forget":true})__");
  CALL("bd-set", R"__({"id":106,"op":"set_board","package":{"board_package":1,"id":"relay-board","name":"Relay board","mcu":"esp32-devkit","terminals":{"OUT1":{"pin":23},"TRIG1":{"pin":36,"pull":"board"},"TRIG2":{"pin":18,"active":"high","pull":"board"},"USER1":{"pin":5}}},"defaults":"keep"})__");
  CALL("bd-get-pending", R"__({"id":107,"op":"get_board"})__");
  CALL("bd-reboot", R"__({"id":108,"op":"reboot"})__");
  sim.step(500);
  CALL("bd-info-after", R"__({"id":109,"op":"info"})__");
  CALL("bd-get-after", R"__({"id":110,"op":"get_board"})__");
  CALL("bd-set-preset", R"__({"id":111,"op":"set_board","preset":"relay4-button","defaults":"apply"})__");
  CALL("bd-forget", R"__({"id":112,"op":"set_board","forget":true})__");
  CALL("bd-defaults-pending", R"__({"id":113,"op":"get_defaults"})__");
  CALL("bd-apply-defaults-pending", R"__({"id":114,"op":"apply_defaults","only":["script"]})__");
  CALL("bd-apply-defaults-bad", R"__({"id":115,"op":"apply_defaults","only":["wifi"]})__");
  CALL("bd-reset-keep-wifi-bad", R"__({"id":116,"op":"factory_reset","keep_wifi":"yes"})__");
}

static void testInputSession() {
  // a test press (op test_input): shown on the input; with remote start off the example show does not start,
  // with it on the show runs as from a real press; letting go; the request's checks
  Sim sim("a0b1c2d3e4f5");
  sim.addExample();
  sim.step(2500);
  CALL_S("ti-press", R"__({"id":120,"op":"test_input","input":"TRIG1","active":true})__");
  sim.step(300);
  PUT("ti-state-shown", sim.stateJson());
  CALL_S("ti-let-go", R"__({"id":121,"op":"test_input","input":"TRIG1","active":false})__");
  sim.step(300);
  CALL_S("ti-remote-on", R"__({"id":122,"op":"set_params","values":[{"key":"remote_trigger_enable","value":true}]})__");
  CALL_S("ti-press-starts", R"__({"id":123,"op":"test_input","input":"TRIG1","active":true,"ms":300})__");
  sim.step(500);
  PUT("ti-state-running", sim.stateJson());
  CALL("ti-bad-input", R"__({"id":124,"op":"test_input","input":"OUT1","active":true})__");
  CALL("ti-bad-ms", R"__({"id":125,"op":"test_input","input":"TRIG1","active":true,"ms":30001})__");
  CALL("ti-no-active", R"__({"id":126,"op":"test_input","input":"TRIG1"})__");
}

void session_matches_the_golden_files() {
  Sim sim("a0b1c2d3e4f5");
#include "session.inc"
  std::vector<std::string> ev;
  sim.ctl().eventsSince(0, ev);
  std::string all;
  for (size_t i = 0; i < ev.size(); i++) all += (i ? "\n" : "") + ev[i];
  check("events", all);
  TEST_ASSERT_EQUAL(66, compared);
  scriptSession(sim);
  scriptBootError();
  TEST_ASSERT_EQUAL(87, compared);
  fleetSession(sim);
  TEST_ASSERT_EQUAL(123, compared);
  showsSession();
  TEST_ASSERT_EQUAL(134, compared);
  wifiSession();
  TEST_ASSERT_EQUAL(138, compared);
  boardSession();
  TEST_ASSERT_EQUAL(155, compared);
  testInputSession();
  TEST_ASSERT_EQUAL(164, compared);
}


// ---------------------------------------------------------------- proptroller-proto tests
static bool parses(const char* j, Request& r) {
  Response* e = nullptr;
  bool ok = parseRequest(j, strlen(j), r, e);
  delete e;
  return ok;
}
static std::string errCode(const char* j, bool& has_id, uint32_t& id) {
  Request r;
  Response* e = nullptr;
  if (parseRequest(j, strlen(j), r, e)) return "";
  std::string c = e->doc()["error"]["code"].as<std::string>();
  has_id = !e->doc()["id"].isNull();
  id = e->doc()["id"] | 0;
  delete e;
  return c;
}

void request_wire_format() {
  Request r;
  TEST_ASSERT_TRUE(parses(R"({"v":1,"id":7,"op":"run","show":3})", r));
  TEST_ASSERT_TRUE(r.has_id && r.id == 7 && r.op == "run" && r["show"] == 3);
  Request r2;
  TEST_ASSERT_TRUE(parses(R"({"op":"set_output","channel":"OUT2","level":500})", r2));
  TEST_ASSERT_EQUAL(1, r2.v);
  Request r3;
  TEST_ASSERT_TRUE(parses(R"({"op":"set_output","channel":"CC1","level":null})", r3));
  Request r4;
  TEST_ASSERT_TRUE(parses(R"({"op":"sound","cmd":"play_folder","folder":1,"track":2})", r4));
  SoundCommand c;
  TEST_ASSERT_TRUE(reqSound(r4, c) && c == SoundCommand::playFolder(1, 2));
  Request r5;
  TEST_ASSERT_TRUE(parses(R"({"op":"pixel","action":{"do":"fill","color":"#00ff00"}})", r5));
  Request r6;
  TEST_ASSERT_FALSE(parses(R"({"op":"record_start","show":2,"target":"PIXEL"})", r6));  // no such op any more
  Request r7;
  TEST_ASSERT_TRUE(parses(R"({"op":"set_params","values":[{"key":"name","value":"Door"},{"key":"supply_mv","value":12000},{"key":"x","value":true}]})", r7));
  Request r8;
  TEST_ASSERT_TRUE(parses(R"({"op":"put_show","pin":"1234","data":{"id":4,"name":"Prop","tracks":[{"target":"RLY1","cues":[{"at":0,"do":"pulse","ms":3000}]}]}})", r8));
  TEST_ASSERT_TRUE(r8.has_pin && r8.pin == "1234" && r8.mutates());
  Request r9;
  parses(R"({"op":"state"})", r9);
  TEST_ASSERT_FALSE(r9.mutates());
}

void bad_requests() {
  bool has_id;
  uint32_t id;
  TEST_ASSERT_EQUAL_STRING("bad_request", errCode("not json", has_id, id).c_str());
  TEST_ASSERT_EQUAL_STRING("bad_request", errCode(R"({"op":"launch"})", has_id, id).c_str());
  TEST_ASSERT_EQUAL_STRING("bad_request", errCode(R"({"op":"run"})", has_id, id).c_str());
  TEST_ASSERT_EQUAL_STRING("bad_request", errCode(R"({"op":"set_output","channel":"OUT9"})", has_id, id).c_str());
  TEST_ASSERT_EQUAL_STRING("version", errCode(R"({"v":2,"id":1,"op":"info"})", has_id, id).c_str());
  TEST_ASSERT_TRUE(has_id && id == 1);
}

void host_labels() {
  TEST_ASSERT_EQUAL_STRING("front-door-2", hostLabel("Front Door #2", "x").c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller-c2d3", hostLabel("  ", "a0b1c2d3").c_str());
  TEST_ASSERT_EQUAL_STRING("fog-machine", hostLabel("Fog_Machine", "").c_str());
}

void param_checks() {
  ParamDesc p = ParamDesc::integer("x", "X", "G", 0, 10, "");
  TEST_ASSERT_NULL(p.check(Value::integer(5)));
  TEST_ASSERT_NOT_NULL(p.check(Value::integer(11)));
  TEST_ASSERT_NOT_NULL(p.check(Value::boolean(true)));
  ParamDesc c = ParamDesc::choice("m", "M", "G", {"a", "b"});
  TEST_ASSERT_NULL(c.check(Value::text("b")));
  TEST_ASSERT_NOT_NULL(c.check(Value::text("z")));
  ParamDesc t = ParamDesc::text("n", "N", "G", 3, false);
  TEST_ASSERT_NOT_NULL(t.check(Value::text("abcd")));
}

void event_seq_wraps() {
  EventLog log;
  for (int i = 0; i < 70; i++) log.log("x");
  std::vector<std::string> out;
  TEST_ASSERT_EQUAL(70, log.since(0, out));
  TEST_ASSERT_EQUAL(64, out.size());
  out.clear();
  log.since(68, out);
  TEST_ASSERT_EQUAL(2, out.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(session_matches_the_golden_files);
  RUN_TEST(request_wire_format);
  RUN_TEST(bad_requests);
  RUN_TEST(host_labels);
  RUN_TEST(param_checks);
  RUN_TEST(event_seq_wraps);
  return UNITY_END();
}
