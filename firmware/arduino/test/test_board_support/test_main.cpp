// Board support (pins.h "Another board"): the example BSP's pin map with terminals left out (NO_PIN) and an active-high
// input. Absent terminals are never driven or read and are not offered; the polarity is handled in the input layer.
#include <unity.h>
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

// the example board support package (bsp/relay4-button): four outputs, two triggers (TRIG2 a button with a
// pull-down: active high), nothing else
#include "../../bsp/relay4-button/proptroller_board.h"
constexpr PinMap RELAY4_ACTIVE_HIGH_BUTTON = PROPTROLLER_BOARD_MAP;
static_assert(pinsDistinct(RELAY4_ACTIVE_HIGH_BUTTON) && outputsCanDrive(RELAY4_ACTIVE_HIGH_BUTTON) &&
                  internalPullsPossible(RELAY4_ACTIVE_HIGH_BUTTON),
              "the example profile passes the pin checks");
// the checks catch a bad profile
constexpr PinMap TWO_ON_ONE = {Mcu::Esp32DevKit, {23, 23, 21, 19}, {NO_PIN, NO_PIN}, {NO_PIN, NO_PIN}, {36, 18},
                               {NO_PIN, NO_PIN, NO_PIN}, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, 39, {NO_PIN, NO_PIN}, 2, 0, 0x03};
static_assert(!pinsDistinct(TWO_ON_ONE), "a GPIO used twice is caught");
constexpr PinMap PULL_ON_36 = {Mcu::Esp32DevKit, {23, 22, 21, 19}, {NO_PIN, NO_PIN}, {NO_PIN, NO_PIN}, {36, 18},
                               {NO_PIN, NO_PIN, NO_PIN}, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, 39, {NO_PIN, NO_PIN}, 2, 0, 0x02};
static_assert(!internalPullsPossible(PULL_ON_36), "an internal pull on GPIO36 is caught");

static Response ok(Sim& sim, const std::string& j) {
  Response r = sim.request(j);
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.errorMessage().c_str());
  return r;
}

void absent_terminals_are_never_touched() {
  Sim sim("p", "sim", &RELAY4_ACTIVE_HIGH_BUTTON);
  // a show that asks for RLY1 and RLY2 anyway (stored before the profile, say)
  ok(sim, R"({"op":"put_show","data":{"id":2,"name":"r","triggers":[{"on":"cue","name":"go"}],"tracks":[{"target":"RLY1","cues":[{"at":0,"do":"on"}]},{"target":"RLY2","cues":[{"at":0,"do":"on"}]},{"target":"OUT2","cues":[{"at":0,"do":"on"},{"at":3000,"do":"off"}]}]}})");
  sim.step(2500);
  ok(sim, R"({"op":"cue","name":"go"})");
  sim.step(500);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out2));
  for (int ch = 4; ch < 8; ch++) {
    TEST_ASSERT_EQUAL_MESSAGE(0, sim.world().writes[ch], "an absent output was written");
    TEST_ASSERT_EQUAL(0, sim.level(ALL_CHANNELS[ch]));
  }
  for (int k = 2; k < 5; k++) TEST_ASSERT_EQUAL_MESSAGE(0, sim.world().reads[k], "an absent input was read");
  TEST_ASSERT_TRUE(sim.world().reads[1] > 0);
  Response r = sim.request(R"({"op":"set_output","channel":"RLY1","level":0})");
  TEST_ASSERT_EQUAL_STRING("RLY1 is not on this board", r.errorMessage().c_str());
}

void an_active_high_button_counts_once_after_a_release() {
  Sim sim("p", "sim", &RELAY4_ACTIVE_HIGH_BUTTON);
  ok(sim, R"({"op":"put_show","data":{"id":2,"name":"b","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"OUT3","cues":[{"at":0,"do":"pulse","ms":300}]}]}})");
  sim.setInput(Input::Trig2, true);  // held (pin HIGH) from power-on: never counts
  sim.step(3000);
  TEST_ASSERT_TRUE(sim.world().input_high[1]);
  TEST_ASSERT_EQUAL(0, sim.ctl().presses[1]);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out3));
  sim.setInput(Input::Trig2, false);  // released: the pin goes LOW
  sim.step(500);
  sim.press(Input::Trig2, 200);
  sim.step(20);
  TEST_ASSERT_EQUAL(1, sim.ctl().presses[1]);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out3));
}

void info_lists_only_the_terminals_the_board_has() {
  Sim sim("p", "sim", &RELAY4_ACTIVE_HIGH_BUTTON);
  std::string ids;
  for (auto& t : sim.ctl().info().terminals) ids += t.id + " ";
  TEST_ASSERT_EQUAL_STRING("TRIG1 TRIG2 OUT1 OUT2 OUT3 OUT4 ", ids.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(absent_terminals_are_never_touched);
  RUN_TEST(an_active_high_button_counts_once_after_a_release);
  RUN_TEST(info_lists_only_the_terminals_the_board_has);
  return UNITY_END();
}
