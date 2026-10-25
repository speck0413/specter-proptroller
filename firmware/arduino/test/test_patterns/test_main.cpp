// LED patterns on the simulated board, end to end: the protocol ops (list, put, delete, refusals, the page lock),
// the record across power cycles and a factory reset, `pixels = pattern` and `pixels press` in a show script with
// TRIG1, TRIG2, the USER buttons and remote starts, the strip's frames, the plain event-log lines, a runaway
// pattern stopped with the strip dark, the pixel test, and the heavy 600-light pattern's frame time on this host.
#include <unity.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include "proptroller/pattern_vm.h"
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
// a program's bytes (base64) from the shared golden vectors
static std::string program(const char* golden_case) {
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, readFile(std::string("../pattern-golden/cases/") + golden_case + ".json")));
  return d["program"].as<std::string>();
}
static Response put(Sim& b, const std::string& name, const std::string& prog, const char* controls = nullptr, const char* pin = nullptr) {
  JsonDocument d;
  d["op"] = "put_pattern";
  d["name"] = name;
  d["program"] = prog;
  if (pin) d["pin"] = pin;
  if (controls) {
    JsonDocument c;
    deserializeJson(c, controls);
    d["controls"] = c.as<JsonObjectConst>();
  }
  std::string j;
  serializeJson(d, j);
  return b.request(j);
}
static Response setScript(Sim& b, const std::string& text) {
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = text;
  std::string j;
  serializeJson(d, j);
  return b.request(j);
}
static std::string events(Sim& b) {
  std::vector<std::string> ev;
  b.eventsSince(0, ev);
  std::string all;
  for (auto& e : ev) all += e + "\n";
  return all;
}
static bool logged(Sim& b, const std::string& text) { return events(b).find(text) != std::string::npos; }
static int count(Sim& b, Rgb c, size_t from = 0, size_t to = 9999) {
  auto& px = b.world().pixels;
  int n = 0;
  for (size_t i = from; i < px.size() && i <= to; i++) n += px[i] == c;
  return n;
}
static const Rgb RED(255, 0, 0), YELLOW(255, 255, 0), GREEN(0, 255, 0), DARK(0, 0, 0);

static Sim* board() {
  Sim* b = new Sim("p");
  TEST_ASSERT_TRUE(b->request(R"({"op":"set_params","values":[{"key":"pixel_count","value":60},{"key":"pixel_brightness","value":100}]})").isOk());
  return b;
}

static const char* const PUZZLE_SCRIPT =
    "effect puzzle:\n"
    "  pixels = pattern clue puzzle\n"
    "  wait for user3\n"          // USER3: start the puzzle again (the pattern line runs again at the top)
    "effect clue1:\n"
    "  wait for trig1\n"
    "  pixels press clue1\n"
    "effect clue5:\n"
    "  wait for trig2\n"
    "  pixels press clue5\n"
    "effect clue2:\n"
    "  wait for user1\n"
    "  pixels press clue2\n"
    "effect rest:\n"
    "  wait for user2\n"
    "  pixels press clue3\n"
    "  pixels press clue4\n"
    "  pixels press clue6\n";

void ops_store_list_and_delete() {
  std::unique_ptr<Sim> b(board());
  JsonDocument info;
  deserializeJson(info, b->infoJson());
  bool cap = false;
  for (JsonVariantConst c : info["capabilities"].as<JsonArrayConst>()) cap |= c == "patterns";
  TEST_ASSERT_TRUE(cap);
  Response r = put(*b, "  Clue   Puzzle ", program("puzzle-out-of-order"), R"({"triggerClue3":[]})");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  TEST_ASSERT_EQUAL_STRING("pattern_put", r.doc()["result"]["kind"]);
  TEST_ASSERT_EQUAL_STRING("Clue Puzzle", r.doc()["result"]["name"]);
  TEST_ASSERT_EQUAL(7, r.doc()["result"]["buttons"].size());
  TEST_ASSERT_EQUAL_STRING("reset", r.doc()["result"]["buttons"][6]);
  TEST_ASSERT_TRUE(put(*b, "rainbow", program("example-rainbow"), R"({"sliderSpeed":[65536]})").isOk());
  Response l = b->request(R"({"op":"list_patterns"})");
  TEST_ASSERT_EQUAL(2, l.doc()["result"]["patterns"].size());
  TEST_ASSERT_EQUAL_STRING("slider", l.doc()["result"]["patterns"][1]["controls"][0]["kind"]);
  TEST_ASSERT_EQUAL_STRING("Speed", l.doc()["result"]["patterns"][1]["controls"][0]["label"]);
  TEST_ASSERT_EQUAL(8, l.doc()["result"]["max"].as<int>());
  // plain refusals
  TEST_ASSERT_EQUAL_STRING("a pattern's name has letters, digits, spaces and - _ ( ) only",
                           put(*b, "ghost#1", program("example-rainbow")).doc()["error"]["message"]);
  TEST_ASSERT_EQUAL_STRING("these are not pattern bytes (send the pattern from Studio)", put(*b, "x", "aGVsbG8gdGhlcmU=").doc()["error"]["message"]);
  TEST_ASSERT_EQUAL_STRING("the pattern has no control called sliderNope",
                           put(*b, "x", program("example-rainbow"), R"({"sliderNope":[1]})").doc()["error"]["message"]);
  for (int i = 0; i < 6; i++) TEST_ASSERT_TRUE(put(*b, "p" + std::to_string(i), program("example-rainbow")).isOk());
  TEST_ASSERT_EQUAL_STRING("the board keeps at most 8 patterns: delete one first", put(*b, "ninth", program("example-rainbow")).doc()["error"]["message"]);
  // replacing one by its name (any case) keeps the count
  TEST_ASSERT_TRUE(put(*b, "P0", program("example-comet")).isOk());
  TEST_ASSERT_TRUE(b->request(R"({"op":"delete_pattern","name":"p5"})").isOk());
  TEST_ASSERT_EQUAL_STRING("not_found", b->request(R"({"op":"delete_pattern","name":"p5"})").doc()["error"]["code"]);
  TEST_ASSERT_EQUAL(7, b->request(R"({"op":"list_patterns"})").doc()["result"]["patterns"].size());
  TEST_ASSERT_TRUE(logged(*b, "pattern Clue Puzzle stored"));
  TEST_ASSERT_TRUE(logged(*b, "pattern p5 removed"));
}

void the_record_survives_a_power_cycle_and_a_factory_reset_clears_it() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(put(*b, "clue puzzle", program("puzzle-out-of-order")).isOk());
  b->step(100);  // written when the board is quiet (nothing runs here)
  TEST_ASSERT_EQUAL(1, b->patternFlash().saves);
  b->powerCycle();
  Response l = b->request(R"({"op":"list_patterns"})");
  TEST_ASSERT_EQUAL(1, l.doc()["result"]["patterns"].size());
  TEST_ASSERT_EQUAL_STRING("clue puzzle", l.doc()["result"]["patterns"][0]["name"]);
  TEST_ASSERT_TRUE(b->request(R"({"op":"factory_reset"})").isOk());
  b->step(100);
  b->powerCycle();
  TEST_ASSERT_EQUAL(0, b->request(R"({"op":"list_patterns"})").doc()["result"]["patterns"].size());
}

void a_damaged_record_is_kept_and_refuses_changes() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(b->patternFlash().save("PTPN\x01\x00\x00\x00\xff\xff\x00\x00junk"));
  b->powerCycle();
  TEST_ASSERT_FALSE(b->ctl().recordsOk());
  Response r = put(*b, "x", program("example-rainbow"));
  TEST_ASSERT_EQUAL_STRING("refused", r.doc()["error"]["code"]);
  TEST_ASSERT_EQUAL_STRING("the stored LED patterns could not be read and are kept: factory reset first, then send patterns",
                           r.doc()["error"]["message"]);
}

void the_page_lock_guards_changes_not_the_list() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(b->request(R"({"op":"set_params","values":[{"key":"pin","value":"secret12"},{"key":"page_lock","value":true}]})").isOk());
  TEST_ASSERT_EQUAL_STRING("pin", put(*b, "x", program("example-rainbow")).doc()["error"]["code"]);
  TEST_ASSERT_TRUE(b->request(R"({"op":"list_patterns"})").isOk());
  b->step(1100);  // the wrong password's wait
  TEST_ASSERT_TRUE(put(*b, "x", program("example-rainbow"), nullptr, "secret12").isOk());
  TEST_ASSERT_EQUAL_STRING("pin", b->request(R"({"op":"delete_pattern","name":"x"})").doc()["error"]["code"]);
}

// The owner's puzzle: red until solved, clues in any order each light their own stretch, all of them green;
// TRIG1, TRIG2, USER1, USER2 and a remote start press the buttons, USER3 starts it again.
void the_clue_puzzle_solved_out_of_order() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(put(*b, "clue puzzle", program("puzzle-out-of-order")).isOk());
  Response s = setScript(*b, PUZZLE_SCRIPT);
  TEST_ASSERT_TRUE_MESSAGE(s.doc()["result"]["ok"].as<bool>(), s.json().c_str());
  b->step(2500);  // the boot lockout
  TEST_ASSERT_EQUAL(60, count(*b, RED));
  b->press(Input::Trig2, 150);  // clue 5
  b->step(100);
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 40, 49));
  TEST_ASSERT_EQUAL(50, count(*b, RED));
  b->press(Input::Trig1, 150);  // clue 1
  b->step(100);
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 0, 9));
  // a remote start of an effect (the phone page, MQTT, another board) presses its button like its input
  // (here an effect that starts remotely without the remote trigger: remote_free_effects)
  Response en = b->request(R"({"op":"set_params","values":[{"key":"remote_free_effects","value":"clue2"}]})");
  TEST_ASSERT_TRUE_MESSAGE(en.isOk(), en.json().c_str());
  Response a = b->request(R"({"op":"action","key":"clue2"})");
  TEST_ASSERT_TRUE_MESSAGE(a.isOk(), a.json().c_str());
  b->step(100);
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 10, 19));
  TEST_ASSERT_EQUAL(30, count(*b, YELLOW));
  // three presses in one run, one after another in the same moment: all land, in order
  b->press(Input::User2, 150);
  b->step(100);
  TEST_ASSERT_EQUAL(60, count(*b, GREEN));
  // USER3: the pattern line runs again, the puzzle starts fresh
  b->press(Input::User3, 150);
  b->step(100);
  TEST_ASSERT_EQUAL(60, count(*b, RED));
  b->press(Input::User1, 150);  // clue 2 after the restart
  b->step(100);
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 10, 19));
  TEST_ASSERT_EQUAL(50, count(*b, RED));
}

void the_checker_knows_a_stored_patterns_buttons() {
  std::unique_ptr<Sim> b(board());
  const std::string text = "effect a:\n  pixels = pattern clue puzzle\n  wait for trig1\n  pixels press clue9\n";
  // not stored: the checker cannot know its buttons
  TEST_ASSERT_TRUE(setScript(*b, text).doc()["result"]["ok"].as<bool>());
  TEST_ASSERT_TRUE(put(*b, "clue puzzle", program("puzzle-out-of-order")).isOk());
  Response r = b->request(R"({"op":"check_script","text":"effect a:\n  pixels = pattern clue puzzle\n  wait for trig1\n  pixels press clue9\n"})");
  TEST_ASSERT_FALSE(r.doc()["result"]["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("line 4: pattern \"clue puzzle\" has no button \"clue9\". Its buttons are clue1, clue2, clue3, clue4, clue5, clue6 and reset.",
                           r.doc()["result"]["errors"][0]["message"]);
  // the stored script still runs; at run time the press is told and ignored, the show goes on
  b->step(2500);
  b->press(Input::Trig1, 150);
  b->step(100);
  TEST_ASSERT_TRUE(logged(*b, "pixels press clue9 ignored: pattern clue puzzle has no button clue9 (its buttons are clue1, clue2, clue3, clue4, clue5, clue6 and reset)"));
  TEST_ASSERT_EQUAL(60, count(*b, RED));
}

void a_missing_pattern_and_a_press_on_another_effect_are_told() {
  std::unique_ptr<Sim> b(board());
  Response s = setScript(*b, "effect a:\n  pixels = pattern ghost\n  wait for trig1\n  pixels = rainbow\n  pixels press clue1\n");
  TEST_ASSERT_TRUE(s.doc()["result"]["ok"].as<bool>());
  b->step(2500);
  TEST_ASSERT_TRUE(logged(*b, "pattern ghost is not on this board: the strip stays dark (send it from Studio)"));
  TEST_ASSERT_EQUAL(60, count(*b, DARK));
  b->press(Input::Trig1, 150);
  b->step(100);
  TEST_ASSERT_TRUE(logged(*b, "pixels press clue1 ignored: the strip is not showing a pattern"));
  // the run ended back at the effect's top, where the pattern line ran again: sending the pattern lets it play
  TEST_ASSERT_EQUAL(60, count(*b, DARK));
  TEST_ASSERT_TRUE(put(*b, "ghost", program("puzzle-out-of-order")).isOk());
  b->step(100);
  TEST_ASSERT_EQUAL(60, count(*b, RED));
}

void a_runaway_pattern_stops_with_the_strip_dark_and_the_show_goes_on() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(put(*b, "forever", program("error-endless-render")).isOk());
  TEST_ASSERT_TRUE(setScript(*b, "effect a:\n  pixels = pattern forever\n  wait for trig1\n  out1 = on\n  wait 1\n  out1 = off\n").doc()["result"]["ok"].as<bool>());
  b->step(2500);
  TEST_ASSERT_TRUE(logged(*b, "pattern forever stopped: the pattern took too long on one frame and was stopped (a loop that never ends?). The strip is dark until the pattern starts again"));
  TEST_ASSERT_EQUAL(60, count(*b, DARK));
  b->press(Input::Trig1, 150);
  b->step(200);
  TEST_ASSERT_EQUAL(1000, b->level(Channel::Out1));  // the show runs as ever
}

void the_pixel_test_plays_a_stored_pattern() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(put(*b, "clue puzzle", program("puzzle-out-of-order")).isOk());
  TEST_ASSERT_TRUE(b->request(R"({"op":"pixel","action":{"target":"PIXEL","do":"pattern","name":"Clue Puzzle"}})").isOk());
  b->step(100);
  TEST_ASSERT_EQUAL(60, count(*b, RED));
  TEST_ASSERT_EQUAL_STRING("not_found", b->request(R"({"op":"pixel","action":{"target":"PIXEL","do":"pattern","name":"nope"}})").doc()["error"]["code"]);
  TEST_ASSERT_TRUE(b->request(R"({"op":"pixel","action":null})").isOk());
  b->step(100);
  TEST_ASSERT_EQUAL(60, count(*b, DARK));
}

void control_values_sent_with_a_pattern_apply_at_its_start() {
  std::unique_ptr<Sim> b(board());
  // clue 3 already solved when it starts
  TEST_ASSERT_TRUE(put(*b, "clue puzzle", program("puzzle-out-of-order"), R"({"triggerClue3":[]})").isOk());
  TEST_ASSERT_TRUE(b->request(R"({"op":"pixel","action":{"target":"PIXEL","do":"pattern","name":"clue puzzle"}})").isOk());
  b->step(100);
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 20, 29));
}

// A pattern that moves keeps moving: it starts once per pixels line (or pixel test), not at every tick.
static int brightest(Sim& b) {
  auto& px = b.world().pixels;
  int at = -1, best = -1;
  for (size_t i = 0; i < px.size(); i++)
    if (px[i].b > best) { best = px[i].b; at = (int)i; }
  return at;
}
void a_moving_pattern_keeps_moving() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(put(*b, "comet", program("example-comet")).isOk());
  TEST_ASSERT_TRUE(setScript(*b, "effect a:\n  pixels = pattern comet\n  wait for trig1\n").doc()["result"]["ok"].as<bool>());
  b->step(2500);
  int a0 = brightest(*b);
  b->step(300);
  int a1 = brightest(*b);
  TEST_ASSERT_TRUE_MESSAGE(a1 - a0 >= 8, "the comet moves about 10 lights in 300 ms");
  // the pixel test too
  TEST_ASSERT_TRUE(b->request(R"({"op":"pixel","action":{"target":"PIXEL","do":"pattern","name":"comet"}})").isOk());
  b->step(30);
  int p0 = brightest(*b);
  b->step(300);
  TEST_ASSERT_TRUE(brightest(*b) - p0 >= 8);
}

// The presses of one tick land in order: clue 1 then reset ends red; reset then clue 1 ends with clue 1 lit. Two
// effects started in the same millisecond both land.
void presses_in_one_moment_land_in_order() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(put(*b, "clue puzzle", program("puzzle-out-of-order")).isOk());
  TEST_ASSERT_TRUE(setScript(*b, "effect p:\n  pixels = pattern clue puzzle\n  wait for user3\n"
                                 "effect a:\n  wait for trig1\n  pixels press clue1\n  pixels press reset\n"
                                 "effect b:\n  wait for user1\n  pixels press reset\n  pixels press clue1\n"
                                 "effect c:\n  wait for trig2\n  pixels press clue5\n"
                                 "effect d:\n  wait for user2\n  pixels press clue6\n").doc()["result"]["ok"].as<bool>());
  b->step(2500);
  b->press(Input::Trig1, 150);
  b->step(60);
  TEST_ASSERT_EQUAL(60, count(*b, RED));
  b->press(Input::User1, 150);
  b->step(60);
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 0, 9));
  // TRIG2 and USER2 closed in the same millisecond
  b->setInput(Input::Trig2, true);
  b->setInput(Input::User2, true);
  b->step(150);
  b->setInput(Input::Trig2, false);
  b->setInput(Input::User2, false);
  b->step(60);
  TEST_ASSERT_EQUAL(30, count(*b, YELLOW));
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 40, 49));
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 50, 59));
}

// A pattern sent again while the pixel test shows it: the test follows the new copy; deleted: dark.
void the_pixel_test_follows_a_pattern_sent_again() {
  std::unique_ptr<Sim> b(board());
  TEST_ASSERT_TRUE(put(*b, "x", program("puzzle-out-of-order")).isOk());
  TEST_ASSERT_TRUE(b->request(R"({"op":"pixel","action":{"target":"PIXEL","do":"pattern","name":"x"}})").isOk());
  b->step(60);
  TEST_ASSERT_EQUAL(60, count(*b, RED));
  TEST_ASSERT_TRUE(put(*b, "x", program("puzzle-out-of-order"), R"({"triggerClue1":[]})").isOk());
  b->step(60);
  TEST_ASSERT_EQUAL(10, count(*b, YELLOW, 0, 9));
  TEST_ASSERT_TRUE(b->request(R"({"op":"delete_pattern","name":"x"})").isOk());
  b->step(60);
  TEST_ASSERT_EQUAL(60, count(*b, DARK));
}

// The heavy pattern on 600 lights: its frame time on this computer (the board's own numbers come from `mem`).
void the_heavy_pattern_on_600_lights() {
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, readFile("../pattern-golden/cases/heavy-600.json")));
  std::string bytes;
  std::string b64 = d["program"].as<std::string>();
  TEST_ASSERT_TRUE(base64Decode(b64.data(), b64.size(), bytes));
  PatternProgram p;
  std::string why;
  TEST_ASSERT_TRUE(p.decode((const uint8_t*)bytes.data(), bytes.size(), why));
  uint32_t seed = 1;
  PatternVM vm(p, [](void* c) { uint32_t& x = *(uint32_t*)c; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x >> 16; }, &seed, nullptr, 0);
  vm.setPixelCount(600);
  std::vector<Rgb> px(600);
  TEST_ASSERT_TRUE(vm.init());
  auto t0 = std::chrono::steady_clock::now();
  const int FRAMES = 200;
  uint32_t steps = 0;
  for (int i = 0; i < FRAMES; i++) {
    TEST_ASSERT_TRUE(vm.frame(20, px.data()));
    steps = std::max(steps, vm.takeMaxSteps());
  }
  double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / FRAMES;
  char m[160];
  snprintf(m, sizeof m, "heavy-600: %.0f us a frame on this host, %u steps a frame (budget %u)", us, (unsigned)steps, (unsigned)PATTERN_FRAME_STEPS);
  TEST_MESSAGE(m);
  TEST_ASSERT_LESS_THAN(PATTERN_FRAME_STEPS, steps);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(ops_store_list_and_delete);
  RUN_TEST(the_record_survives_a_power_cycle_and_a_factory_reset_clears_it);
  RUN_TEST(a_damaged_record_is_kept_and_refuses_changes);
  RUN_TEST(the_page_lock_guards_changes_not_the_list);
  RUN_TEST(the_clue_puzzle_solved_out_of_order);
  RUN_TEST(the_checker_knows_a_stored_patterns_buttons);
  RUN_TEST(a_missing_pattern_and_a_press_on_another_effect_are_told);
  RUN_TEST(a_runaway_pattern_stops_with_the_strip_dark_and_the_show_goes_on);
  RUN_TEST(the_pixel_test_plays_a_stored_pattern);
  RUN_TEST(control_values_sent_with_a_pattern_apply_at_its_start);
  RUN_TEST(a_moving_pattern_keeps_moving);
  RUN_TEST(presses_in_one_moment_land_in_order);
  RUN_TEST(the_pixel_test_follows_a_pattern_sent_again);
  RUN_TEST(the_heavy_pattern_on_600_lights);
  return UNITY_END();
}
