// LED patterns: the interpreter against the shared golden vectors (firmware/pattern-golden, written by Specter
// Studio's interpreter): every case's lights byte for byte and its plain error at the same step; the program
// check refuses damaged bytes without ever reading outside them; the clue puzzle solved out of order.
#include <unity.h>
#include <dirent.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "proptroller/pattern_math.h"
#include "proptroller/pattern_vm.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static const char* const DIR_ = "../pattern-golden/";

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

static std::vector<std::string> caseNames() {
  std::vector<std::string> v;
  std::string d = std::string(DIR_) + "cases/";
  DIR* dir = opendir(d.c_str());
  if (!dir) return v;
  while (dirent* e = readdir(dir)) {
    std::string n = e->d_name;
    if (n.size() > 5 && n.substr(n.size() - 5) == ".json") v.push_back(d + n);
  }
  closedir(dir);
  std::sort(v.begin(), v.end());
  return v;
}

// the golden vectors' random numbers: xorshift32 from the case's seed, 16 bits a call
struct Xorshift {
  uint32_t x;
  static uint32_t next(void* c) {
    uint32_t& x = ((Xorshift*)c)->x;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x >> 16;
  }
};

static std::string hex(const std::vector<Rgb>& px) {
  static const char* const H = "0123456789abcdef";
  std::string o;
  for (auto& p : px)
    for (uint8_t b : {p.r, p.g, p.b}) { o += H[b >> 4]; o += H[b & 15]; }
  return o;
}

static bool loadProgram(JsonDocument& d, PatternProgram& prog, std::string& why) {
  std::string bytes;
  const char* b64 = d["program"] | "";
  if (!base64Decode(b64, strlen(b64), bytes)) { why = "not base64"; return false; }
  return prog.decode((const uint8_t*)bytes.data(), bytes.size(), why);
}

static void test_limits_match_studio() {
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, readFile(std::string(DIR_) + "limits.json")));
  TEST_ASSERT_EQUAL(PATTERN_MAX_CODE, d["code"].as<int>());
  TEST_ASSERT_EQUAL(PATTERN_MAX_GLOBALS, d["globals"].as<int>());
  TEST_ASSERT_EQUAL(PATTERN_MAX_FUNCTIONS, d["functions"].as<int>());
  TEST_ASSERT_EQUAL(PATTERN_MAX_CONTROLS, d["controls"].as<int>());
  TEST_ASSERT_EQUAL(PATTERN_MAX_STACK, d["stack"].as<int>());
  TEST_ASSERT_EQUAL(PATTERN_MAX_DEPTH, d["depth"].as<int>());
  TEST_ASSERT_EQUAL(PATTERN_MAX_LOCALS, d["locals"].as<int>());
  TEST_ASSERT_EQUAL(PATTERN_MAX_CELLS, d["cells"].as<int>());
  TEST_ASSERT_EQUAL(PATTERN_FRAME_STEPS, d["steps"].as<uint32_t>());
}

static void test_every_golden_case() {
  auto files = caseNames();
  TEST_ASSERT_GREATER_THAN(20, (int)files.size());
  std::string failures;
  for (auto& f : files) {
    JsonDocument d;
    if (deserializeJson(d, readFile(f))) { failures += f + ": unreadable\n"; continue; }
    std::string name = d["name"] | "";
    PatternProgram prog;
    std::string why;
    if (!loadProgram(d, prog, why)) { failures += name + ": refused: " + why + "\n"; continue; }
    Xorshift rng{d["seed"].as<uint32_t>()};
    if (rng.x == 0) rng.x = 1;
    PatternVM vm(prog, Xorshift::next, &rng, nullptr, 0);
    uint16_t pixels = d["pixels"].as<uint16_t>();
    vm.setPixelCount(pixels);
    std::vector<Rgb> px(pixels);
    JsonArrayConst want = d["frames"].as<JsonArrayConst>();
    size_t frame = 0;
    int failed_step = -2;
    if (!vm.init()) failed_step = -1;
    JsonArrayConst steps = d["steps"].as<JsonArrayConst>();
    for (size_t s = 0; failed_step == -2 && s < steps.size(); s++) {
      JsonObjectConst st = steps[s];
      if (!st["frame"].isNull()) {
        if (!vm.frame(st["frame"].as<uint32_t>(), px.data())) { failed_step = (int)s; break; }
        std::string got = hex(px);
        if (frame >= want.size() || got != want[frame].as<std::string>()) {
          failures += name + ": frame " + std::to_string(frame) + " differs\n";
          failed_step = -3;
          break;
        }
        frame++;
      } else {
        std::vector<int32_t> v;
        for (JsonVariantConst x : st["values"].as<JsonArrayConst>()) v.push_back(x.as<int32_t>());
        if (!vm.setControl(st["control"] | "", v.data(), v.size())) { failed_step = (int)s; break; }
      }
    }
    if (failed_step == -3) continue;
    bool want_error = !d["error"].isNull();
    if (want_error != (failed_step != -2)) { failures += name + ": " + (want_error ? "no error, expected one" : "error: " + vm.error()) + "\n"; continue; }
    if (want_error) {
      if (d["error"]["step"].as<int>() != failed_step) failures += name + ": failed at step " + std::to_string(failed_step) + "\n";
      if (vm.error() != d["error"]["message"].as<std::string>()) failures += name + ": message \"" + vm.error() + "\"\n";
    }
    if (frame != want.size()) failures += name + ": " + std::to_string(frame) + " frames of " + std::to_string(want.size()) + "\n";
  }
  if (!failures.empty()) TEST_FAIL_MESSAGE(failures.c_str());
}

// Every single-byte change and every cut of every case's program is either refused or runs safely (the sanitizer
// build, native_asan, catches any read or write outside the program's memory).
static void test_damaged_programs_never_escape() {
  int refused = 0, ran = 0;
  for (auto& f : caseNames()) {
    JsonDocument d;
    TEST_ASSERT_FALSE(deserializeJson(d, readFile(f)));
    std::string bytes;
    const char* b64 = d["program"] | "";
    TEST_ASSERT_TRUE(base64Decode(b64, strlen(b64), bytes));
    if (bytes.size() > 1500) continue;
    uint32_t seed = 1;
    for (size_t i = 0; i <= bytes.size(); i++) {
      std::vector<std::string> variants;
      variants.push_back(bytes.substr(0, i));  // cut short
      if (i < bytes.size())
        for (uint8_t x : {(uint8_t)0x01, (uint8_t)0x80, (uint8_t)0xff}) {
          std::string m = bytes;
          m[i] = (char)(m[i] ^ x);
          variants.push_back(m);
        }
      for (auto& m : variants) {
        PatternProgram p;
        std::string why;
        if (!p.decode((const uint8_t*)m.data(), m.size(), why)) {
          TEST_ASSERT_FALSE(why.empty());
          refused++;
          continue;
        }
        ran++;
        Xorshift rng{seed++};
        PatternVM vm(p, Xorshift::next, &rng, nullptr, 0);
        vm.setPixelCount(8);
        std::vector<Rgb> px(8);
        if (vm.init()) vm.frame(16, px.data());
        int32_t one = 65536;
        for (int c = 0; c < (int)p.controls.size(); c++) vm.callControl(c, &one, 1);
      }
    }
  }
  TEST_ASSERT_GREATER_THAN(1000, refused);
  TEST_ASSERT_GREATER_THAN(10, ran);
}

static void test_plain_refusals() {
  PatternProgram p;
  std::string why;
  const uint8_t junk[] = "hello there, this is not a pattern";
  TEST_ASSERT_FALSE(p.decode(junk, sizeof junk, why));
  TEST_ASSERT_EQUAL_STRING("these are not pattern bytes (send the pattern from Studio)", why.c_str());
  uint8_t newer[22] = {'S', 'P', 'A', 'T', 2};
  TEST_ASSERT_FALSE(p.decode(newer, sizeof newer, why));
  TEST_ASSERT_EQUAL_STRING("the pattern was made by a newer Studio (format 2): update the board's firmware", why.c_str());
  uint8_t big[22] = {'S', 'P', 'A', 'T', 1, 0, 0x01, 0x20, 1, 0, 1, 0};  // 8193 code words
  TEST_ASSERT_FALSE(p.decode(big, sizeof big, why));
  TEST_ASSERT_EQUAL_STRING("the pattern is too big for this board (8193 code steps, at most 4096)", why.c_str());
}

static void test_clue_puzzle_out_of_order() {
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, readFile(std::string(DIR_) + "cases/puzzle-out-of-order.json")));
  PatternProgram p;
  std::string why;
  TEST_ASSERT_TRUE_MESSAGE(loadProgram(d, p, why), why.c_str());
  TEST_ASSERT_EQUAL_STRING("clue1, clue2, clue3, clue4, clue5, clue6 and reset", p.buttonList().c_str());
  TEST_ASSERT_EQUAL(2, p.findButton("Clue3"));
  TEST_ASSERT_EQUAL(6, p.findButton("  RESET "));
  TEST_ASSERT_EQUAL(-1, p.findButton("clue7"));
  Xorshift rng{1};
  PatternVM vm(p, Xorshift::next, &rng, nullptr, 0);
  vm.setPixelCount(60);
  std::vector<Rgb> px(60);
  TEST_ASSERT_TRUE(vm.init());
  auto press = [&](const char* b) { TEST_ASSERT_TRUE(vm.callControl(p.findButton(b), nullptr, 0)); };
  auto count = [&](Rgb c) { return (int)std::count(px.begin(), px.end(), c); };
  const Rgb RED(255, 0, 0), YELLOW(255, 255, 0), GREEN(0, 255, 0);
  TEST_ASSERT_TRUE(vm.frame(20, px.data()));
  TEST_ASSERT_EQUAL(60, count(RED));
  press("clue5");
  press("clue2");
  TEST_ASSERT_TRUE(vm.frame(20, px.data()));
  TEST_ASSERT_EQUAL(20, count(YELLOW));
  TEST_ASSERT_TRUE(px[10] == YELLOW && px[19] == YELLOW && px[40] == YELLOW && px[49] == YELLOW && px[20] == RED);
  for (const char* b : {"clue6", "clue1", "clue4", "clue3"}) press(b);
  TEST_ASSERT_TRUE(vm.frame(20, px.data()));
  TEST_ASSERT_EQUAL(60, count(GREEN));
  press("reset");
  TEST_ASSERT_TRUE(vm.frame(20, px.data()));
  TEST_ASSERT_EQUAL(60, count(RED));
}

static void test_maths_edges() {
  using namespace pmath;
  TEST_ASSERT_EQUAL(ONE, fsin(102944));  // sin(pi / 2)
  TEST_ASSERT_EQUAL(1024 * ONE, fpow(2 * ONE, 10 * ONE));
  TEST_ASSERT_EQUAL(4 * ONE, fsqrt(16 * ONE));
  TEST_ASSERT_EQUAL(INT32_MAX, fdiv(ONE, 0));
  TEST_ASSERT_EQUAL(INT32_MIN, fdiv(-ONE, 0));
  TEST_ASSERT_EQUAL(0, fdiv(0, 0));
  TEST_ASSERT_EQUAL(INT32_MIN, neg(INT32_MIN));
  TEST_ASSERT_EQUAL(-1, fmul(-1, 1));  // toward minus infinity, as a shift
  TEST_ASSERT_EQUAL(32768, smoothstep(0, ONE, 32768));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_limits_match_studio);
  RUN_TEST(test_every_golden_case);
  RUN_TEST(test_damaged_programs_never_escape);
  RUN_TEST(test_plain_refusals);
  RUN_TEST(test_clue_puzzle_out_of_order);
  RUN_TEST(test_maths_edges);
  return UNITY_END();
}
