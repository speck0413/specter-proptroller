// Show scripts: the beginner language (firmware/SHOW-SCRIPT.md). A short text of effects; each one waits for an
// input, then sets outputs, waits, fades, flickers and plays sounds, and goes back to its wait. The text is
// checked once, when it is saved, into a fixed table of steps (SS-C1); the runner plays that table every tick.
#pragma once
#include "types.h"
#include "dfplayer.h"
#include <ArduinoJson.h>
#include <string>
#include <vector>

namespace pt {

// SS-C3 limits (sized for the Pico W).
static const uint8_t SCRIPT_MAX_EFFECTS = 8;
static const uint16_t SCRIPT_MAX_LINES_PER_EFFECT = 64;
static const uint16_t SCRIPT_MAX_LINES = 256;
static const size_t SCRIPT_MAX_BYTES = 8 * 1024;
static const size_t SCRIPT_MAX_NAME = 32;          // characters in an effect name
static const uint16_t SCRIPT_MAX_SOUNDS = 255;     // entries in the sound list
static const uint16_t SCRIPT_MAX_TRACK = 99;       // sound N is /NN/001.mp3 (one file per folder)
static const uint16_t DMX_CHANNELS = 512;
static const uint32_t SCRIPT_MAX_TIME_MS = 24UL * 3600UL * 1000UL;  // one line waits at most a day
static const Millis SCRIPT_BOOT_LOCKOUT_MS = 2000; // no wait for passes after power-on (SM-B2)
// flicker: a new level every FLICKER_MIN_MS to FLICKER_MAX_MS, base x (FLICKER_LOW_PERMILLE + r % ...) / 1000
static const uint32_t FLICKER_MIN_MS = 30, FLICKER_MAX_MS = 120;
static const uint32_t FLICKER_LOW_PERMILLE = 100;
static const uint8_t DMX_FULL = 255;
static const uint32_t FLICKER_SEED = 0x2545F491u;  // xorshift32

// A target: 0-7 the board's outputs (OUT1 ... CC2), SCRIPT_DMX + k the DMX channel k + 1.
static const uint16_t SCRIPT_DMX = 8;
inline bool scriptIsDmx(uint16_t target) { return target >= SCRIPT_DMX; }
// Terminals a person can name (SS-N): the 8 outputs, then the 5 inputs (Settings::names order).
static const uint8_t SCRIPT_NAMED = CHANNEL_COUNT + INPUT_COUNT;

struct ScriptStep {
  enum class Op : uint8_t { Wait, Set, Sleep, Fade, Flicker, Single, Loop, Stop };
  uint32_t ms = 0;      // sleep / fade / flicker time
  uint16_t target = 0;  // input index (Wait) or target
  uint16_t value = 0;   // level: permille for an output, 0-255 for DMX; the sound number
  uint16_t line = 0;    // 1-based line in the text
  Op op = Op::Sleep;
};

struct ScriptEffect {
  char name[SCRIPT_MAX_NAME + 1] = {0};
  uint16_t first = 0, count = 0, line = 0;
};

// The checked table (fixed size: no allocation while it runs).
struct ScriptProgram {
  uint8_t effects = 0;
  ScriptEffect fx[SCRIPT_MAX_EFFECTS];
  uint16_t steps = 0;
  ScriptStep step[SCRIPT_MAX_LINES];
  uint8_t channels = 0;  // bit per Channel the script sets
  uint8_t inputs = 0;    // bit per Input the script waits for
  bool dmx = false;      // a dmx line: the port sends frames
  void reset() {
    effects = 0;
    for (auto& e : fx) e = ScriptEffect();
    steps = 0;
    for (auto& s : step) s = ScriptStep();
    channels = 0;
    inputs = 0;
    dmx = false;
  }
};

struct ScriptMessage {
  uint16_t line;  // 0: the whole script
  std::string text;
};

// What the board tells the checker about itself.
struct ScriptBoard {
  bool dmx_on = false;                          // dmx_port = s4 (SS-D8)
  // rly1_role, rly2_role, cc1_role, cc2_role: 0 shows, 1 trigger_repeat, 2 sound_trigger, 3 none (OutputRole)
  uint8_t roles[4] = {0, 0, 1, 2};
  bool buttons_are_triggers = false;            // user1-3 usable (the board always says true; the golden set also checks false)
  uint8_t effect_inputs = 0, effect_channels = 0;  // terminals the advanced effect owns (SS-R11)
  // SS-N: the names the person gave the terminals, as given (the wiring label, setting <terminal>_name, and the
  // profile's terminal alias), in terminal order; on the heap (the board is built on small stacks)
  struct Name {
    uint8_t terminal;  // 0-7 OUT1 ... CC2, 8-12 TRIG1 ... USER3
    std::string name;
  };
  std::vector<Name> names;
};

typedef std::vector<std::pair<std::string, uint16_t>> SoundList;  // file name -> track (SS-C4)

struct [[nodiscard]] ScriptCheck {
  bool ok() const { return errors.empty(); }
  std::vector<ScriptMessage> errors, warnings;
};

// SS-W4: phone typing cleaned up (line endings, tabs, non-breaking spaces, smart quotes).
std::string scriptClean(const std::string& text);
// Check `text`; on success `out` holds the table. Every error and warning, in line order.
[[nodiscard]] ScriptCheck scriptCompile(const std::string& text, const SoundList& sounds, const ScriptBoard& board,
                          ScriptProgram& out);
// The step table as JSON (firmware/show-script-golden/README.md "The step table").
void scriptTableJson(const ScriptProgram& p, JsonObject out);
// A check with its step table: {"ok", "errors", "warnings"} and "table" when ok (the golden set's form).
void scriptCheckJson(const ScriptCheck& c, const ScriptProgram* table, JsonObject out);
// A check as the protocol sends it (check_script, set_script, get_script's "check", MQTT script/result):
// {"ok", "errors", "warnings"} and, when ok, "effects": how many effects the script has. The step table itself
// stays on the board (PROTOCOL.md "Show scripts").
void scriptReplyJson(const ScriptCheck& c, int effects, JsonObject out);
// The sound list as JSON ({"chime.mp3":1,...}) and back; false on a bad list.
std::string soundListJson(const SoundList& s);
[[nodiscard]] bool soundListParse(const std::string& json, SoundList& out, std::string& why);

// The DMX port (SS-D6-D9): 250 kbaud 8N2 frames of a start code and 512 slots. start() takes the pin, stop()
// gives it back as an input; send() starts one frame and returns at once.
class DmxOut {
 public:
  virtual ~DmxOut() {}
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void send(const uint8_t* slots, size_t n) = 0;
};
static const Millis DMX_FRAME_MS = 25;

// Each input's raw state from the 1 ms samples: closed or open, and since when.
struct ScriptInputs {
  bool closed[INPUT_COUNT] = {false, false, false, false, false};
  Millis since[INPUT_COUNT] = {0, 0, 0, 0, 0};
};

// Plays a table. Owns its outputs' levels and the DMX universe.
class ScriptRunner {
 public:
  // null: no script. Every effect arrives at its first line (its wait for) at `now`.
  void load(const ScriptProgram* p, Millis now);
  const ScriptProgram* program() const { return p_; }
  void setTimes(uint32_t debounce_ms, uint32_t release_ms, Millis boot_end) {
    debounce_ms_ = debounce_ms; release_ms_ = release_ms; boot_end_ = boot_end;
  }
  // Every 1 ms sample: a waiting effect's input counts as released once it has read open without a break
  // for the release time since the effect arrived (SS-R4, SS-R5).
  void sample(Millis now, const ScriptInputs& in);
  // One tick: runs every effect as far as it can go. Sound lines append to `sound`.
  void tick(Millis now, const ScriptInputs& in, std::vector<SoundCommand>& sound);
  Level level(int channel) const { return level_[channel]; }
  const uint8_t* dmx() const { return dmx_; }
  bool running(int effect) const { return st_[effect].running; }
  uint16_t line(int effect) const;  // the line the effect is on
  uint32_t runs(int effect) const { return st_[effect].runs; }

 private:
  struct State {
    uint16_t pc = 0;
    bool entered = false, released = false, running = false;
    Millis since = 0, until = 0, next_flicker = 0;
    Level from = 0, before = 0;
    uint32_t runs = 0;
  };
  void enter(int e, Millis now);
  void advance(int e, Millis now);
  bool stepOnce(int e, Millis now, const ScriptInputs& in, std::vector<SoundCommand>& sound);
  void sampleWait(int e, Millis now, const ScriptInputs& in);
  uint16_t get(uint16_t target) const;
  void put(uint16_t target, uint16_t value);
  uint32_t rnd();
  const ScriptProgram* p_ = nullptr;
  State st_[SCRIPT_MAX_EFFECTS];
  Level level_[CHANNEL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t dmx_[DMX_CHANNELS] = {0};
  uint32_t rng_ = FLICKER_SEED;
  uint32_t debounce_ms_ = TRIGGER_DEBOUNCE_MS, release_ms_ = TRIGGER_DEBOUNCE_MS;
  Millis boot_end_ = SCRIPT_BOOT_LOCKOUT_MS;
};

}  // namespace pt
