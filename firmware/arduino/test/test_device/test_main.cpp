#include "proptroller/password.h"
// The generic controller on virtual hardware: the cases of proptroller-sim/tests (board.rs,
// roles_sound.rs, runs_window.rs, cascade.rs), the JSON scenarios, and the settings tests.
#include <unity.h>
#include <fstream>
#include <sstream>
#include "proptroller/sim/sim.h"
#include "proptroller/storage_ab.h"
#include <map>

using namespace pt;
void setUp() {}
void tearDown() {}

static std::string str(const std::string& s) {
  JsonDocument d;
  d.set(s);
  std::string o;
  serializeJson(d, o);
  return o;
}
static void set(Sim& sim, const std::string& key, const std::string& value_json) {
  Response r = sim.request(R"({"op":"set_params","values":[{"key":")" + key + R"(","value":)" + value_json + "}]}");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), (key + ": " + r.errorMessage()).c_str());
}
static Response put(Sim& sim, const std::string& show) { return sim.request(R"({"op":"put_show","data":)" + show + "}"); }
static std::vector<std::pair<Millis, Level>> changes(const Sim& s, Channel c) {
  std::vector<std::pair<Millis, Level>> v;
  for (auto& x : s.trace)
    if (x.channel == c) v.push_back({x.t, x.level});
  return v;
}
static JsonDocument state(Sim& sim) {
  JsonDocument d;
  deserializeJson(d, sim.stateJson());
  return d;
}
static JsonDocument stateCtl(Sim& sim) {
  JsonDocument d;
  deserializeJson(d, sim.ctl().stateJson());
  return d;
}
static bool allOff(Sim& s) {
  for (auto c : ALL_CHANNELS)
    if (s.level(c)) return false;
  return true;
}

// ---------------------------------------------------------------- board.rs
void boots_with_everything_off_and_arms() {
  Sim sim("t");
  TEST_ASSERT_TRUE(allOff(sim));
  sim.step(50);
  TEST_ASSERT_TRUE(sim.ctl().pattern() == Pattern(Pattern::Kind::Steady));
  TEST_ASSERT_TRUE(sim.world().led);
  TEST_ASSERT_TRUE(allOff(sim));
}

void example_show_runs_from_trig1_with_sound_and_pixels() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  sim.step(2000);
  sim.trace.clear();
  Millis t0 = sim.now();
  sim.press(Input::Trig1, 150);
  sim.step(3000);
  auto o = changes(sim, Channel::Out1);
  TEST_ASSERT_EQUAL(2, o.size());
  TEST_ASSERT_EQUAL(1000, o[0].second);
  TEST_ASSERT_TRUE_MESSAGE(o[0].first - t0 <= 110, "the 100 ms debounce + one tick");
  TEST_ASSERT_EQUAL(2000, o[1].first - o[0].first);
  bool played = false;
  for (auto& p : sim.world().df.plays) played |= p.what == "/mp3/0001";
  TEST_ASSERT_TRUE_MESSAGE(played, "trigger sound (the example show has no sound of its own)");
  TEST_ASSERT_TRUE(sim.world().pixel_frames > 10);
  TEST_ASSERT_EQUAL(0, sim.world().pixel_errors);
  TEST_ASSERT_TRUE(PROPTROLLER_PIXEL.idleLevel());
  for (auto& p : sim.world().pixels) TEST_ASSERT_TRUE(p == Rgb());
}

void trigger_held_at_power_on_never_triggers() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  sim.setInput(Input::Trig1, true);
  sim.powerCycle(100);
  sim.step(5000);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  TEST_ASSERT_EQUAL(0, changes(sim, Channel::Out1).size());
  TEST_ASSERT_TRUE(sim.ctl().pattern() == Pattern(Pattern::Kind::Flicker));
  JsonDocument st = stateCtl(sim);
  TEST_ASSERT_TRUE(st["inputs"][0]["active"].as<bool>() && st["inputs"][0]["waiting"].as<bool>());
  sim.setInput(Input::Trig1, false);
  sim.step(300);  // the release is debounced (100 ms), then re-arms (100 ms)
  TEST_ASSERT_TRUE(sim.ctl().pattern() == Pattern(Pattern::Kind::Steady));
  sim.press(Input::Trig1, 150);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out1));
}

void power_loss_mid_show_does_not_resume() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  sim.step(200);
  sim.press(Input::Trig1, 150);
  sim.step(500);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out1));
  sim.powerCycle(300);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  sim.step(3000);
  int on = 0;
  for (auto& c : changes(sim, Channel::Out1)) on += c.second > 0;
  TEST_ASSERT_EQUAL(1, on);
}

void dfplayer_driver_against_the_module_model() {
  Sim sim("t");
  sim.step(3000);
  bool vol = false;
  for (auto& l : sim.world().df.log) TEST_ASSERT_TRUE(l.first >= 1500);
  for (auto& l : sim.world().df.log) vol |= l.second == df::cmd::VOLUME;
  TEST_ASSERT_TRUE(vol);
  TEST_ASSERT_EQUAL(0, sim.world().df.bad_frames);
  TEST_ASSERT_EQUAL(15, sim.world().df.volume);
  TEST_ASSERT_TRUE(stateCtl(sim)["sound_online"].as<bool>());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"sound","cmd":"play_folder","folder":3,"track":9})").isOk());
  sim.step(100);
  TEST_ASSERT_TRUE(stateCtl(sim)["sound_playing"].as<bool>());
  TEST_ASSERT_TRUE(sim.world().df.playing);
  sim.step(3200);
  TEST_ASSERT_FALSE(stateCtl(sim)["sound_playing"].as<bool>());
  sim.world().df.card[{1, 1}] = 1000;
  sim.request(R"({"op":"sound","cmd":"play_folder","folder":2,"track":2})");
  sim.step(300);
  TEST_ASSERT_FALSE(stateCtl(sim)["sound_playing"].as<bool>());
  std::vector<std::string> ev;
  sim.ctl().eventsSince(0, ev);
  bool found = false;
  for (auto& e : ev) found |= e.find("error 6") != std::string::npos;
  TEST_ASSERT_TRUE(found);
}

void dfplayer_clone_without_acks() {
  Sim sim("t");
  sim.world().df.behaviour.acks = false;
  set(sim, "df_ack", "false");
  sim.step(2000);
  for (int i = 1; i <= 5; i++) sim.request(R"({"op":"sound","cmd":"volume","volume":)" + std::to_string(i) + "}");
  sim.step(500);
  TEST_ASSERT_EQUAL(5, sim.world().df.volume);
  TEST_ASSERT_EQUAL(6, sim.world().df.count(df::cmd::VOLUME));
}

// SM-S3: the firmware neither measures nor checks load current: any OUT dims, all may be on together
void no_load_current_checks() {
  Sim sim("t");
  Response r = sim.request(R"({"op":"set_params","values":[{"key":"out2_load_ma","value":5000}]})");
  TEST_ASSERT_EQUAL_STRING("not_found", r.doc()["error"]["code"].as<const char*>());
  TEST_ASSERT_TRUE(put(sim, R"({"id":5,"name":"Dim","tracks":[{"target":"OUT2","cues":[{"at":0,"do":"level","level":500}]}]})").isOk());
  sim.request(R"({"op":"set_output","channel":"OUT2","level":500,"ms":600000})");
  sim.step(3000);
  TEST_ASSERT_EQUAL_MESSAGE(500, sim.level(Channel::Out2), "dimmed as long as asked");
  for (const char* c : {"OUT1", "OUT3", "OUT4"})
    sim.request(std::string(R"({"op":"set_output","channel":")") + c + R"(","level":1000,"ms":600000})");
  sim.step(20);
  for (auto c : {Channel::Out1, Channel::Out3, Channel::Out4}) TEST_ASSERT_EQUAL(1000, sim.level(c));
  JsonDocument st = stateCtl(sim);
  TEST_ASSERT_TRUE(st["outputs"][1]["fault"].isNull() && st["outputs"][1]["load_ma"].isNull() && st["outputs"][1]["pwm_limit_ma"].isNull());
  TEST_ASSERT_FALSE(sim.request(R"({"op":"set_output","channel":"RLY1","level":500})").isOk());
}

void soft_start_on_the_pins() {
  Sim sim("t");
  set(sim, "out2_soft_start_ms", "300");
  TEST_ASSERT_TRUE(put(sim, R"({"id": 2, "name": "Fan", "triggers": [{"on":"input","input":"TRIG1"}],
        "tracks": [{"target": "OUT2", "cues": [{"at": 0, "do": "on"}, {"at": 1000, "do": "off"}]}]})").isOk());
  sim.step(200);
  sim.trace.clear();
  sim.press(Input::Trig1, 150);
  sim.step(1200);
  auto ch = changes(sim, Channel::Out2);
  Millis first = ch[0].first;
  auto at = [&](Millis dt) {
    Level l = 0;
    for (auto& c : ch)
      if (c.first <= first + dt) l = c.second;
    return l;
  };
  TEST_ASSERT_TRUE(at(0) < 100);
  TEST_ASSERT_TRUE(at(150) >= 450 && at(150) <= 550);
  TEST_ASSERT_EQUAL(1000, at(300));
  TEST_ASSERT_EQUAL(0, ch.back().second);
}

void settings_and_shows_survive_power_cycles() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  set(sim, "name", str("Fog corner"));
  TEST_ASSERT_TRUE(put(sim, R"({"id": 3, "name": "Fog", "triggers": [{"on":"input","input":"TRIG2"}],
        "tracks": [{"target": "RLY1", "cues": [{"at": 0, "do": "pulse", "ms": 3000}]}]})").isOk());
  sim.step(2500);
  sim.powerCycle(500);
  TEST_ASSERT_EQUAL_STRING("Fog corner", sim.ctl().settings().name.c_str());
  TEST_ASSERT_NOT_NULL(sim.ctl().engine().show(3));
  TEST_ASSERT_NOT_NULL(sim.ctl().engine().show(1));
  sim.step(200);
  sim.press(Input::Trig2, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Rly1));
  TEST_ASSERT_TRUE(sim.request(R"({"op":"reboot"})").isOk());
  sim.step(20);
  TEST_ASSERT_EQUAL(3, sim.boots);
  sim.request(R"({"op":"factory_reset"})");
  sim.step(20);
  TEST_ASSERT_EQUAL_STRING("Proptroller", sim.ctl().settings().name.c_str());
  TEST_ASSERT_NULL(sim.ctl().engine().show(3));
  TEST_ASSERT_EQUAL_MESSAGE(0, sim.ctl().engine().showCount(), "a factory reset leaves no shows");
}

// The board has no recorder of its own any more (the phone page and Specter Studio record an effect script): the
// record ops are unknown, USER1-3 are plain triggers, and a board updated from a firmware that had learn mode
// keeps its recorded show as a normal show.
void record_mode_is_gone_and_a_recorded_show_stays() {
  Sim sim("t");
  TEST_ASSERT_TRUE(put(sim, R"({"id":1,"name":"Recorded","triggers":[{"on":"input","input":"TRIG1"}],"tracks":[{"target":"OUT3","cues":[{"at":0,"do":"on"},{"at":300,"do":"off"}]}]})").isOk());
  TEST_ASSERT_TRUE(put(sim, R"({"id":2,"name":"Button","triggers":[{"on":"input","input":"USER1"}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"pulse","ms":400}]}]})").isOk());
  sim.ctl().saveNow();
  // the stored settings as an older firmware wrote them: learn mode on, recording into show 1
  std::string rec;
  TEST_ASSERT_TRUE(sim.flash().load(rec));
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, rec));
  d["settings"]["buttons"] = "learn";
  d["settings"]["record_show"] = 1;
  rec.clear();
  serializeJson(d, rec);
  TEST_ASSERT_TRUE(sim.flash().save(rec));
  sim.powerCycle();
  TEST_ASSERT_TRUE(sim.ctl().recordsOk());
  TEST_ASSERT_NOT_NULL(sim.ctl().engine().show(1));
  TEST_ASSERT_EQUAL_STRING("Recorded", sim.ctl().engine().show(1)->name.c_str());
  // the ops are gone (a bad request, like any unknown op), and so are the capability, the state and the settings
  for (const char* op : {R"({"op":"record_start","show":0,"target":"OUT1"})", R"({"op":"record_tap","down":true})",
                         R"({"op":"record_finish"})", R"({"op":"record_abort"})"}) {
    Response r = sim.request(op);
    TEST_ASSERT_EQUAL_STRING("bad_request", r.doc()["error"]["code"].as<const char*>());
  }
  TEST_ASSERT_TRUE(sim.infoJson().find("\"record\"") == std::string::npos);
  TEST_ASSERT_TRUE(sim.stateJson().find("recording") == std::string::npos);
  std::string params = sim.request(R"({"op":"get_params"})").json();
  TEST_ASSERT_TRUE(params.find("\"buttons\"") == std::string::npos);
  TEST_ASSERT_TRUE(params.find("record_show") == std::string::npos);
  Response bad = sim.request(R"({"op":"set_params","values":[{"key":"buttons","value":"learn"}]})");
  TEST_ASSERT_FALSE(bad.isOk());
  // the recorded show plays on TRIG1 like any show
  sim.step(500);
  sim.trace.clear();
  sim.press(Input::Trig1, 150);
  sim.step(1000);
  TEST_ASSERT_EQUAL(2, changes(sim, Channel::Out3).size());
  // USER1 held as long as the old REC hold: a plain trigger, no learn menu on the status LED
  sim.trace.clear();
  sim.press(Input::User1, 1200);
  TEST_ASSERT_FALSE(sim.ctl().pattern() == Pattern(Pattern::Kind::Count, 1));
  sim.step(500);
  TEST_ASSERT_EQUAL(2, changes(sim, Channel::Out2).size());
  // the record is written back without the old keys at the next save
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"name","value":"Porch"}]})").isOk());
  sim.ctl().saveNow();
  TEST_ASSERT_TRUE(sim.flash().load(rec));
  TEST_ASSERT_TRUE(rec.find("record_show") == std::string::npos);
  TEST_ASSERT_TRUE(rec.find("\"buttons\"") == std::string::npos);
}

static std::vector<int> showIds(Sim& sim) {
  Response r = sim.request(R"({"op":"list_shows"})");
  std::vector<int> v;
  JsonArrayConst a = r.result()["shows"].as<JsonArrayConst>();
  for (JsonVariantConst x : a) v.push_back(x["id"].as<int>());
  return v;
}

void fresh_board_has_no_shows_until_the_example_is_added() {
  Sim sim("t");
  TEST_ASSERT_EQUAL(0, showIds(sim).size());
  // wiring a fresh board starts no show: OUT1-4 and the relays stay off whatever input is pressed (the CC1 / CC2
  // output roles, trigger repeat and sound trigger, still pass a TRIG press on as a 200 ms contact pulse)
  sim.step(500);
  for (Input i : {Input::Trig1, Input::Trig2, Input::User1, Input::User2, Input::User3}) {
    sim.press(i, 150);
    for (int k = 0; k < 10; k++) {
      sim.step(10);
      for (Channel c : {Channel::Out1, Channel::Out2, Channel::Out3, Channel::Out4, Channel::Rly1, Channel::Rly2})
        TEST_ASSERT_EQUAL(0, sim.level(c));
    }
    sim.step(300);
  }
  // the example only when asked for: id 1, then the lowest free id
  Response r = sim.request(R"({"op":"add_example"})");
  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("show", r.result()["kind"].as<const char*>());
  TEST_ASSERT_EQUAL(1, r.result()["data"]["id"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Example show: door knock", r.result()["data"]["name"].as<const char*>());
  sim.step(200);
  sim.press(Input::Trig1, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out1));
  r = sim.request(R"({"op":"add_example"})");
  TEST_ASSERT_EQUAL(2, r.result()["data"]["id"].as<int>());
  // with the built-in effect on, TRIG1 and OUT1 are its own: refused
  Sim fx("t");
  set(fx, "effect", str("pulse_and_recover"));
  r = fx.request(R"({"op":"add_example"})");
  TEST_ASSERT_EQUAL_STRING("invalid", r.doc()["error"]["code"].as<const char*>());
  TEST_ASSERT_EQUAL(0, showIds(fx).size());
}

// New shows get their id from the board (two phones adding at once never pick the same one).
void board_picks_new_show_ids() {
  Sim sim("t");
  TEST_ASSERT_TRUE(put(sim, R"({"id":2,"name":"Fog","triggers":[],"tracks":[{"target":"RLY1","cues":[{"at":0,"do":"pulse","ms":500}]}]})").isOk());
  Response r = sim.request(R"({"op":"put_show","new":true,"data":{"id":2,"name":"A","triggers":[],"tracks":[]}})");
  TEST_ASSERT_TRUE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("show", r.result()["kind"].as<const char*>());
  TEST_ASSERT_EQUAL_MESSAGE(1, r.result()["data"]["id"].as<int>(), "the lowest free id, whatever id was sent");
  r = sim.request(R"({"op":"put_show","new":true,"data":{"id":0,"name":"B","triggers":[],"tracks":[]}})");
  TEST_ASSERT_EQUAL(3, r.result()["data"]["id"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Fog", sim.request(R"({"op":"get_show","show":2})").result()["data"]["name"].as<const char*>());
}

void add_replace_delete_shows() {
  Sim sim("t");
  TEST_ASSERT_TRUE(put(sim, R"({"id":2,"name":"Fog","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"RLY1","cues":[{"at":0,"do":"pulse","ms":500}]}]})").isOk());
  TEST_ASSERT_TRUE(put(sim, R"({"id":3,"name":"Lamp","triggers":[{"on":"input","input":"TRIG1"}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"pulse","ms":500}]}]})").isOk());
  TEST_ASSERT_TRUE(showIds(sim) == std::vector<int>({2, 3}));
  // replace: the same id with new content
  TEST_ASSERT_TRUE(put(sim, R"({"id":3,"name":"Lamp 2","triggers":[{"on":"input","input":"TRIG1"}],"tracks":[{"target":"OUT3","cues":[{"at":0,"do":"pulse","ms":500}]}]})").isOk());
  Response r = sim.request(R"({"op":"get_show","show":3})");
  TEST_ASSERT_EQUAL_STRING("Lamp 2", r.result()["data"]["name"].as<const char*>());
  TEST_ASSERT_EQUAL(1, r.result()["data"]["tracks"].size());
  // delete, and a second delete is not_found
  TEST_ASSERT_TRUE(sim.request(R"({"op":"delete_show","show":3})").isOk());
  r = sim.request(R"({"op":"delete_show","show":3})");
  TEST_ASSERT_EQUAL_STRING("not_found", r.doc()["error"]["code"].as<const char*>());
  TEST_ASSERT_TRUE(showIds(sim) == std::vector<int>({2}));
}

void upgrade_removes_only_the_untouched_factory_example() {
  // earlier firmware stored this show on every fresh board
  Sim sim("t");
  TEST_ASSERT_TRUE(put(sim, FACTORY_EXAMPLE_SHOW).isOk());
  TEST_ASSERT_TRUE(put(sim, R"({"id":2,"name":"Fog","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"RLY1","cues":[{"at":0,"do":"pulse","ms":500}]}]})").isOk());
  sim.step(2500);
  sim.powerCycle(500);
  TEST_ASSERT_TRUE(showIds(sim) == std::vector<int>({2}));
  sim.step(2500);  // saved without it
  sim.powerCycle(500);
  TEST_ASSERT_TRUE(showIds(sim) == std::vector<int>({2}));
  // an edited one is the user's own, and an added example stays across power cycles
  Sim b("t");
  std::string edited = FACTORY_EXAMPLE_SHOW;
  edited.replace(edited.find("3000"), 4, "4000");
  TEST_ASSERT_TRUE(put(b, edited).isOk());
  b.request(R"({"op":"add_example"})");
  b.step(2500);
  b.powerCycle(500);
  TEST_ASSERT_TRUE(showIds(b) == std::vector<int>({1, 2}));
}

void link_on_while_held() {
  Sim sim("t");
  TEST_ASSERT_TRUE(put(sim, R"({"id":2,"name":"Link","triggers":[{"on":"input","input":"TRIG1","until_release":true}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"on"}]},{"target":"RLY1","cues":[{"at":0,"do":"on"}]}],"length_ms":5000})").isOk());
  sim.step(500);
  sim.setInput(Input::Trig1, true);
  sim.step(1500);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out2));
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Rly1));
  sim.setInput(Input::Trig1, false);
  sim.step(150);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out2));
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Rly1));
  // held past the show's end: off at the end, nothing at the release
  sim.step(500);
  sim.setInput(Input::Trig1, true);
  sim.step(5500);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out2));
  sim.setInput(Input::Trig1, false);
  sim.step(300);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out2));
  Response r = sim.request(R"({"op":"get_show","show":2})");
  TEST_ASSERT_TRUE(r.result()["data"]["triggers"][0]["until_release"].as<bool>());
  // a USER button (buttons = triggers, the default) follows the same way
  TEST_ASSERT_TRUE(put(sim, R"({"id":3,"name":"Link U","triggers":[{"on":"input","input":"USER1","until_release":true}],"tracks":[{"target":"OUT4","cues":[{"at":0,"do":"on"}]}],"length_ms":5000})").isOk());
  sim.setInput(Input::User1, true);
  sim.step(1500);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out4));
  sim.setInput(Input::User1, false);
  sim.step(150);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out4));
  sim.press(Input::User1, 100);  // a short click: on, then off at the release
  sim.step(100);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out4));
}

void pause_input_and_pin() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  set(sim, "trig2", str("pause"));
  set(sim, "pin", str("654321"));
  TEST_ASSERT_TRUE_MESSAGE(sim.request(R"({"op":"stop_all"})").isOk(), "SM-P1: a password alone does not lock the page");
  set(sim, "page_lock", "true");
  sim.step(200);
  Response r = sim.request(R"({"op":"run","show":1})");
  TEST_ASSERT_EQUAL_STRING("pin", r.doc()["error"]["code"].as<const char*>());
  Response wrong = sim.request(R"({"op":"run","show":1,"pin":"654322"})");
  TEST_ASSERT_EQUAL_STRING("pin", wrong.doc()["error"]["code"].as<const char*>());
  sim.step(1000);  // the next try waits 1 s after a wrong password
  TEST_ASSERT_TRUE(sim.request(R"({"op":"run","show":1,"pin":"654321"})").isOk());
  sim.step(100);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out1));
  TEST_ASSERT_TRUE(sim.request(R"({"op":"state"})").isOk());
  sim.setInput(Input::Trig2, true);
  sim.step(150);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  TEST_ASSERT_TRUE(sim.ctl().pattern() == Pattern(Pattern::Kind::Heartbeat));
  sim.press(Input::Trig1, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  sim.setInput(Input::Trig2, false);
  sim.step(4000);
  sim.press(Input::Trig1, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out1));
  sim.setInput(Input::Trig2, true);
  sim.powerCycle(100);
  sim.step(200);
  TEST_ASSERT_TRUE(stateCtl(sim)["paused"].as<bool>());
}

void storage_full_is_refused() {
  Sim sim("t");
  std::string cues;
  for (int i = 0; i < 900; i++) cues += (i ? "," : "") + std::string("{\"at\":") + std::to_string(i * 10) + ",\"do\":\"" + (i % 2 ? "off" : "on") + "\"}";
  Response r = put(sim, R"({"id":10,"name":"Big","tracks":[{"target":"OUT1","cues":[)" + cues + "]}]}");
  TEST_ASSERT_EQUAL(0, r.errorMessage().find("storage full: "));
}

void status_patterns_follow_the_show() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  sim.step(200);
  sim.press(Input::Trig1, 150);
  sim.step(150);  // released for the 100 ms release time
  TEST_ASSERT_TRUE(sim.ctl().pattern() == Pattern(Pattern::Kind::FastBlink));
  sim.step(2500);
  TEST_ASSERT_TRUE(sim.ctl().pattern() == Pattern(Pattern::Kind::SlowBlink));
  sim.step(3000);
  TEST_ASSERT_TRUE(sim.ctl().pattern() == Pattern(Pattern::Kind::Steady));
}

// ---------------------------------------------------------------- roles_sound.rs
static std::vector<std::pair<Millis, Millis>> onSpans(const Sim& sim, Channel c, Millis from) {
  std::vector<std::pair<Millis, Millis>> v;
  bool on = false;
  Millis s = 0;
  for (auto& x : changes(sim, c)) {
    if (x.first < from) continue;
    if (x.second > 0 && !on) { on = true; s = x.first; }
    else if (x.second == 0 && on) { on = false; v.push_back({s, x.first - s}); }
  }
  return v;
}
static std::vector<DfModel::Play>& plays(Sim& s) { return s.world().df.plays; }

void default_ambient_then_trigger_sound_then_ambient() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  sim.world().df.setCard({{1, 1, 4000}, {DfModel::MP3, 1, 1500}});
  sim.powerCycle(10);
  sim.step(2500);
  TEST_ASSERT_EQUAL(1, plays(sim).size());
  TEST_ASSERT_EQUAL_STRING("/01/001", plays(sim)[0].what.c_str());
  TEST_ASSERT_EQUAL(15, plays(sim)[0].volume);
  TEST_ASSERT_EQUAL_STRING("ambient", stateCtl(sim)["sound"]["source"].as<const char*>());
  Millis t0 = sim.now();
  sim.press(Input::Trig1, 150);
  sim.step(100);
  TEST_ASSERT_EQUAL_STRING("/mp3/0001", plays(sim)[1].what.c_str());
  TEST_ASSERT_EQUAL(25, plays(sim)[1].volume);
  TEST_ASSERT_EQUAL_STRING("/mp3/0001", stateCtl(sim)["sound"]["track"].as<const char*>());
  sim.step(300);
  auto cc1 = onSpans(sim, Channel::Cc1, t0);
  TEST_ASSERT_EQUAL(1, cc1.size());
  TEST_ASSERT_EQUAL(200, cc1[0].second);
  TEST_ASSERT_EQUAL(200, onSpans(sim, Channel::Cc2, t0)[0].second);
  TEST_ASSERT_TRUE_MESSAGE(cc1[0].first - t0 <= 110, "pulse on the accepted press (after the 100 ms debounce)");
  sim.step(2000);
  TEST_ASSERT_EQUAL_STRING("/01/001", plays(sim).back().what.c_str());
  Millis t1 = sim.now();
  size_t n = plays(sim).size();
  sim.press(Input::Trig1, 150);
  sim.step(400);
  TEST_ASSERT_EQUAL(0, onSpans(sim, Channel::Cc1, t1).size());
  TEST_ASSERT_EQUAL(n, plays(sim).size());
}

void one_file_card_plays_it_as_the_trigger_and_skips_ambient() {
  Sim sim("t");
  sim.world().df.setCard({{DfModel::MP3, 1, 1000}});
  sim.powerCycle(10);
  sim.step(3000);
  TEST_ASSERT_EQUAL(0, plays(sim).size());
  TEST_ASSERT_TRUE(stateCtl(sim)["sound"]["ambient_missing"].as<bool>());
  sim.press(Input::Trig1, 150);
  sim.step(200);
  TEST_ASSERT_EQUAL_STRING("/mp3/0001", plays(sim)[0].what.c_str());
  sim.step(3000);
  TEST_ASSERT_EQUAL(1, plays(sim).size());
}

void any_file_on_the_card_plays_as_the_trigger() {
  Sim sim("t");
  sim.world().df.setCard({{DfModel::ROOT, 1, 1000}});
  sim.powerCycle(10);
  sim.step(3000);
  sim.press(Input::Trig1, 150);
  sim.step(300);
  TEST_ASSERT_EQUAL(1, plays(sim).size());
  TEST_ASSERT_EQUAL_STRING("card #1", plays(sim)[0].what.c_str());
}

void ambient_off_and_settings() {
  Sim sim("t");
  sim.world().df.setCard({{1, 1, 4000}, {2, 5, 4000}, {DfModel::MP3, 3, 1000}});
  set(sim, "ambient", "false");
  set(sim, "trigger_track", "3");
  set(sim, "trigger_volume", "30");
  sim.step(2500);
  sim.powerCycle(10);
  sim.step(3000);
  TEST_ASSERT_EQUAL(0, plays(sim).size());
  sim.press(Input::Trig1, 150);
  sim.step(200);
  TEST_ASSERT_EQUAL_STRING("/mp3/0003", plays(sim)[0].what.c_str());
  TEST_ASSERT_EQUAL(30, plays(sim)[0].volume);
  set(sim, "ambient_folder", "2");
  set(sim, "ambient_track", "5");
  set(sim, "ambient_volume", "7");
  set(sim, "ambient", "true");
  sim.step(2000);
  TEST_ASSERT_EQUAL_STRING("/02/005", plays(sim).back().what.c_str());
  TEST_ASSERT_EQUAL(7, plays(sim).back().volume);
}

void show_sound_pulses_the_sound_trigger_and_suppresses_the_default() {
  Sim sim("t");
  TEST_ASSERT_TRUE(put(sim, R"({"id": 2, "name": "Talk", "triggers": [{"on":"input","input":"TRIG2"}],
        "tracks": [{"target": "SOUND", "cues": [{"at": 500, "do": "play", "folder": 3, "track": 1}]}], "length_ms": 1000})").isOk());
  sim.step(2500);
  Millis t0 = sim.now();
  size_t before = plays(sim).size();
  sim.press(Input::Trig2, 150);
  sim.step(1000);
  TEST_ASSERT_EQUAL(before + 1, plays(sim).size());
  TEST_ASSERT_EQUAL_STRING("/03/001", plays(sim).back().what.c_str());
  auto cc2 = onSpans(sim, Channel::Cc2, t0);
  TEST_ASSERT_EQUAL(1, cc2.size());
  TEST_ASSERT_TRUE(cc2[0].first - t0 >= 500);
  TEST_ASSERT_EQUAL(1, onSpans(sim, Channel::Cc1, t0).size());
}

void role_outputs_are_reserved_and_configurable() {
  Sim sim("t");
  std::string s = R"({"id": 3, "name": "Uses CC1", "tracks": [{"target": "CC1", "cues": [{"at": 0, "do": "on"}]}]})";
  Response r = put(sim, s);
  TEST_ASSERT_EQUAL_STRING("invalid", r.doc()["error"]["code"].as<const char*>());
  TEST_ASSERT_TRUE(r.errorMessage().find("role") != std::string::npos);
  set(sim, "cc1_role", str("shows"));
  TEST_ASSERT_TRUE(put(sim, s).isOk());
  r = sim.request(R"({"op":"set_params","values":[{"key":"cc1_role","value":"trigger_repeat"}]})");
  TEST_ASSERT_EQUAL_STRING("invalid", r.doc()["error"]["code"].as<const char*>());
  set(sim, "rly1_role", str("trigger_repeat"));
  set(sim, "role_pulse_ms", "500");
  sim.step(300);
  Millis t0 = sim.now();
  sim.press(Input::Trig1, 150);
  sim.step(800);
  auto r1 = onSpans(sim, Channel::Rly1, t0);
  TEST_ASSERT_EQUAL(1, r1.size());
  TEST_ASSERT_EQUAL(500, r1[0].second);
  bool found = false;
  for (auto& t : sim.ctl().info().terminals)
    if (t.id == "RLY1") { found = true; TEST_ASSERT_EQUAL_STRING("trigger_repeat", t.role.c_str()); TEST_ASSERT_FALSE(t.shows); }
  TEST_ASSERT_TRUE(found);
  TEST_ASSERT_EQUAL_STRING("trigger repeat", stateCtl(sim)["outputs"][4]["driver"].as<const char*>());
  set(sim, "rly1_role", str("none"));
  sim.step(4000);
  Millis t1 = sim.now();
  sim.press(Input::Trig1, 150);
  sim.step(800);
  TEST_ASSERT_EQUAL(0, onSpans(sim, Channel::Rly1, t1).size());
  TEST_ASSERT_EQUAL_STRING("not used", stateCtl(sim)["outputs"][4]["blocked"].as<const char*>());
}

void plain_trigger_without_a_show_still_counts() {
  Sim sim("t");
  sim.step(2500);
  Millis t0 = sim.now();
  sim.press(Input::Trig2, 150);
  sim.step(300);
  TEST_ASSERT_EQUAL(1, onSpans(sim, Channel::Cc1, t0).size());
  bool found = false;
  for (auto& p : plays(sim)) found |= p.what == "/mp3/0001";
  TEST_ASSERT_TRUE(found);
}

void momentary_hold_with_lease() {
  Sim sim("t");
  sim.step(200);
  TEST_ASSERT_TRUE(sim.request(R"({"op":"sim_input","input":"TRIG1","active":true,"lease_ms":300})").isOk());
  sim.step(150);  // past the 100 ms debounce
  TEST_ASSERT_TRUE(stateCtl(sim)["inputs"][0]["active"].as<bool>());
  TEST_ASSERT_TRUE(state(sim)["inputs"][0]["held"].as<bool>());
  sim.request(R"({"op":"sim_input","input":"TRIG1","active":true,"lease_ms":300})");
  sim.step(250);
  TEST_ASSERT_TRUE(state(sim)["inputs"][0]["held"].as<bool>());
  sim.step(300);  // released after the lease (and the debounce)
  TEST_ASSERT_FALSE(state(sim)["inputs"][0]["held"].as<bool>());
  TEST_ASSERT_FALSE(stateCtl(sim)["inputs"][0]["active"].as<bool>());
  sim.request(R"({"op":"sim_input","input":"TRIG2","active":true,"lease_ms":5000})");
  sim.step(50);
  sim.request(R"({"op":"sim_input","input":"TRIG2","active":false})");
  sim.step(50);
  TEST_ASSERT_FALSE(state(sim)["inputs"][1]["held"].as<bool>());
  TEST_ASSERT_FALSE(stateCtl(sim)["inputs"][1]["active"].as<bool>());
}

void wiring_state_detail() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  set(sim, "out1_name", str("door bolt"));
  set(sim, "out1_soft_start_ms", "500");
  set(sim, "trig1_name", str("doormat"));
  sim.step(300);
  sim.press(Input::Trig1, 150);
  sim.step(100);
  JsonDocument st = stateCtl(sim);
  JsonObject o = st["outputs"][0];
  TEST_ASSERT_EQUAL_STRING("Example show: door knock", o["driver"].as<const char*>());
  TEST_ASSERT_TRUE(o["ramping"].as<bool>());
  TEST_ASSERT_EQUAL(1000, o["requested"].as<int>());
  TEST_ASSERT_FALSE(st["inputs"][0]["armed"].as<bool>());
  for (auto& t : sim.ctl().info().terminals) {
    if (t.id == "OUT1") TEST_ASSERT_EQUAL_STRING("door bolt", t.label.c_str());
    if (t.id == "TRIG1") TEST_ASSERT_EQUAL_STRING("doormat", t.label.c_str());
  }
  TEST_ASSERT_EQUAL(0, std::string(st["pixel"] | "").find("flash"));
}

// ---------------------------------------------------------------- runs_window.rs
void three_per_window_and_a_restart_clears_the_count() {
  Sim sim("runs");
  TEST_ASSERT_TRUE(put(sim, R"({"id": 9, "name": "Three per window", "triggers": [{"on":"input","input":"TRIG2"}],
  "cooldown_ms": 180000, "runs_per_cooldown": 3,
  "tracks": [{"target": "OUT4", "cues": [{"at": 0, "do": "on"}, {"at": 500, "do": "off"}]}]})").isOk());
  sim.step(3000);
  auto once = [&]() { sim.press(Input::Trig2, 150); sim.step(1000); };
  auto runs = [&]() { int n = 0; for (auto& c : sim.trace) n += c.channel == Channel::Out4 && c.level > 0; return n; };
  auto show9 = [&]() { JsonDocument d = state(sim); for (JsonVariant s : d["shows"].as<JsonArray>()) if (s["id"] == 9) { JsonDocument o; o.set(s); return o; } return JsonDocument(); };
  once();
  once();
  TEST_ASSERT_EQUAL(1, show9()["runs_left"].as<int>());
  once();
  once();
  TEST_ASSERT_EQUAL(3, runs());
  TEST_ASSERT_EQUAL(0, show9()["runs_left"].as<int>());
  TEST_ASSERT_EQUAL_STRING("cooldown", show9()["state"].as<const char*>());
  sim.powerCycle(500);
  sim.step(3000);
  TEST_ASSERT_EQUAL(3, show9()["runs_left"].as<int>());
  once();
  TEST_ASSERT_EQUAL(4, runs());
}

// ---------------------------------------------------------------- cascade.rs
void master_with_four_slaves() {
  std::vector<std::unique_ptr<Sim>> b;
  for (int i = 0; i < 5; i++) b.emplace_back(new Sim("rig" + std::to_string(i)));
  TEST_ASSERT_TRUE(b[0]->request(R"({"op":"set_params","values":[{"key":"cc1_role","value":"shows"},{"key":"cc2_role","value":"shows"}]})").isOk());
  TEST_ASSERT_TRUE(put(*b[0], R"({"id": 1, "name": "Master", "triggers": [{"on":"input","input":"TRIG1"}], "tracks": [
        {"target": "CC1",  "cues": [{"at": 0,    "do": "pulse", "ms": 200}]},
        {"target": "CC2",  "cues": [{"at": 500,  "do": "pulse", "ms": 200}]},
        {"target": "RLY1", "cues": [{"at": 1000, "do": "pulse", "ms": 200}]},
        {"target": "RLY2", "cues": [{"at": 1500, "do": "pulse", "ms": 200}]},
        {"target": "NET",  "cues": [{"at": 2000, "do": "emit", "cue": "lights"}]}]})").isOk());
  for (int k = 1; k <= 4; k++) {
    TEST_ASSERT_TRUE(put(*b[k], R"({"id": 1, "name": "Slave", "triggers": [{"on":"input","input":"TRIG1"}], "tracks": [
            {"target": "OUT1", "cues": [{"at": 0, "do": "pulse", "ms": 1000}]}]})").isOk());
    TEST_ASSERT_TRUE(put(*b[k], R"({"id": 2, "name": "Lights", "triggers": [{"on":"cue","name":"lights"}], "tracks": [
            {"target": "OUT2", "cues": [{"at": 0, "do": "pulse", "ms": 500}]}]})").isOk());
  }
  const Channel wires[4] = {Channel::Cc1, Channel::Cc2, Channel::Rly1, Channel::Rly2};
  std::vector<std::pair<Millis, std::string>> radio;  // (deliver at, cue)
  int radio_count = 0;
  auto step = [&](Millis ms) {
    for (Millis t = 0; t < ms; t += 10) {
      for (int k = 0; k < 4; k++) b[k + 1]->setInput(Input::Trig1, b[0]->level(wires[k]) > 0);
      for (auto& s : b) s->tick();
      for (auto& c : b[0]->ctl().takeCuesOut()) radio.push_back({b[0]->now() + 20, c});
      for (auto it = radio.begin(); it != radio.end();) {
        if (it->first <= b[0]->now()) {
          for (int k = 1; k <= 4; k++) { b[k]->ctl().cueIn(it->second); radio_count++; }
          it = radio.erase(it);
        } else ++it;
      }
    }
  };
  step(500);
  for (auto& s : b) s->trace.clear();
  b[0]->setInput(Input::Trig1, true);
  step(150);
  b[0]->setInput(Input::Trig1, false);
  step(3500);
  Millis edges[4];
  for (int k = 0; k < 4; k++) {
    auto w = changes(*b[0], wires[k]);
    TEST_ASSERT_FALSE_MESSAGE(w.empty(), "the master drove every wire");
    edges[k] = w[0].first;
  }
  for (int k = 0; k < 4; k++) {
    auto on = changes(*b[k + 1], Channel::Out1);
    TEST_ASSERT_EQUAL(2, on.size());
    Millis latency = on[0].first - edges[k];
    TEST_ASSERT_TRUE_MESSAGE(latency >= 100 && latency <= 130, "the 100 ms debounce + ticks");
    TEST_ASSERT_EQUAL(1000, on[1].first - on[0].first);
  }
  TEST_ASSERT_EQUAL(500, edges[1] - edges[0]);
  for (int k = 1; k <= 4; k++) TEST_ASSERT_EQUAL(2, changes(*b[k], Channel::Out2).size());
  TEST_ASSERT_EQUAL(4, radio_count);
  TEST_ASSERT_EQUAL(0, changes(*b[0], Channel::Out2).size());
}

// ---------------------------------------------------------------- scenarios (firmware/scenarios/*.json)
static void scenario(const char* file) {
  std::ifstream f(std::string("../scenarios/") + file);
  TEST_ASSERT_TRUE_MESSAGE(f.good(), file);
  std::stringstream ss;
  ss << f.rdbuf();
  ScenarioReport rep;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(runScenario(ss.str(), rep, err), err.c_str());
  std::string all;
  for (auto& x : rep.failures) all += x + "; ";
  TEST_ASSERT_TRUE_MESSAGE(rep.passed(), all.c_str());
  TEST_ASSERT_TRUE(rep.checks > 3);
}
void scenario_door_cooldown() { scenario("door-cooldown.json"); }
void scenario_fog_ambient() { scenario("fog-ambient.json"); }
void scenario_power_cycle() { scenario("power-cycle.json"); }

// ---------------------------------------------------------------- settings.rs
void params_cover_every_key_both_ways() {
  Settings s;
  for (auto& d : Settings::params()) {
    Value v;
    TEST_ASSERT_TRUE_MESSAGE(s.get(d.key, v), d.key.c_str());
    TEST_ASSERT_NULL_MESSAGE(d.check(v), d.key.c_str());
    TEST_ASSERT_NULL_MESSAGE(s.set(d.key, v), d.key.c_str());
  }
  TEST_ASSERT_TRUE(s == Settings());
}

void set_values() {
  Settings s;
  TEST_ASSERT_NOT_NULL_MESSAGE(s.set("out2_load_ma", Value::integer(4500)), "SM-S3: no load-current settings");
  TEST_ASSERT_NOT_NULL(s.set("supply_mv", Value::integer(12000)));
  TEST_ASSERT_NULL(s.set("pixel_brightness", Value::integer(40)));
  TEST_ASSERT_EQUAL(40, s.pixel_brightness);
  TEST_ASSERT_NULL(s.set("out2_soft_start_ms", Value::integer(300)));
  TEST_ASSERT_NULL(s.set("rly1_max_on_ms", Value::integer(9000)));
  TEST_ASSERT_NULL(s.set("trig2", Value::text("pause")));
  TEST_ASSERT_NULL(s.set("interlock_relays", Value::boolean(true)));
  EngineConfig e = s.engine();
  TEST_ASSERT_EQUAL(300, e.outputs[1].soft_start_ms);
  TEST_ASSERT_EQUAL(9000, e.outputs[4].max_on_ms);
  TEST_ASSERT_TRUE(e.has_pause_input && e.pause_input == Input::Trig2);
  TEST_ASSERT_EQUAL(1, e.interlocks.size());
  TEST_ASSERT_NOT_NULL(s.set("ap_pass", Value::text("short")));
  TEST_ASSERT_NOT_NULL(s.set("name", Value::text("  ")));
  TEST_ASSERT_NOT_NULL(s.set("nope", Value::integer(1)));
  TEST_ASSERT_NOT_NULL(s.set("out9_soft_start_ms", Value::integer(1)));
  bool blank = false;
  for (auto& pv : s.values()) blank |= pv.key == "wifi_pass" && pv.value == Value::text("");
  TEST_ASSERT_TRUE(blank);
}

void stored_json_tolerates_missing_fields() {
  JsonDocument d;
  deserializeJson(d, R"({"name":"Door"})");
  Settings s;
  s.fromJson(d.as<JsonVariantConst>());
  TEST_ASSERT_EQUAL_STRING("Door", s.name.c_str());
  TEST_ASSERT_EQUAL(100, s.pixel_brightness);
}

// ---------------------------------------------------------------- store.rs (A/B records)
struct MemFs : FileIo {
  std::map<std::string, std::string> files;
  long cut_after = -1;  // a power cut after this many bytes of the next write
  bool read(const char* p, std::string& out) override {
    auto it = files.find(p);
    if (it == files.end()) return false;
    out = it->second;
    return true;
  }
  bool write(const char* p, const std::string& d) override {
    if (cut_after >= 0) { files[p] = d.substr(0, (size_t)cut_after); cut_after = -1; return false; }
    files[p] = d;
    return true;
  }
  bool remove(const char* p) override { return files.erase(p) > 0; }
};

void ab_store_survives_power_cuts() {
  MemFs fs;
  ABStorage st(fs);
  std::string out;
  TEST_ASSERT_FALSE(st.load(out));
  TEST_ASSERT_TRUE(st.save("one"));
  TEST_ASSERT_TRUE(st.save("two"));
  TEST_ASSERT_TRUE(st.load(out) && out == "two");
  TEST_ASSERT_EQUAL(2, st.sequence());
  for (long cut : {0L, 5L, 16L, 18L}) {
    fs.cut_after = cut;
    TEST_ASSERT_FALSE(st.save("three-but-cut"));
    TEST_ASSERT_TRUE(st.load(out));
    TEST_ASSERT_EQUAL_STRING("two", out.c_str());
  }
  TEST_ASSERT_TRUE(st.save("four"));
  TEST_ASSERT_TRUE(st.load(out) && out == "four");
  fs.files["/cfg-a.bin"][20] ^= 1;  // flip a bit in one slot: the other one is used
  TEST_ASSERT_TRUE(st.load(out));
  TEST_ASSERT_TRUE(st.wipe());
  TEST_ASSERT_FALSE(st.load(out));
  TEST_ASSERT_FALSE(st.save(std::string(STORE_CAPACITY + 1, 'x')));
}

// SM-P4: the page password is kept as a salted SHA-256
void page_password_hash_is_salted_sha256() {
  uint8_t h[32];
  sha256((const uint8_t*)"abc", 3, h);
  char x[65];
  for (int i = 0; i < 32; i++) snprintf(x + 2 * i, 3, "%02x", h[i]);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", x);
  std::string a(1000, 'a');
  sha256((const uint8_t*)a.data(), a.size(), h);
  for (int i = 0; i < 32; i++) snprintf(x + 2 * i, 3, "%02x", h[i]);
  TEST_ASSERT_EQUAL_STRING("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3", x);
  std::string salt = newSalt("board-1");
  TEST_ASSERT_EQUAL_STRING("0bb0f566e5343f84", salt.c_str());
  TEST_ASSERT_EQUAL_STRING("6661999b9b455fe7718c194a7e5d92e090603895a85185ff4a349d2ec10614f0", passwordHash(salt, "door-42").c_str());
  TEST_ASSERT_TRUE(passwordOk(salt, passwordHash(salt, "door-42"), "door-42"));
  TEST_ASSERT_FALSE(passwordOk(salt, passwordHash(salt, "door-42"), "door-43"));
  TEST_ASSERT_FALSE(passwordOk(salt, "", ""));
  // stored settings hold the hash, never the password
  Sim sim("t");
  set(sim, "pin", str("door-42"));
  std::string stored;
  JsonDocument d;
  sim.ctl().settings().toJson(d.to<JsonObject>());
  serializeJson(d, stored);
  TEST_ASSERT_TRUE(stored.find("door-42") == std::string::npos && stored.find("pin_hash") != std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(boots_with_everything_off_and_arms);
  RUN_TEST(example_show_runs_from_trig1_with_sound_and_pixels);
  RUN_TEST(trigger_held_at_power_on_never_triggers);
  RUN_TEST(power_loss_mid_show_does_not_resume);
  RUN_TEST(dfplayer_driver_against_the_module_model);
  RUN_TEST(dfplayer_clone_without_acks);
  RUN_TEST(no_load_current_checks);
  RUN_TEST(soft_start_on_the_pins);
  RUN_TEST(settings_and_shows_survive_power_cycles);
  RUN_TEST(record_mode_is_gone_and_a_recorded_show_stays);
  RUN_TEST(fresh_board_has_no_shows_until_the_example_is_added);
  RUN_TEST(add_replace_delete_shows);
  RUN_TEST(board_picks_new_show_ids);
  RUN_TEST(upgrade_removes_only_the_untouched_factory_example);
  RUN_TEST(link_on_while_held);
  RUN_TEST(pause_input_and_pin);
  RUN_TEST(page_password_hash_is_salted_sha256);
  RUN_TEST(storage_full_is_refused);
  RUN_TEST(status_patterns_follow_the_show);
  RUN_TEST(default_ambient_then_trigger_sound_then_ambient);
  RUN_TEST(one_file_card_plays_it_as_the_trigger_and_skips_ambient);
  RUN_TEST(any_file_on_the_card_plays_as_the_trigger);
  RUN_TEST(ambient_off_and_settings);
  RUN_TEST(show_sound_pulses_the_sound_trigger_and_suppresses_the_default);
  RUN_TEST(role_outputs_are_reserved_and_configurable);
  RUN_TEST(plain_trigger_without_a_show_still_counts);
  RUN_TEST(momentary_hold_with_lease);
  RUN_TEST(wiring_state_detail);
  RUN_TEST(three_per_window_and_a_restart_clears_the_count);
  RUN_TEST(master_with_four_slaves);
  RUN_TEST(scenario_door_cooldown);
  RUN_TEST(scenario_fog_ambient);
  RUN_TEST(scenario_power_cycle);
  RUN_TEST(params_cover_every_key_both_ways);
  RUN_TEST(set_values);
  RUN_TEST(stored_json_tolerates_missing_fields);
  RUN_TEST(ab_store_survives_power_cuts);
  return UNITY_END();
}
