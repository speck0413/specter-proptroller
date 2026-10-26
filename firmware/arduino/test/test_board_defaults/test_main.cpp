// A board support package's defaults (device.h FactoryDefaults): a fresh board (nothing stored, or after a factory
// reset) starts with the package's alias profile, settings and show script; a board with stored settings keeps its
// own; a value the package gets wrong is refused in plain words and the rest still applies.
#include <unity.h>
#include "proptroller/sim/sim.h"

using namespace pt;

void setUp() {}
void tearDown() {}

static const char* const PROFILE = R"({"topic_prefix":"myprop","aliases":[{"alias":"valve_time","key":"out1_on_ms"},
  {"alias":"VALVE","terminal":"OUT1"}]})";
static const char* const SETTINGS = "name=Prop7\n# a comment\n  valve_time = 750\ncooldown_ms=0\n\n";
static const char* const SCRIPT = "effect blink:\n  wait for trig2\n  valve = on\n  sleep 1\n  valve = off\n";

static void wipe(Sim& s) {
  TEST_ASSERT_TRUE(s.flash().wipe());
  TEST_ASSERT_TRUE(s.scriptFlash().wipe());
}

void a_fresh_board_starts_with_the_packages_profile_settings_and_script() {
  Sim s("bsp");
  FactoryDefaults d;
  d.profile = PROFILE;
  d.settings = SETTINGS;
  d.script = SCRIPT;
  s.setFactoryDefaults(&d);
  wipe(s);
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", s.factoryRefused.c_str(), s.factoryRefused.c_str());
  TEST_ASSERT_EQUAL_STRING("Prop7", s.ctl().settings().name.c_str());
  TEST_ASSERT_EQUAL(750, s.ctl().settings().fx.out1_on_ms);
  TEST_ASSERT_EQUAL(0, s.ctl().settings().fx.cooldown_ms);
  TEST_ASSERT_EQUAL_STRING("myprop", s.ctl().settings().profile.topic_prefix.c_str());
  Response g = s.request(R"({"op":"get_script"})");
  TEST_ASSERT_TRUE(g.doc()["result"]["check"]["ok"].as<bool>());
  // kept like anything set by hand: a change survives the next power-on, the defaults are not applied again
  s.serial("valve_time=900");
  s.ctl().saveNow();
  s.powerCycle();
  TEST_ASSERT_EQUAL(900, s.ctl().settings().fx.out1_on_ms);
  // the script runs: TRIG2 lights OUT1 for 1 s
  s.step(2500);
  Millis t0 = s.now();
  s.press(Input::Trig2, 150);
  s.step(200);
  TEST_ASSERT_EQUAL(FULL, s.level(Channel::Out1));
  (void)t0;
}

void a_board_with_stored_settings_keeps_its_own() {
  Sim s("bsp");
  s.serial("name=Mine");
  s.ctl().saveNow();
  FactoryDefaults d;
  d.settings = SETTINGS;
  s.setFactoryDefaults(&d);
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING("Mine", s.ctl().settings().name.c_str());
  TEST_ASSERT_FALSE(s.ctl().freshBoard());
}

void a_wrong_default_is_refused_in_plain_words_and_the_rest_applies() {
  Sim s("bsp");
  FactoryDefaults d;
  d.settings = "out1_on_ms=5\nname=Prop8\nno_such_key=1\n";
  d.script = "effect x:\n  out9 = on\n";
  s.setFactoryDefaults(&d);
  wipe(s);
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING("Prop8", s.ctl().settings().name.c_str());
  TEST_ASSERT_TRUE_MESSAGE(s.factoryRefused.find("out1_on_ms=5: ") == 0, s.factoryRefused.c_str());
  TEST_ASSERT_TRUE_MESSAGE(s.factoryRefused.find("no_such_key=1: unknown command or setting") != std::string::npos, s.factoryRefused.c_str());
  TEST_ASSERT_TRUE_MESSAGE(s.factoryRefused.find("script: line 1: effect \"x\" never waits") != std::string::npos, s.factoryRefused.c_str());
}

void a_factory_reset_brings_the_defaults_back() {
  Sim s("bsp");
  FactoryDefaults d;
  d.settings = "name=Prop9\n";
  s.setFactoryDefaults(&d);
  wipe(s);
  s.powerCycle();
  s.serial("name=Other");
  s.ctl().saveNow();
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset"})").isOk());
  s.step(100);
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING("Prop9", s.ctl().settings().name.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(a_fresh_board_starts_with_the_packages_profile_settings_and_script);
  RUN_TEST(a_board_with_stored_settings_keeps_its_own);
  RUN_TEST(a_wrong_default_is_refused_in_plain_words_and_the_rest_applies);
  RUN_TEST(a_factory_reset_brings_the_defaults_back);
  return UNITY_END();
}
