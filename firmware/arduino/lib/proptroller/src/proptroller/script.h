// Show scripts: the beginner language (firmware/SHOW-SCRIPT.md). A short text of effects; each one waits for an
// input, then sets outputs, waits, fades, flickers and plays sounds, and goes back to its wait. The text is
// checked once, when it is saved, into a fixed table of steps (SS-C1); the runner plays that table every tick.
#pragma once
#include "types.h"
#include "dfplayer.h"
#include "gate.h"
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
static const uint16_t SCRIPT_MAX_SOUNDS = 255;     // entries in the sound list (accepted and kept, not used: sounds are numbers)
static const uint16_t SCRIPT_MAX_TRACK = 99;       // sound N (sound.h SOUND_MAX_TRACK)
static const uint16_t DMX_CHANNELS = 512;
static const uint32_t SCRIPT_MAX_TIME_MS = 24UL * 3600UL * 1000UL;  // one line waits at most a day
static const Millis SCRIPT_BOOT_LOCKOUT_MS = 2000; // no wait for passes after power-on (SM-B2)
// flicker: a new level every FLICKER_MIN_MS to FLICKER_MAX_MS, base x (FLICKER_LOW_PERMILLE + r % ...) / 1000
static const uint32_t FLICKER_MIN_MS = 30, FLICKER_MAX_MS = 120;
static const uint32_t FLICKER_LOW_PERMILLE = 100;
static const uint8_t DMX_FULL = 255;
static const uint32_t FLICKER_SEED = 0x2545F491u;  // xorshift32

// Gates and limits (SHOW-SCRIPT.md §9, the advanced lines).
// A limit: up to SCRIPT_LIMIT_MAX_RUNS runs in a window of at most SCRIPT_LIMIT_MAX_MS (the effect's own ranges).
static const uint32_t SCRIPT_LIMIT_MAX_RUNS = 20;
static const uint32_t SCRIPT_LIMIT_MAX_MS = 3600000UL;
// The settings a line may name for a time (wait, pulse, a limit's window) or a count (a limit's runs), read when
// the line runs; a profile's key alias of one works too.
static const uint8_t SCRIPT_SETTING_COUNT = 8;
extern const char* const SCRIPT_SETTINGS[SCRIPT_SETTING_COUNT];
static const uint8_t SCRIPT_SETTING_RUNS = 7;  // runs_per_cooldown: the one count; the others are times in ms

// The pixel strip's line (`pixels = chase red`, SHOW-SCRIPT.md): its effects (in Effect::Kind's order) and the
// colour names; an exact colour is 0xRRGGBB (# starts a comment).
static const uint8_t SCRIPT_PIXEL_EFFECT_COUNT = 11;
extern const char* const SCRIPT_PIXEL_EFFECTS[SCRIPT_PIXEL_EFFECT_COUNT];
struct ScriptColour {
  const char* name;
  uint32_t rgb;
};
static const uint8_t SCRIPT_COLOUR_COUNT = 10;
extern const ScriptColour SCRIPT_COLOURS[SCRIPT_COLOUR_COUNT];
static const uint32_t SCRIPT_PIXEL_DEFAULT_RGB = 0xFFFFFF;  // an effect written without a colour: white
// Does the effect take a colour (rainbow and fire have their own, off none)?
bool scriptPixelColoured(uint8_t effect);
// The words of the language for an editor (firmware/web/words.js mirrors them; test_script checks): outputs,
// inputs, the settings a line may name.
extern const char* const SCRIPT_OUTPUT_WORDS[CHANNEL_COUNT];
extern const char* const SCRIPT_INPUT_WORDS[INPUT_COUNT];
// the words that make or join a line (effect, wait, for, fade, to, over, ... pixels), the level words, the time units
static const uint8_t SCRIPT_KEYWORD_COUNT = 18;
extern const char* const SCRIPT_KEYWORDS[SCRIPT_KEYWORD_COUNT];
extern const char* const SCRIPT_LEVEL_WORDS[2];  // on, off
struct ScriptUnit {
  const char* name;
  uint32_t ms;
};
static const uint8_t SCRIPT_UNIT_COUNT = 10;
extern const ScriptUnit SCRIPT_TIME_UNITS[SCRIPT_UNIT_COUNT];

// A target: 0-7 the board's outputs (OUT1 ... CC2), SCRIPT_DMX + k the DMX channel k + 1.
static const uint16_t SCRIPT_DMX = 8;
inline bool scriptIsDmx(uint16_t target) { return target >= SCRIPT_DMX; }
// Terminals a person can name (SS-N): the 8 outputs, then the 5 inputs (Settings::names order).
static const uint8_t SCRIPT_NAMED = CHANNEL_COUNT + INPUT_COUNT;

struct ScriptStep {
  // Pixels (a `pixels = <effect> <colour>` line): target the effect (SCRIPT_PIXEL_EFFECTS, Effect::Kind's order),
  // ms the colour 0xRRGGBB.
  enum class Op : uint8_t { Wait, Set, Sleep, Fade, Flicker, Single, Loop, Stop, Pulse, LoopOff, Volume, Pixels };
  uint32_t ms = 0;      // wait / fade / flicker / pulse time
  uint16_t target = 0;  // input index (Wait) or target
  uint16_t value = 0;   // level: permille for an output, 0-255 for DMX; the sound number; the volume 0-30
  uint16_t line = 0;    // 1-based line in the text
  Op op = Op::Sleep;
  uint8_t setting = 0;  // wait / pulse: 0 the time is `ms`; k + 1 the setting SCRIPT_SETTINGS[k], read when it runs
};

struct ScriptEffect {
  char name[SCRIPT_MAX_NAME + 1] = {0};
  uint16_t first = 0, count = 0, line = 0;
  bool limit = false;          // "limit N runs per T"
  uint8_t limit_runs = 0;      // N (or 0 with limit_runs_setting)
  uint8_t limit_runs_setting = 0, limit_ms_setting = 0;  // k + 1: the setting SCRIPT_SETTINGS[k]
  uint32_t limit_ms = 0;       // T (or 0 with limit_ms_setting)
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
  bool pixels = false;   // a pixels line
  void reset() {
    effects = 0;
    for (auto& e : fx) e = ScriptEffect();
    steps = 0;
    for (auto& s : step) s = ScriptStep();
    channels = 0;
    inputs = 0;
    dmx = false;
    pixels = false;
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
  bool ready_lamp = false;                      // OUT4 is the ready lamp of the script's first effect (out4_ready_lamp)
  // the profile's key aliases of the settings a line may name (alias, key)
  std::vector<std::pair<std::string, std::string>> setting_aliases;
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
  // a test press not in `closed` (remote start off): only the effects that start remotely without the remote
  // trigger (remote_free_effects) see it, joined with the pin (ScriptRunner::view)
  bool test[INPUT_COUNT] = {false, false, false, false, false};
  Millis test_since[INPUT_COUNT] = {0, 0, 0, 0, 0};
  bool anyTest() const { for (bool t : test) if (t) return true; return false; }
};

// Plays a table. Owns its outputs' levels and the DMX universe.
class ScriptRunner {
 public:
  // null: no script. Every effect arrives at its top (its sound lines, then its first wait for) at `now`.
  void load(const ScriptProgram* p, Millis now);
  const ScriptProgram* program() const { return p_; }
  void setTimes(uint32_t debounce_ms, uint32_t release_ms, Millis boot_end) {
    debounce_ms_ = debounce_ms; release_ms_ = release_ms; boot_end_ = boot_end;
  }
  // The values of SCRIPT_SETTINGS now (a changed one applies from the next line that reads it).
  void setSettings(const uint32_t values[SCRIPT_SETTING_COUNT]);
  // Paused (STOP / the pause input): nothing starts or steps.
  void setPaused(bool p) { paused_ = p; }
  // remote_trigger_enable now, and the effects (bit per effect) that start remotely without it: a queued remote
  // start that needs the enable is dropped when it goes off (SM-Q5).
  void setRemote(bool enabled, uint8_t free_effects);
  // The start rule of a trigger input's first wait for (trig1_wait / trig2_wait): false "return" (SS-R4: a press,
  // after a release, both debounced as the library's Trigger reads them; one that comes while the effect
  // cannot start, its own run included, is remembered one deep; none in the boot lockout), true "ready" (the
  // pulse-and-recover effect's, SM-T: a release since the last run, held while the effect could start).
  // Not set (a runner on its own): the beginner rule, as for USER1-3.
  void setReadyRule(int input, bool ready) { if (input >= 0 && input < 2) { ready_rule_[input] = ready; rule_set_[input] = true; } }
  // The effect called `name` (lower case, runs of spaces made one; "_" for a space works too), or -1.
  int effectNamed(const std::string& name) const;
  // What a remote start did: it starts (on the next sample), it is queued (one), or why not.
  enum class Answer : uint8_t { Starts, Queued, AlreadyQueued, Disabled, CooldownFull, NoEffect };
  // A remote start of effect e (web, MQTT, serial, SM-Q): now when it can start, else one waits. `allowed`:
  // remote_trigger_enable is on, or the effect starts without it.
  Answer remoteStart(int e, Millis now, bool allowed);
  bool cancelRemote();  // every queued remote start
  // For status: could start now, runs left in its window, the window's time left, a start waiting.
  bool ready(int effect, Millis now) const;
  // could start now and a press would start it (released since: the ready lamp)
  bool armed(int effect, Millis now) const;
  bool hasLimit(int effect) const { return p_ && effect < p_->effects && p_->fx[effect].limit; }
  uint32_t runsLeft(int effect, Millis now) const { return gate_[effect].runsLeft(now); }
  uint32_t cooldownRemaining(int effect, Millis now) const { return gate_[effect].cooldownRemaining(now); }
  bool windowFull(int effect, Millis now) const { return p_ && p_->fx[effect].limit && gate_[effect].windowFull(now); }
  const RunGate& gate(int effect) const { return gate_[effect]; }
  bool queued(int effect) const { return queued_[effect]; }
  bool remembered(int effect) const { return pending_[effect]; }
  bool anyQueued() const;
  uint32_t totalRuns() const;
  // Every 1 ms sample: the inputs, and every effect steps as far as it can go (SS-R1). Sound lines are handed
  // out at the next tick.
  void sample(Millis now, const ScriptInputs& in);
  // One tick: pulses that are over end, and the sound lines since the last tick append to `sound` (audio_loop is
  // soundLoop(n), turned off soundLoop(0); audio_volume is a Volume command).
  void tick(Millis now, const ScriptInputs& in, std::vector<SoundCommand>& sound);
  Level level(int channel) const { return level_[channel]; }
  const uint8_t* dmx() const { return dmx_; }
  bool running(int effect) const { return st_[effect].running; }
  // The strip's effect the script set last (its pixels line) and when that line ran; false while no line has set
  // one. It stays until another pixels line (SHOW-SCRIPT.md: like audio_loop).
  bool pixels(const ScriptStep*& step, Millis& since) const {
    if (!p_ || pixel_step_ < 0) return false;
    step = &p_->step[pixel_step_];
    since = pixel_since_;
    return true;
  }
  uint16_t line(int effect) const;  // the line the effect is on
  uint32_t runs(int effect) const { return st_[effect].runs; }
  // A run of an effect that turns OUT2 on (its recovery) is before that line: the status value `pulsing` (SS-R13)
  bool beforeRecovery(int effect) const { return st_[effect].running && recovery_[effect] && st_[effect].pc < recovery_[effect]; }

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
  // the inputs as effect e sees them: a test press that starts nothing reaches only the effects in remote_free_effects
  ScriptInputs view(int e, const ScriptInputs& in) const;
  bool busy(int e) const;                     // an effect using one of its outputs runs (SS-R8)
  bool atTop(int e) const { return st_[e].pc == head(e) && !st_[e].running; }
  bool canStart(int e, Millis now) const;
  bool readyRule(int e) const;
  bool returnRule(int e) const;               // a trigger input's first wait for with trig1_wait / trig2_wait = return
  uint16_t head(int e) const;                 // the step of the effect's first wait for
  void run(Millis now, const ScriptInputs& in, std::vector<SoundCommand>& sound);
  void start(int e, Millis now);
  uint32_t timeOf(const ScriptStep& st) const { return st.setting ? values_[st.setting - 1] : st.ms; }
  uint32_t limitRuns(int e) const;
  uint32_t limitMs(int e) const;
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
  RunGate gate_[SCRIPT_MAX_EFFECTS];                // each effect's window, and the "ready" rule's release and confirm
  uint8_t conflicts_[SCRIPT_MAX_EFFECTS] = {0};     // bit per effect that uses one of the same outputs
  bool local_go_[SCRIPT_MAX_EFFECTS] = {false};     // the "ready" rule's hold to confirm is complete
  bool pending_[SCRIPT_MAX_EFFECTS] = {false};      // a start that came while its outputs were busy (one deep)
  bool queued_[SCRIPT_MAX_EFFECTS] = {false};       // a remote start waiting (one deep, SM-Q1)
  bool ready_rule_[2] = {false, false};             // trig1_wait, trig2_wait = ready
  bool rule_set_[2] = {false, false};
  uint8_t order_[SCRIPT_MAX_EFFECTS] = {0};
  uint16_t recovery_[SCRIPT_MAX_EFFECTS] = {0};     // 1 + the step of its first line turning OUT2 on, 0 = none (load)         // the effects by their first wait for's input (load)               // trig1_wait, trig2_wait given (ready or return)
  bool press_armed_[SCRIPT_MAX_EFFECTS] = {false};  // the "return" rule: released (debounce + release open) since its last press
  Millis pulse_until_[CHANNEL_COUNT] = {0};         // a pulse turns its output off then (0: none)
  uint32_t values_[SCRIPT_SETTING_COUNT] = {0};
  bool paused_ = false;
  int16_t pixel_step_ = -1;   // the last pixels line that ran (a step index), -1 none
  Millis pixel_since_ = 0;
  bool remote_enabled_ = false;
  uint8_t remote_free_ = 0;
  std::vector<SoundCommand> pending_sound_;         // sound lines since the last tick
};

}  // namespace pt
