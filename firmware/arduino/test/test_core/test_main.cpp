// Core tests: show, pixels, ramp, sound, teach, record UI, run window, engine.
#include <unity.h>
#include <vector>
#include "proptroller/engine.h"
#include "proptroller/pixels.h"
#include "proptroller/sound.h"
#include "proptroller/teach.h"
#include "proptroller/status.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static Show parse(const char* j) {
  Show s;
  std::string e;
  if (!showFromString(j, s, e)) TEST_FAIL_MESSAGE(e.c_str());
  return s;
}
static Action act(Action::Kind k) { Action a; a.kind = k; return a; }
static Cue cue(uint32_t at, Action a) { Cue c; c.at = at; c.action = a; return c; }

static const char* DOOR = R"({
  "id": 1, "name": "Front door",
  "triggers": [{"on": "input", "input": "TRIG1"}, {"on": "cue", "name": "door"}],
  "cooldown_ms": 5000,
  "tracks": [
    {"target": "OUT1",  "cues": [{"at": 0, "do": "on"}, {"at": 3000, "do": "off"}]},
    {"target": "OUT2",  "cues": [{"at": 500, "do": "ramp", "to": 1000, "ms": 300}, {"at": 4000, "do": "off"}]},
    {"target": "SOUND", "cues": [{"at": 0, "do": "play", "folder": 1, "track": 1}]},
    {"target": "PIXEL", "cues": [{"at": 0, "do": "flicker", "color": "#ff6000"}, {"at": 4000, "do": "off"}]},
    {"target": "CC1",   "cues": [{"at": 0, "do": "pulse", "ms": 200}]},
    {"target": "NET",   "cues": [{"at": 100, "do": "emit", "cue": "lights"}]}
  ]
})";

// ---------------------------------------------------------------- show
void json_round_trip_and_defaults() {
  Show s = parse(DOOR);
  TEST_ASSERT_TRUE(s.enabled && s.stop_sound && !s.looping);
  TEST_ASSERT_EQUAL(1, s.priority);
  TEST_ASSERT_TRUE(s.retrigger == Retrigger::Ignore);
  TEST_ASSERT_TRUE(s.tracks[1].target == Target::of(Channel::Out2));
  Action r = act(Action::Kind::Ramp);
  r.level = 1000;
  r.ms = 300;
  TEST_ASSERT_TRUE(s.tracks[1].cues[0].action == r);
  TEST_ASSERT_EQUAL(4000, s.length());
  std::string j = showToString(s);
  Show back = parse(j.c_str());
  TEST_ASSERT_EQUAL_STRING(j.c_str(), showToString(back).c_str());
  TEST_ASSERT_TRUE(j.find("\"target\":\"CC1\"") != std::string::npos);
  TEST_ASSERT_TRUE(j.find("\"do\":\"pulse\"") != std::string::npos);
  TEST_ASSERT_TRUE(validate(s).ok());
}

void level_evaluation() {
  Track t;
  t.target = Target::of(Channel::Out1);
  Action ramp1 = act(Action::Kind::Ramp); ramp1.level = 1000; ramp1.ms = 1000;
  Action lvl = act(Action::Kind::SetLevel); lvl.level = 200;
  Action ramp0 = act(Action::Kind::Ramp); ramp0.level = 0; ramp0.ms = 500;
  Action blink = act(Action::Kind::Blink); blink.on_ms = 100; blink.off_ms = 100; blink.count = 2;
  Action pulse = act(Action::Kind::Pulse); pulse.ms = 50;
  t.cues = {cue(1000, ramp1), cue(100, lvl), cue(3000, ramp0), cue(4000, blink), cue(5000, pulse)};
  Compiled c(t);
  Level l;
  TEST_ASSERT_FALSE(c.levelAt(50, l));
  const uint64_t ts[] = {100, 1500, 2500, 3250, 3600, 4050, 4150, 4250, 4450, 5049, 5050};
  const Level want[] = {200, 600, 1000, 500, 0, 1000, 0, 1000, 0, 1000, 0};
  for (int i = 0; i < 11; i++) {
    TEST_ASSERT_TRUE(c.levelAt(ts[i], l));
    TEST_ASSERT_EQUAL_MESSAGE(want[i], l, "level_at");
  }
}

void validation_rules() {
  Show s = parse(DOOR);
  s.id = 0;
  TEST_ASSERT_TRUE(validate(s).problem == Problem::BadId);
  s = parse(DOOR);
  s.tracks[4].cues[0].action = act(Action::Kind::SetLevel);
  s.tracks[4].cues[0].action.level = 500;
  ShowError e = validate(s);
  TEST_ASSERT_TRUE(e.problem == Problem::WrongTarget && e.track == 4 && e.cue == 0);
  TEST_ASSERT_EQUAL_STRING("WrongTarget in track 4 at cue 0", e.message().c_str());
  s = parse(DOOR);
  s.tracks[2].cues[0].action = act(Action::Kind::On);
  TEST_ASSERT_TRUE(validate(s).problem == Problem::WrongTarget);
  s = parse(DOOR);
  s.tracks[2].cues[0].action = act(Action::Kind::Volume);
  s.tracks[2].cues[0].action.volume = 31;
  TEST_ASSERT_TRUE(validate(s).problem == Problem::OutOfRange);
  s = parse(DOOR);
  s.tracks.push_back(s.tracks[0]);
  TEST_ASSERT_TRUE(validate(s).problem == Problem::DuplicateTarget);
  s = parse(DOOR);
  s.tracks[5].cues[0].action.cue = "";
  TEST_ASSERT_TRUE(validate(s).problem == Problem::BadCueName);
  s = parse(DOOR);
  s.looping = true;
  s.has_length = true;
  s.length_ms = 50;
  TEST_ASSERT_TRUE(validate(s).problem == Problem::TooFast);
  s = parse(DOOR);
  s.tracks[0].cues[1].at = MAX_LENGTH_MS + 1;
  TEST_ASSERT_TRUE(validate(s).problem == Problem::TooLong);
  s = parse(DOOR);
  s.runs_per_cooldown = 21;
  TEST_ASSERT_TRUE(validate(s).problem == Problem::OutOfRange);
}

// SM-S3: no load-current rules: any OUT may be dimmed or ramped, outputs may be on together
void validation_has_no_load_current_rules() {
  Show s = parse(DOOR);
  s.tracks[1].cues[0].action = act(Action::Kind::SetLevel);
  s.tracks[1].cues[0].action.level = 500;
  TEST_ASSERT_TRUE(validate(s).ok());
  s.tracks[1].cues[0].action = act(Action::Kind::Ramp);
  s.tracks[1].cues[0].action.level = 1000;
  s.tracks[1].cues[0].action.ms = 30000;
  TEST_ASSERT_TRUE(validate(s).ok());
}

void targets_parse() {
  for (int i = 0; i < TARGET_COUNT; i++) {
    Target t;
    TEST_ASSERT_TRUE(parseTarget(name(targetAt(i)), t));
    TEST_ASSERT_TRUE(t == targetAt(i));
  }
  Target t;
  TEST_ASSERT_TRUE(parseTarget("sound", t) && t == Target::sound());
  TEST_ASSERT_FALSE(parseTarget("BOGUS", t));
}

void triggers_json() {
  Show s = parse(R"({"id":1,"name":"x","triggers":[{"on":"input","input":"USER3"},{"on":"every","period_ms":60000},{"on":"boot"},{"on":"after","show":2},{"on":"release","input":"TRIG2"}]})");
  TEST_ASSERT_TRUE(s.triggers[0].on == ShowTrigger::Kind::OnInput && s.triggers[0].input == Input::User3);
  TEST_ASSERT_TRUE(s.triggers[2].on == ShowTrigger::Kind::Boot && s.triggers[2].ms == 0);
  TEST_ASSERT_TRUE(s.triggers[3].on == ShowTrigger::Kind::After && s.triggers[3].show == 2);
  Show bad;
  std::string e;
  TEST_ASSERT_FALSE(showFromString(R"({"id":1,"name":"x","tracks":[{"target":"OUT1","cues":[{"at":0,"do":"launch"}]}]})", bad, e));
  TEST_ASSERT_FALSE(showFromString(R"({"id":1,"name":"x","tracks":[{"target":"OUT1","cues":[{"at":0,"do":"play","folder":300,"track":1}]}]})", bad, e));
  TEST_ASSERT_FALSE(showFromString(R"({"name":"x"})", bad, e));
}

// ---------------------------------------------------------------- pixels
void color_json() {
  Rgb c;
  TEST_ASSERT_TRUE(parseColor("#FF8001", c));
  TEST_ASSERT_TRUE(c == Rgb(255, 128, 1));
  TEST_ASSERT_EQUAL_STRING("#ff8001", colorText(c).c_str());
  TEST_ASSERT_FALSE(parseColor("red", c));
  TEST_ASSERT_TRUE(parseColor("00ff00", c) && c == Rgb(0, 255, 0));
}

void effects() {
  Rgb px[10];
  Effect e;
  Rgb red(255, 0, 0);
  e.kind = Effect::Kind::Fill; e.color = red;
  render(e, 0, px, 10);
  for (auto& p : px) TEST_ASSERT_TRUE(p == red);
  e.kind = Effect::Kind::Chase; e.step_ms = 100; e.width = 2;
  render(e, 350, px, 10);
  for (int i = 0; i < 10; i++) TEST_ASSERT_EQUAL(i == 3 || i == 4, px[i] == red);
  e.kind = Effect::Kind::Flash; e.period_ms = 1000;
  render(e, 600, px, 10);
  for (auto& p : px) TEST_ASSERT_TRUE(p == Rgb());
  e.kind = Effect::Kind::Fade; e.ms = 1000;
  render(e, 500, px, 10);
  TEST_ASSERT_EQUAL(127, px[0].r);
  render(e, 5000, px, 10);
  TEST_ASSERT_EQUAL(255, px[0].r);
  e.kind = Effect::Kind::Rainbow;
  render(e, 0, px, 10);
  TEST_ASSERT_TRUE(px[0] != px[5]);
  e.kind = Effect::Kind::Flicker;
  render(e, 1000, px, 10);
  for (auto& p : px) TEST_ASSERT_TRUE(p.r >= 120 && p.g == 0);
  e.kind = Effect::Kind::Off;
  render(e, 0, px, 10);
  for (auto& p : px) TEST_ASSERT_TRUE(p == Rgb());
  e.kind = Effect::Kind::Chase; e.step_ms = 0; e.width = 0;
  render(e, 5, nullptr, 0);
}

void wheel_covers_primaries() {
  TEST_ASSERT_TRUE(wheel(0) == Rgb(255, 0, 0));
  TEST_ASSERT_TRUE(wheel(85) == Rgb(0, 255, 0));
  TEST_ASSERT_TRUE(wheel(170) == Rgb(0, 0, 255));
}

// ---------------------------------------------------------------- ramp
void soft_start_envelope() {
  Shaper s(300, 0);
  TEST_ASSERT_EQUAL(1, s.update(1000, 1000));
  TEST_ASSERT_EQUAL(500, s.update(1150, 1000));
  TEST_ASSERT_EQUAL(1000, s.update(1300, 1000));
  TEST_ASSERT_EQUAL(400, s.update(1400, 400));
  TEST_ASSERT_EQUAL(0, s.update(1410, 0));
  TEST_ASSERT_EQUAL(1, s.update(1420, 1000));
  TEST_ASSERT_EQUAL(500, s.update(1570, 1000));
}

void no_soft_start_is_immediate() {
  Shaper s(0, 0);
  TEST_ASSERT_EQUAL(1000, s.update(0, 1000));
  Shaper s2(99999, 0);
  TEST_ASSERT_EQUAL(MAX_SOFT_START_MS, s2.soft_start_ms);
}

void max_on_watchdog() {
  Shaper s(0, 1000);
  TEST_ASSERT_EQUAL(1000, s.update(0, 1000));
  TEST_ASSERT_EQUAL(1000, s.update(999, 1000));
  TEST_ASSERT_EQUAL(0, s.update(1000, 1000));
  TEST_ASSERT_TRUE(s.tripped());
  TEST_ASSERT_EQUAL(0, s.update(5000, 1000));
  TEST_ASSERT_EQUAL(0, s.update(5010, 0));
  TEST_ASSERT_FALSE(s.tripped());
  TEST_ASSERT_EQUAL(1000, s.update(5020, 1000));
}

// ---------------------------------------------------------------- sound
static DfEvent dfe(DfEvent::Kind k, uint16_t v = 0) { DfEvent e; e.kind = k; e.value = v; return e; }
static bool cmdsEq(const Commands& a, const Commands& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (!(a[i] == b[i])) return false;
  return true;
}
static Commands online(SoundDirector& d) {
  d.onEvent(dfe(DfEvent::Kind::Online, 2), 1500);
  return d.tick(1500);
}
static const Commands AMBIENT = {SoundCommand::volume(15), SoundCommand::playFolder(1, 1)};

void ambient_trigger_ambient() {
  SoundDirector d;
  TEST_ASSERT_TRUE(d.tick(0).empty());
  TEST_ASSERT_TRUE(cmdsEq(online(d), AMBIENT));
  TEST_ASSERT_TRUE(d.playing().kind == Playing::Kind::Ambient);
  d.onEvent(dfe(DfEvent::Kind::Finished, 1), 5000);
  TEST_ASSERT_TRUE(cmdsEq(d.tick(5000), AMBIENT));
  TEST_ASSERT_TRUE(cmdsEq(d.trigger(), {SoundCommand::volume(25), SoundCommand::playMp3(1)}));
  TEST_ASSERT_TRUE(d.tick(6000).empty());
  d.onEvent(dfe(DfEvent::Kind::Finished, 1), 8000);
  TEST_ASSERT_TRUE(d.tick(8100).empty());
  TEST_ASSERT_TRUE(cmdsEq(d.tick(8300), AMBIENT));
}

void one_file_card() {
  SoundDirector d;
  online(d);
  TEST_ASSERT_TRUE(d.onEvent(dfe(DfEvent::Kind::Error, 6), 1600).empty());
  TEST_ASSERT_TRUE(d.ambientMissing());
  TEST_ASSERT_TRUE(d.tick(5000).empty());
  d.trigger();
  d.onEvent(dfe(DfEvent::Kind::Finished, 1), 7000);
  TEST_ASSERT_TRUE(d.tick(9000).empty());
}

void file_not_in_mp3_falls_back_to_card_order() {
  SoundConfig c;
  c.ambient = false;
  SoundDirector d(c);
  online(d);
  d.trigger();
  TEST_ASSERT_TRUE(cmdsEq(d.onEvent(dfe(DfEvent::Kind::Error, 6), 2000), {SoundCommand::playIndex(1)}));
  TEST_ASSERT_TRUE(d.playing().kind == Playing::Kind::Trigger && d.playing().fallback && d.playing().track == 1);
  TEST_ASSERT_TRUE(d.onEvent(dfe(DfEvent::Kind::Error, 6), 2100).empty());
  TEST_ASSERT_TRUE(d.playing().kind == Playing::Kind::Idle);
}

void shows_and_suspend() {
  SoundDirector d;
  online(d);
  d.observeShow(SoundCommand::playFolder(2, 4), 2000);
  TEST_ASSERT_TRUE(d.playing().kind == Playing::Kind::Show);
  d.observeShow(SoundCommand::simple(SoundCommand::Kind::Stop), 3000);
  TEST_ASSERT_FALSE(d.tick(3400).empty());
  TEST_ASSERT_TRUE(cmdsEq(d.suspend(), {SoundCommand::simple(SoundCommand::Kind::Stop)}));
  d.onEvent(dfe(DfEvent::Kind::Finished, 1), 4000);
  TEST_ASSERT_TRUE(d.tick(9000).empty());
  d.resume(9000);
  TEST_ASSERT_FALSE(d.tick(9300).empty());
  SoundConfig c = d.cfg;
  c.ambient = false;
  TEST_ASSERT_TRUE(cmdsEq(d.setConfig(c, 9400), {SoundCommand::simple(SoundCommand::Kind::Stop)}));
  TEST_ASSERT_TRUE(d.tick(9500).empty());
}

// ---------------------------------------------------------------- teach
void teach_quantizes_and_clamps() {
  Teach t(100, 600000);
  uint32_t v;
  TEST_ASSERT_FALSE(t.stop(5, v));
  t.start(1000);
  TEST_ASSERT_TRUE(t.isActive());
  TEST_ASSERT_TRUE(t.stop(4234, v)); TEST_ASSERT_EQUAL(3230, v);
  t.start(0);
  t.stop(4, v); TEST_ASSERT_EQUAL(100, v);
  t.start(0);
  TEST_ASSERT_TRUE(t.timedOut(600000));
  t.stop(700000, v); TEST_ASSERT_EQUAL(600000, v);
  t.start(0);
  t.abort();
  TEST_ASSERT_FALSE(t.isActive());
  TEST_ASSERT_EQUAL(1230, quantize(1234));
  TEST_ASSERT_EQUAL(1240, quantize(1235));
}

// ---------------------------------------------------------------- run window
void one_run_per_window_is_the_classic_cooldown() {
  RunWindow w;
  TEST_ASSERT_TRUE(w.allows(0, 1));
  w.countRun(0);
  w.open(0, 5000);
  TEST_ASSERT_FALSE(w.allows(4990, 1));
  TEST_ASSERT_TRUE(w.allows(5000, 1));
}

void three_then_wait() {
  RunWindow w;
  for (Millis t : {0, 1000, 2000}) {
    TEST_ASSERT_TRUE(w.allows(t, 3));
    w.countRun(t);
    w.open(t, 180000);
  }
  TEST_ASSERT_FALSE(w.allows(179999, 3));
  TEST_ASSERT_EQUAL(0, w.left(100000, 3));
  TEST_ASSERT_TRUE(w.allows(180000, 3));
  TEST_ASSERT_EQUAL(3, w.left(180000, 3));
}

void changed_limit_applies_to_the_next_run() {
  RunWindow w;
  w.countRun(0);
  w.open(0, 10000);
  TEST_ASSERT_TRUE(w.allows(100, 3));
  TEST_ASSERT_FALSE(w.allows(100, 1));
  TEST_ASSERT_TRUE(w.allows(100, 2));
}

void zero_window_never_holds_back() {
  RunWindow w;
  for (Millis t = 0; t < 5; t++) {
    TEST_ASSERT_TRUE(w.allows(t, 1));
    w.countRun(t);
    w.open(t, 0);
  }
}

void a_run_after_the_window_ended_opens_a_fresh_one() {
  RunWindow w;
  w.countRun(0);
  w.open(0, 1000);
  w.countRun(1500);  // no allows() in between: countRun forgets the ended window itself
  w.open(1500, 1000);
  TEST_ASSERT_TRUE(w.isOpen(1600) && w.used() == 1 && w.remainingMs(1600) == 900);
}

#include "engine_tests.inc"

// the watchdog is fed only while the show step is alive (C4): a stalled step stops the feeding
void watchdog_feeds_only_while_the_step_runs() {
  TEST_ASSERT_TRUE(stepAlive(1000, 1000));
  TEST_ASSERT_TRUE(stepAlive(1000, 1005));  // stamped by the other core just after `now` was read
  TEST_ASSERT_TRUE(stepAlive(1099, 1000));
  TEST_ASSERT_FALSE(stepAlive(1100, 1000));
  TEST_ASSERT_FALSE(stepAlive(60000, 0));   // the step never ran
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(json_round_trip_and_defaults);
  RUN_TEST(level_evaluation);
  RUN_TEST(validation_rules);
  RUN_TEST(validation_has_no_load_current_rules);
  RUN_TEST(targets_parse);
  RUN_TEST(triggers_json);
  RUN_TEST(color_json);
  RUN_TEST(effects);
  RUN_TEST(wheel_covers_primaries);
  RUN_TEST(soft_start_envelope);
  RUN_TEST(no_soft_start_is_immediate);
  RUN_TEST(max_on_watchdog);
  RUN_TEST(ambient_trigger_ambient);
  RUN_TEST(one_file_card);
  RUN_TEST(file_not_in_mp3_falls_back_to_card_order);
  RUN_TEST(shows_and_suspend);
  RUN_TEST(teach_quantizes_and_clamps);
  RUN_TEST(one_run_per_window_is_the_classic_cooldown);
  RUN_TEST(three_then_wait);
  RUN_TEST(changed_limit_applies_to_the_next_run);
  RUN_TEST(zero_window_never_holds_back);
  RUN_TEST(a_run_after_the_window_ended_opens_a_fresh_one);
  RUN_TEST(watchdog_feeds_only_while_the_step_runs);
  run_engine_tests();
  return UNITY_END();
}
