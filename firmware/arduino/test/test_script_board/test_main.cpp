// Show scripts on the simulated board: the protocol ops, storage across power cycles, the boot re-check,
// settings that would break the stored script, MQTT, the DMX port and the ambient loop.
#include <unity.h>
#include <string>
#include "proptroller/script_record.h"
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static Response setScript(Sim& sim, const std::string& text, const char* sounds = "{}") {
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = text;
  JsonDocument s;
  deserializeJson(s, sounds);
  d["sounds"] = s.as<JsonObjectConst>();
  std::string j;
  serializeJson(d, j);
  return sim.request(j);
}

static const char* const LAMP = "effect lamp:\nwait for trig1\nout2 = on\nsleep 1\nout2 = off\n";

void set_get_and_survive_a_power_cycle() {
  Sim sim("t");
  sim.step(100);
  TEST_ASSERT_EQUAL_MESSAGE(0, sim.ctl().engine().showCount(), "a fresh board has no shows");
  Response r = setScript(sim, LAMP);
  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("script_check", r.doc()["result"]["kind"]);
  TEST_ASSERT_TRUE(r.doc()["result"]["ok"].as<bool>());
  TEST_ASSERT_EQUAL(1, r.doc()["result"]["effects"].as<int>());
  TEST_ASSERT_TRUE(r.doc()["result"]["table"].isNull());  // the step table stays on the board
  sim.powerCycle();
  Response g = sim.request(R"({"op":"get_script"})");
  TEST_ASSERT_EQUAL_STRING(LAMP, g.doc()["result"]["text"]);
  TEST_ASSERT_TRUE(g.doc()["result"]["check"]["ok"].as<bool>());
  sim.step(2500);
  sim.press(Input::Trig1, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out2));
  JsonDocument st;
  deserializeJson(st, sim.stateJson());
  TEST_ASSERT_EQUAL_STRING("lamp", st["script"]["effects"][0]["name"]);
  TEST_ASSERT_TRUE(st["script"]["effects"][0]["running"].as<bool>());
  TEST_ASSERT_EQUAL(4, st["script"]["effects"][0]["line"].as<int>());
  TEST_ASSERT_EQUAL_STRING("script", st["outputs"][1]["driver"]);
  // an empty script removes it
  TEST_ASSERT_TRUE(setScript(sim, "# nothing\n").doc()["result"]["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("", sim.request(R"({"op":"get_script"})").doc()["result"]["text"]);
  sim.powerCycle();
  TEST_ASSERT_EQUAL_STRING("", sim.request(R"({"op":"get_script"})").doc()["result"]["text"]);
}

void a_refused_script_stores_nothing() {
  Sim sim("t");
  setScript(sim, LAMP);
  Response r = setScript(sim, "effect a:\nwait for trig1\nout5 = on\n");
  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_FALSE(r.doc()["result"]["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("line 3: there is no out5 on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.",
                           r.doc()["result"]["errors"][0]["message"]);
  TEST_ASSERT_EQUAL_STRING(LAMP, sim.request(R"({"op":"get_script"})").doc()["result"]["text"]);
  Response c = sim.request(R"({"op":"check_script","text":"effect b:\nwait for trig2\n"})");
  TEST_ASSERT_TRUE(c.doc()["result"]["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING(LAMP, sim.request(R"({"op":"get_script"})").doc()["result"]["text"]);  // check stores nothing
}

void the_stored_script_is_checked_again_at_boot() {
  Sim sim("t");
  // a record a newer rule refuses (as after a firmware update): out9 does not exist
  (void)sim.scriptFlash().save(scriptRecordEncode("effect a:\nwait for trig2\nout2 = on\nout9 = on\n", "", nullptr, 0));
  sim.powerCycle();
  sim.step(3000);
  sim.press(Input::Trig2, 150);
  sim.step(500);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out2));
  JsonDocument st;
  deserializeJson(st, sim.stateJson());
  TEST_ASSERT_EQUAL_STRING("line 4: there is no out9 on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.", st["script"]["error"]);
}

void settings_that_would_break_the_script_are_refused() {
  Sim sim("t");
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"dmx_port","value":"s4"},{"key":"cc1_role","value":"shows"}]})").isOk());
  TEST_ASSERT_TRUE(setScript(sim, "effect fog:\nwait for trig2\ndmx1 = on\ncc1 = on\nsleep 1\ndmx1 = off\ncc1 = off\n").doc()["result"]["ok"].as<bool>());
  Response r = sim.request(R"({"op":"set_params","values":[{"key":"dmx_port","value":"off"}]})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("dmx_port: the stored script uses dmx1. Change the script first.", r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_params","values":[{"key":"cc1_role","value":"trigger_repeat"}]})");
  TEST_ASSERT_EQUAL_STRING("cc1_role: the stored script uses cc1. Change the script first.", r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_params","values":[{"key":"effect","value":"pulse_and_recover"}]})");
  TEST_ASSERT_EQUAL_STRING("effect: the stored script uses trig2. Change the script first.", r.errorMessage().c_str());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"name","value":"Fog"}]})").isOk());
}

void dmx_frames_only_with_the_port_and_a_dmx_line() {
  Sim sim("t");
  setScript(sim, LAMP);
  sim.step(1000);
  TEST_ASSERT_FALSE(sim.world().dmx_on);
  TEST_ASSERT_EQUAL(0, sim.world().dmx_frames.size());
  sim.request(R"({"op":"set_params","values":[{"key":"dmx_port","value":"s4"}]})");
  sim.step(1000);
  TEST_ASSERT_FALSE_MESSAGE(sim.world().dmx_on, "no dmx line: S4 stays an input");
  setScript(sim, "effect fog:\nwait for trig1\ndmx3 = 50%\nsleep 1\ndmx3 = off\n");
  sim.step(1000);
  TEST_ASSERT_TRUE(sim.world().dmx_on);
  size_t n = sim.world().dmx_frames.size();
  TEST_ASSERT_TRUE_MESSAGE(n >= 39 && n <= 41, "a frame every 25 ms");
  TEST_ASSERT_EQUAL(512, sim.world().dmx_frames.back().second.size());
  sim.step(1500);
  sim.press(Input::Trig1, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL(128, sim.world().dmx_frames.back().second[2]);
  setScript(sim, LAMP);
  sim.step(100);
  TEST_ASSERT_FALSE_MESSAGE(sim.world().dmx_on, "the port is given back");
}

void the_script_owns_the_player_and_wins_its_outputs() {
  Sim sim("t");
  // a show on OUT2 from TRIG2 and a script on OUT2 from TRIG1: the script's level wins
  sim.request(R"({"op":"put_show","data":{"id":5,"name":"Other","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"on"},{"at":3000,"do":"off"}]}]}})");
  setScript(sim, LAMP);
  sim.step(3000);
  sim.press(Input::Trig2, 150);
  sim.step(300);
  TEST_ASSERT_EQUAL_MESSAGE(0, sim.level(Channel::Out2), "the script holds OUT2 off");
  JsonDocument st;
  deserializeJson(st, sim.stateJson());
  TEST_ASSERT_FALSE_MESSAGE(st["sound"]["source"] == "ambient", "no ambient loop while a script is stored");
}

void scripts_over_mqtt() {
  Sim sim("t");
  sim.request(R"({"op":"set_params","values":[{"key":"name","value":"lab"}]})");
  Controller& c = sim.ctl();
  std::string p = c.mqttPrefix();
  auto out = c.mqttOnMessage(p + "/script/check", R"({"text":"effect a:\nwait for trig1\nout5 = on\n"})");
  TEST_ASSERT_EQUAL(1, out.size());
  TEST_ASSERT_EQUAL_STRING((p + "/script/result").c_str(), out[0].topic.c_str());
  TEST_ASSERT_TRUE(out[0].payload.find("there is no out5") != std::string::npos);
  out = c.mqttOnMessage(p + "/script/set", std::string("{\"text\":\"effect lamp:\\nwait for trig1\\nout2 = on\\nsleep 1\\nout2 = off\\n\"}"));
  TEST_ASSERT_TRUE(out[0].payload.find("\"ok\":true") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING(LAMP, c.scriptText().c_str());
  // locked: script/set is refused, as config/set
  sim.request(R"({"op":"set_params","values":[{"key":"pin","value":"654321"}]})");
  sim.request(R"({"op":"set_params","values":[{"key":"page_lock","value":true},{"key":"pin","value":"654321"}],"pin":"654321"})");
  out = c.mqttOnMessage(p + "/script/set", R"({"text":""})");
  TEST_ASSERT_EQUAL_STRING((p + "/error").c_str(), out[0].topic.c_str());
  TEST_ASSERT_EQUAL_STRING(LAMP, c.scriptText().c_str());
}


// SS-N: a wiring label or a profile alias in the script; renaming or removing one the script uses is refused
void names_in_a_stored_script() {
  Sim sim("t");
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"out2_name","value":"Lamp"},{"key":"rly1_role","value":"shows"}]})").isOk());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"FOG","terminal":"RLY1"},{"alias":"BELL","terminal":"TRIG2"}]}})").isOk());
  Response r = setScript(sim, "effect porch:\nwait for bell\nlamp = on\nfog = on\nsleep 1\nfog = off\nlamp = off\n");
  TEST_ASSERT_TRUE(r.doc()["result"]["ok"].as<bool>());
  // a rename or a removal the script would not survive
  r = sim.request(R"({"op":"set_params","values":[{"key":"out2_name","value":"porch lamp"}]})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("out2_name: the stored script uses lamp. Change the script first.", r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"BELL","terminal":"TRIG2"}]}})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("profile: the stored script uses fog. Change the script first.", r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_params","values":[{"key":"rly1_role","value":"trigger_repeat"}]})");
  TEST_ASSERT_EQUAL_STRING("rly1_role: the stored script uses fog (rly1). Change the script first.", r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_params","values":[{"key":"out1_name","value":"lamp"}]})");
  TEST_ASSERT_EQUAL_STRING("out1_name: the stored script uses lamp. Change the script first.", r.errorMessage().c_str());
  // a name moved to another terminal: the script would pass but drive another output
  r = sim.request(R"({"op":"set_params","values":[{"key":"out2_name","value":""},{"key":"out1_name","value":"lamp"}]})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("out2_name: the stored script uses lamp. Change the script first.", r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_params","values":[{"key":"out2_name","value":"spare"},{"key":"out3_name","value":"lamp"}]})");
  TEST_ASSERT_FALSE(r.isOk());  // a swap
  r = sim.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"FOG","terminal":"RLY2"},{"alias":"BELL","terminal":"TRIG2"}]}})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("profile: the stored script would then switch another terminal. Change the script first.", r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"FOG","terminal":"RLY1"},{"alias":"BELL","terminal":"TRIG1"}]}})");
  TEST_ASSERT_FALSE(r.isOk());  // an input's name moved
  // a change the script does not care about
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"out3_name","value":"spare"}]})").isOk());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"FOG","terminal":"RLY1"},{"alias":"BELL","terminal":"TRIG2"},{"alias":"go","control":"trigger"}]}})").isOk());
  // it runs across a power cycle
  sim.request(R"({"op":"reboot"})");  // the settings saved
  sim.powerCycle();
  uint32_t checks = sim.ctl().script_checks_at_boot;
  TEST_ASSERT_TRUE(sim.request(R"({"op":"get_script"})").doc()["result"]["check"]["ok"].as<bool>());
  sim.step(3000);
  sim.press(Input::Trig2, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out2));
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Rly1));
  TEST_ASSERT_EQUAL_UINT32(1, checks);  // the names changed after the save: checked again once, and it passes
}

void a_stored_script_whose_name_is_gone_does_not_run() {
  Sim sim("t");
  // a record stored for a board with "lamp" (as after a settings reset): checked again at boot, kept, not run
  (void)sim.scriptFlash().save(scriptRecordEncode("effect a:\nwait for trig2\nlamp = on\n", "", nullptr, 0));
  sim.powerCycle();
  sim.step(3000);
  sim.press(Input::Trig2, 150);
  sim.step(500);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  JsonDocument st;
  deserializeJson(st, sim.stateJson());
  TEST_ASSERT_EQUAL_STRING("line 3: there is no lamp on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.", st["script"]["error"]);
  // naming the terminal again brings it back after a power cycle
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"out1_name","value":"lamp"}]})").isOk());
  sim.request(R"({"op":"reboot"})");
  sim.powerCycle();
  sim.step(3000);
  sim.press(Input::Trig2, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(set_get_and_survive_a_power_cycle);
  RUN_TEST(a_refused_script_stores_nothing);
  RUN_TEST(the_stored_script_is_checked_again_at_boot);
  RUN_TEST(settings_that_would_break_the_script_are_refused);
  RUN_TEST(dmx_frames_only_with_the_port_and_a_dmx_line);
  RUN_TEST(the_script_owns_the_player_and_wins_its_outputs);
  RUN_TEST(scripts_over_mqtt);
  RUN_TEST(names_in_a_stored_script);
  RUN_TEST(a_stored_script_whose_name_is_gone_does_not_run);
  return UNITY_END();
}
