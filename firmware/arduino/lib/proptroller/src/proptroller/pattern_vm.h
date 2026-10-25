// LED patterns (firmware/SHOW-SCRIPT.md "The pixel strip", firmware/PROTOCOL.md "LED patterns"): Specter Studio
// compiles a pattern to a small program of numbers and sends its bytes; the board never compiles source. This is
// the program (read and checked from its bytes) and its interpreter, step for step and bit for bit the same as
// Studio's src/patterns/vm.ts: firmware/pattern-golden holds the shared vectors (test/test_pattern_vm).
//
// Every limit is fixed (the PATTERN_MAX_* below, the same numbers as Studio's BOARD_LIMITS and
// pattern-golden/limits.json). A program is checked once when it is received or loaded: every step, jump, index
// and the stack height everywhere, so a damaged or hostile program can never read or write outside its memory.
// While it runs, a frame has a budget of steps and a deadline; a pattern that overruns stops with a plain message.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "ws2812.h"

// The player is built for the boards with room and a core to spare for it: the ESP32 and ESP32-S3 (two cores) and
// the Pico 2 W (and the host). The Pico W and the plain Pico (RP2040) have no room; the ESP32-C3 has one core, which
// the 10 ms step and the 1 ms samples share with everything else: no pattern player there either.
#if defined(ESP32) && __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif
#if !defined(PT_PATTERNS)
#if defined(ARDUINO_ARCH_RP2040) && !defined(PICO_RP2350)
#define PT_PATTERNS 0
#elif defined(ESP32) && (defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_FREERTOS_UNICORE))
#define PT_PATTERNS 0
#else
#define PT_PATTERNS 1
#endif
#endif

namespace pt {

static const uint16_t PATTERN_MAX_CODE = 4096;       // code words
static const uint16_t PATTERN_MAX_GLOBALS = 256;
static const uint16_t PATTERN_MAX_FUNCTIONS = 128;
static const uint16_t PATTERN_MAX_CONTROLS = 16;
static const uint16_t PATTERN_MAX_STACK = 256;       // values being worked on at once
static const uint16_t PATTERN_MAX_DEPTH = 12;        // functions calling each other at once (the native stack: 12 x about 350 B)
static const uint16_t PATTERN_MAX_LOCALS = 512;      // the values of every function running at once
static const uint16_t PATTERN_MAX_CELLS = 2048;      // list values in all, one more per list
static const uint32_t PATTERN_FRAME_STEPS = 98304;   // steps one frame (or one control call) may take
static const uint16_t PATTERN_MAX_PIXELS = 4096;
static const uint8_t PATTERN_FORMAT = 1;
static const uint8_t PATTERN_MAX_TEXT = 63;          // a control's name or label

enum class PatternControlKind : uint8_t { Slider, HsvPicker, RgbPicker, Toggle, Trigger, InputNumber };
const char* patternControlKindName(PatternControlKind k);

struct PatternControl {
  PatternControlKind kind = PatternControlKind::Slider;
  uint16_t fn = 0;
  std::string name, label;
};

struct PatternFunction {
  uint16_t addr = 0, end = 0;  // its code: [addr, end)
  uint8_t params = 0, locals = 0;
};

// A program read from its bytes and checked.
class PatternProgram {
 public:
  // False with the reason in plain words when the bytes are not a program this board can run.
  [[nodiscard]] bool decode(const uint8_t* b, size_t n, std::string& why);
  std::vector<int32_t> code;
  std::vector<PatternFunction> fns;
  std::vector<PatternControl> controls;
  uint16_t globals = 1;
  int32_t before_render = -1, render = -1, render2d = -1, render3d = -1;
  // The control a script's `pixels press <button>` means: a trigger whose label matches (lower case, runs of
  // spaces made one), -1 when none does.
  int findButton(const std::string& words) const;
  // The trigger buttons' labels, lower case ("clue1, clue2 and reset"), for messages.
  std::string buttonList() const;
  int findControl(const std::string& name) const;
  // RAM the program itself takes (code, functions, controls).
  size_t bytes() const;

 private:
  bool verify(std::string& why);
};

// Base64 (the program's bytes travel as text in a request); false when `in` is not base64.
[[nodiscard]] bool base64Decode(const char* in, size_t n, std::string& out);

// A pattern's words compared the way a script writes them: lower case, runs of spaces made one, trimmed.
std::string patternWords(const std::string& s);

struct PatternVal {
  int32_t v = 0;
  uint8_t t = 0;  // 0 a number, 1 a list (v: its first cell, which holds the length), 2 a function (v: its index)
};

class PatternVM {
 public:
  // `random16`: a random whole number 0-65535 (the board's own source; the golden test's xorshift); `us`: a
  // microsecond clock for the deadline (nullptr: no deadline, only the step budget).
  PatternVM(const PatternProgram& prog, uint32_t (*random16)(void*), void* random_ctx, uint32_t (*us)(), uint32_t frame_us);
  ~PatternVM();
  PatternVM(const PatternVM&) = delete;
  PatternVM& operator=(const PatternVM&) = delete;

  void setPixelCount(uint16_t n);
  uint16_t pixelCount() const { return pixels_; }
  // The top-level code, once. False when it failed (error()).
  bool init();
  // One frame of `delta_ms`: beforeRender, then render for every light into `out` (pixelCount() lights).
  bool frame(uint32_t delta_ms, Rgb* out);
  // A control's function with its values (16.16); an unknown name does nothing.
  bool setControl(const std::string& name, const int32_t* values, size_t n);
  bool callControl(int index, const int32_t* values, size_t n);
  bool failed() const { return !err_.empty(); }
  const std::string& error() const { return err_; }
  // Heap the interpreter took for this program (globals, lists, stack, locals).
  size_t bytes() const;
  // The most steps one call (a frame) took since the last read (then cleared), for `mem`.
  uint32_t takeMaxSteps() { uint32_t m = max_steps_; max_steps_ = 0; return m; }

 private:
  PatternVal exec(uint16_t fi, const PatternVal* args, int nargs);
  PatternVal builtin(int32_t id, const PatternVal* a, int n, uint32_t pc);
  PatternVal call(const PatternVal& f, const PatternVal* args, int n, uint32_t pc);
  void start();
  void endCall();
  void fail(const char* msg);
  void fail(const std::string& msg) { fail(msg.c_str()); }
  // the value as a number / a list; false (and the plain message) when it is not one
  bool num(const PatternVal& x, int32_t& out) {
    if (x.t == 0) { out = x.v; return true; }
    notNum(x);
    return false;
  }
  bool list(const PatternVal& x, int32_t& at) {
    if (x.t == 1) { at = x.v; return true; }
    notList();
    return false;
  }
  void notNum(const PatternVal& x);
  void notList();
  bool newList(int32_t len, int32_t& at);
  bool timeUp() const;
  void sortList(int32_t at);

  const PatternProgram& p_;
  uint32_t (*random16_)(void*);
  void* random_ctx_;
  uint32_t (*us_)();
  uint32_t frame_us_;
  uint32_t deadline_ = 0;
  uint16_t pixels_ = 16;
  uint64_t clock_ = 0;
  PatternVal* G_ = nullptr;
  PatternVal* S_ = nullptr;
  PatternVal* L_ = nullptr;
  PatternVal* cells_ = nullptr;
  int32_t cells_used_ = 0;
  int sp_ = 0;
  int depth_ = 0;
  int locals_used_ = 0;
  int32_t left_ = 0;
  uint32_t spent_ = 0;
  uint32_t max_steps_ = 0;
  int32_t seed_ = 0x2545f491;
  int32_t r_ = 0, g_ = 0, b_ = 0;
  std::string err_;
};

}  // namespace pt
