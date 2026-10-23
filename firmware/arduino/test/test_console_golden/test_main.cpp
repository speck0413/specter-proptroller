// The serial console and the MQTT topics (test/golden-console): what the protocol golden files do not cover.
// This test writes the files: with GOLDEN_WRITE_CONSOLE set to a directory (tools/golden.sh) every answer
// is written there instead of compared. A console answer is consoleLine()'s text with each line ended in CR LF;
// MQTT messages one per line as "topic [r] payload".
#include <cstdlib>
#include <unity.h>
#include <fstream>
#include <sstream>
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static int n = 0;
static void check(const std::string& name, const std::string& got) {
  char b[16];
  snprintf(b, sizeof b, "%02d-", ++n);
  std::string file = std::string(b) + name + ".txt";
  if (const char* dir = getenv("GOLDEN_WRITE_CONSOLE")) {
    std::ofstream(std::string(dir) + "/" + file, std::ios::binary) << got;
    return;
  }
  std::ifstream f("test/golden-console/" + file, std::ios::binary);
  if (!f.good()) TEST_FAIL_MESSAGE(("missing golden file " + file).c_str());
  std::stringstream ss;
  ss << f.rdbuf();
  std::string want = ss.str();
  if (want == got) return;
  size_t i = 0;
  while (i < want.size() && i < got.size() && want[i] == got[i]) i++;
  size_t from = i > 60 ? i - 60 : 0;
  std::string m = name + " differs at byte " + std::to_string(i) + ": want ..." + want.substr(from, 160) + " got ..." + got.substr(from, 160);
  TEST_FAIL_MESSAGE(m.c_str());
}

static std::string fileName(std::string l) {
  for (auto& c : l)
    if (c == '=' || c == ' ') c = '_';
  return l;
}
static void console(Sim& sim, const std::string& line) {
  std::string a = sim.ctl().consoleLine(line);
  check("console-" + fileName(line), a.empty() ? a : a + "\r\n");
}
static std::string msgs(const std::vector<MqttMsg>& v) {
  std::string s;
  for (auto& m : v) s += m.topic + (m.retain ? " [r] " : " ") + m.payload + "\n";
  return s;
}
static std::string list(const std::vector<std::string>& v) {
  std::string s;
  for (auto& x : v) s += x + "\n";
  return s;
}
static void mqtt(Sim& sim, const std::string& prefix, const std::string& tag, const std::string& topic, const std::string& payload) {
  std::string t = topic;
  for (auto& c : t)
    if (c == '/') c = '_';
  check(tag + t, msgs(sim.ctl().mqttOnMessage(prefix + "/" + topic, payload)));
}

void console_and_mqtt_match_the_golden_files() {
  Sim sim("a0b1c2d3e4f5");
  sim.addExample();  // a fresh board has no shows
  sim.step(2000);
  for (const char* l : {"help", "status", "settings", "name", "name=Front Door", "name", "trig_debounce_ms=5000", "nope=1", "nope", "trigger", "recover", "cancel",
                        "page_lock", "pixel_count=8", "pixel_count", "pin", "wifi_pass", "wifi_pass=secret-pass", "wifi_pass", "pixel_count=abc", "run_count", "mem"})
    console(sim, l);
  check("mqtt-client-id", sim.ctl().mqttClientId());
  check("mqtt-subscriptions", list(sim.ctl().mqttSubscriptions()));
  check("mqtt-will", msgs({sim.ctl().mqttWill()}));
  check("mqtt-on-connect", msgs(sim.ctl().mqttOnConnect()));
  sim.press(Input::Trig1, 150);
  sim.step(300);
  check("mqtt-poll-show", msgs(sim.ctl().mqttPoll()));
  std::string p = sim.ctl().mqttPrefix();
  const char* topics[][2] = {{"config/get/name", ""}, {"config/set/pixel_count", "12"}, {"config/set/pixel_count", "lots"}, {"config/set/nope", "1"},
                             {"rpc", R"({"id":5,"op":"list_shows"})"}, {"cue", "fog"}, {"control/stop_all", ""}, {"control/trigger", ""},
                             {"status/get/run_count", ""}, {"status/get/nope", ""}, {"config/get/wifi_pass", ""}};
  for (auto& t : topics) mqtt(sim, p, "mqtt-", t[0], t[1]);
  sim.step(1500);
  check("mqtt-poll-after", msgs(sim.ctl().mqttPoll()));
  // the pulse-and-recover effect and a profile's aliases
  sim.request(R"({"op":"delete_show","show":1})");
  sim.request(R"({"op":"set_params","values":[{"key":"effect","value":"pulse_and_recover"}]})");
  sim.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"valve_time","key":"out1_on_ms","label":"Valve open time","unit":"ms"},{"alias":"rest_s","key":"out2_on_ms","unit":"s","scale":1000},{"alias":"go","control":"trigger"},{"alias":"presses","status":"run_count"}]}})");
  sim.step(3000);
  for (const char* l : {"status", "settings", "help", "presses", "run_count", "valve_time", "valve_time=750", "rest_s=90", "rest_s=4", "go", "trigger",
                        "remote_trigger_enable=true", "go", "recover", "cancel"})
    console(sim, l);
  sim.step(100);
  const char* fx[][2] = {{"status/get/presses", ""}, {"status/get/runs_left", ""}, {"control/go", ""}, {"control/nope", ""}, {"config/get/valve_time", ""}, {"config/set/rest_s", "3"}};
  for (auto& t : fx) mqtt(sim, p, "mqtt-fx-", t[0], t[1]);
  check("mqtt-subscriptions-fx", list(sim.ctl().mqttSubscriptions()));
  TEST_ASSERT_EQUAL(61, n);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(console_and_mqtt_match_the_golden_files);
  return UNITY_END();
}
