// The show model and its validation.
//
// A show in JSON (the same JSON the phone UI edits):
//
//   {"id": 1, "name": "Front door",
//    "triggers": [{"on": "input", "input": "TRIG1"}],
//    "cooldown_ms": 5000,
//    "tracks": [
//      {"target": "OUT1",  "cues": [{"at": 0, "do": "on"}, {"at": 3000, "do": "off"}]},
//      {"target": "SOUND", "cues": [{"at": 0, "do": "play", "folder": 1, "track": 1}]}]}
//
// Levels are permille (0-1000). Times are ms from the start of the show.
#pragma once
#include "dfplayer.h"
#include "pixels.h"
#include "types.h"
#include <string>
#include <vector>

#define ARDUINOJSON_ENABLE_STD_STRING 1
#include <ArduinoJson.h>

namespace pt {

static const size_t MAX_SHOWS = 16;
static const size_t MAX_TRACKS = 16;
static const size_t MAX_CUES = 1000;
static const uint32_t MAX_LENGTH_MS = 3600000;
static const size_t MAX_NAME = 40;
static const size_t MAX_CUE_NAME = 24;
static const uint32_t MIN_LOOP_MS = 100;
// Most runs one cooldown can allow.
static const uint8_t MAX_RUNS_PER_COOLDOWN = 20;
// The most a show with an `until_release` trigger and no length_ms runs while its input is held (10 minutes).
static const uint32_t UNTIL_RELEASE_MAX_MS = 600000;

// What starts a show.
struct ShowTrigger {
  enum class Kind : uint8_t { OnInput, OnRelease, Every, Boot, Cue, After };
  Kind on = Kind::OnInput;
  Input input = Input::Trig1;  // OnInput, OnRelease
  uint32_t ms = 0;             // Every: period_ms; Boot: delay_ms
  std::string name;            // Cue
  uint16_t show = 0;           // After
  // OnInput: a run this press started ends when the input is let go ("on while held"); the show's end is the
  // most it can run held (UNTIL_RELEASE_MAX_MS when it has no length_ms). Left out of the JSON when false.
  bool until_release = false;
  bool operator==(const ShowTrigger& o) const;
};

enum class Retrigger : uint8_t { Ignore, Restart, Stop };

// What a cue does. Which actions fit which target is checked by validate().
struct Action {
  enum class Kind : uint8_t { On, Off, SetLevel, Ramp, Pulse, Blink, Play, LoopFolder, Volume, Fill, Chase, Rainbow, Flash, Fade, Flicker, Emit };
  Kind kind = Kind::On;
  uint16_t level = 0;  // SetLevel: level; Ramp: to
  uint32_t ms = 0;     // Ramp, Pulse, Fade
  uint32_t on_ms = 0, off_ms = 0;
  uint16_t count = 0;  // Blink (0 = until the next cue)
  // Play: folder 0 is sound `track` (1-99, sound.h: the card's NN/001-….mp3), folder 1-99 the file /FF/TTT.mp3
  uint8_t folder = 0, volume = 0;
  uint16_t track = 0;
  Rgb color;
  uint32_t step_ms = 0;
  uint16_t width = 3;
  uint32_t period_ms = 0;
  std::string cue;  // Emit
  bool operator==(const Action& o) const;
  // How long the cue itself lasts (0 for instantaneous or open-ended actions).
  uint32_t duration() const;
  // The pixel effect of a pixel action (false if it is not one).
  bool effect(Effect& out) const;
  // The player command of a sound action.
  bool sound(SoundCommand& out) const;
  const char* name() const;  // "on", "ramp", ...
};

struct Cue {
  uint32_t at = 0;
  Action action;
  bool operator==(const Cue& o) const { return at == o.at && action == o.action; }
};

struct Track {
  Target target;
  std::vector<Cue> cues;
};

struct Show {
  uint16_t id = 1;
  std::string name;
  bool enabled = true;
  std::vector<ShowTrigger> triggers;
  uint32_t cooldown_ms = 0;
  Retrigger retrigger = Retrigger::Ignore;
  bool looping = false;
  uint8_t priority = 1;
  bool has_length = false;
  uint32_t length_ms = 0;
  // How many runs the cooldown allows (default 1: rest after every run). With 3 the show can run three
  // times; the cooldown starts when the first run ends, and once three runs have started inside it the show
  // waits until it is over.
  uint8_t runs_per_cooldown = 1;
  bool stop_sound = true;
  std::vector<Track> tracks;

  void normalize();  // stable sort of each track's cues by time
  uint32_t naturalLength() const;
  // length_ms, or the end of the last cue; with an `until_release` trigger and no length_ms at least
  // UNTIL_RELEASE_MAX_MS (the run ends at the release)
  uint32_t length() const;
  bool untilRelease() const;  // a trigger has `until_release`
  const Track* track(Target t) const;
  Track& trackMut(Target t);
};

// A track prepared for evaluation: sorted cues plus the level each cue starts from.
struct Compiled {
  std::vector<Cue> cues;
  std::vector<Level> start;
  explicit Compiled(const Track& t);
  Compiled() {}
  // Level at t ms into the show; false before the first cue (the track has not touched its output yet).
  bool levelAt(uint64_t t, Level& out) const;
};

enum class Problem : uint8_t { None, BadId, BadName, TooBig, DuplicateTarget, TooLong, WrongTarget, OutOfRange, TooFast, BadCueName };
const char* problemName(Problem p);   // "bad_id"
const char* problemDebug(Problem p);  // "BadId"
// A friendly sentence for beginners ("A cue dims OUT2, but its load is too big to dim ...").
const char* problemHelp(Problem p);

struct [[nodiscard]] ShowError {
  Problem problem = Problem::None;
  int track = -1;  // -1 = none
  int cue = -1;
  bool ok() const { return problem == Problem::None; }
  std::string message() const;  // "PwmOverLimit in track 1 at cue 0"
};

// Check a show against the model limits.
[[nodiscard]] ShowError validate(const Show& s);
// A validation error in plain words, naming the output and saying what to do ("OUT2 is dimmed at 1.20 s,
// but its load (3000 mA) is above the 2000 mA that may be dimmed at this supply; ...").
std::string explain(const ShowError& e, const Show& show);

// JSON (wire format of PROTOCOL.md).
void toJson(const Show& s, JsonObject o);
void toJson(const Action& a, uint32_t at, JsonObject o);
void toJson(const ShowTrigger& t, JsonObject o);
// Parse; on failure returns false and an error message.
[[nodiscard]] bool fromJson(JsonVariantConst v, Show& s, std::string& err);
[[nodiscard]] bool actionFromJson(JsonVariantConst v, Action& a, std::string& err);
bool triggerFromJson(JsonVariantConst v, ShowTrigger& t, std::string& err);
std::string showToString(const Show& s);
[[nodiscard]] bool showFromString(const std::string& json, Show& s, std::string& err);

// Typed field readers used by every JSON parser in the library (strict like serde: wrong type = error).
bool readU64(JsonVariantConst o, const char* key, uint64_t max, uint64_t& out, bool required, std::string& err);
template <typename T>
bool readUint(JsonVariantConst o, const char* key, T& out, bool required, std::string& err, uint64_t max = (uint64_t)(T)~(T)0) {
  uint64_t v = out;
  if (!readU64(o, key, max, v, required, err)) return false;
  out = (T)v;
  return true;
}
bool readBool(JsonVariantConst o, const char* key, bool& out, bool required, std::string& err);
bool readStr(JsonVariantConst o, const char* key, std::string& out, bool required, std::string& err);

}  // namespace pt
