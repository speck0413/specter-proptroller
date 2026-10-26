// Show scripts: the checker and the runner. The rules and every message are firmware/show-script-golden/README.md;
// test_script_golden runs its cases.
#include "script.h"
#include "sound.h"
#include "pixels.h"
#include "pattern_vm.h"
#include <string.h>
#include <algorithm>

namespace pt {

// ------------------------------------------------------------------ names

const char* const SCRIPT_OUTPUT_WORDS[CHANNEL_COUNT] = {"out1", "out2", "out3", "out4", "rly1", "rly2", "cc1", "cc2"};
const char* const SCRIPT_INPUT_WORDS[INPUT_COUNT] = {"trig1", "trig2", "user1", "user2", "user3"};
static const char* const (&OUT_NAMES)[CHANNEL_COUNT] = SCRIPT_OUTPUT_WORDS;
static const char* const OUT_UPPER[CHANNEL_COUNT] = {"OUT1", "OUT2", "OUT3", "OUT4", "RLY1", "RLY2", "CC1", "CC2"};
static const char* const (&IN_NAMES)[INPUT_COUNT] = SCRIPT_INPUT_WORDS;
const char* const SCRIPT_KEYWORDS[SCRIPT_KEYWORD_COUNT] = {"effect", "wait",  "for",   "sleep",        "fade",       "to",
                                                          "over",   "flicker", "pulse", "limit",      "run",        "runs",
                                                          "per",    "audio_single", "audio_loop", "audio_volume", "audio_stop", "pixels",
                                                          "tell",
                                                          "pattern", "press"};
const char* const SCRIPT_TELL_REFUSED[SCRIPT_TELL_REFUSED_COUNT] = {
    "name", "page_lock", "pin", "wifi_ssid", "wifi_pass", "ap_pass", "mqtt_broker", "mqtt_port", "board_link",
    "link_group", "link_key", "remote_trigger_enable", "remote_free_effects", "effect"};
bool scriptTellRefused(const std::string& key) {
  for (auto k : SCRIPT_TELL_REFUSED)
    if (key == k) return true;
  return false;
}
const char* const SCRIPT_LEVEL_WORDS[2] = {"on", "off"};
// no unit: seconds
const ScriptUnit SCRIPT_TIME_UNITS[SCRIPT_UNIT_COUNT] = {{"ms", 1},         {"s", 1000},       {"sec", 1000},  {"secs", 1000},
                                                         {"second", 1000},  {"seconds", 1000}, {"min", 60000}, {"mins", 60000},
                                                         {"minute", 60000}, {"minutes", 60000}};
static const char* const IN_UPPER[INPUT_COUNT] = {"TRIG1", "TRIG2", "USER1", "USER2", "USER3"};
static const char* const ROLE_WORDS[4] = {"shows", "pulse on every trigger", "pulse when a sound starts", "not used"};
const char* const SCRIPT_SETTINGS[SCRIPT_SETTING_COUNT] = {"out1_on_ms", "out2_on_ms", "out3_on_ms", "recover_time_ms",
                                                          "cooldown_ms", "trigger_out_pulse_ms", "role_pulse_ms", "runs_per_cooldown"};

const char* const SCRIPT_PIXEL_EFFECTS[SCRIPT_PIXEL_EFFECT_COUNT] = {"off", "fill", "chase", "rainbow", "flash", "fade",
                                                                 "flicker", "comet", "sparkle", "breathe", "fire"};
static_assert((int)Effect::Kind::Fire + 1 == SCRIPT_PIXEL_EFFECT_COUNT && (int)Effect::Kind::Comet == 7,
              "SCRIPT_PIXEL_EFFECTS is in Effect::Kind's order");
const ScriptColour SCRIPT_COLOURS[SCRIPT_COLOUR_COUNT] = {
    {"red", 0xFF0000},  {"orange", 0xFF6000}, {"yellow", 0xFFC000}, {"green", 0x00FF00}, {"cyan", 0x00FFFF},
    {"blue", 0x0000FF}, {"purple", 0x8000FF}, {"pink", 0xFF40A0},   {"white", 0xFFFFFF}, {"warm white", 0xFFA040}};
bool scriptPixelColoured(uint8_t effect) {
  return effect != (uint8_t)Effect::Kind::Off && effect != (uint8_t)Effect::Kind::Rainbow && effect != (uint8_t)Effect::Kind::Fire;
}

static std::string targetName(uint16_t t) {
  return scriptIsDmx(t) ? "dmx" + std::to_string(t - SCRIPT_DMX + 1) : std::string(OUT_NAMES[t]);
}
static std::string targetUpper(uint16_t t) {
  return scriptIsDmx(t) ? "DMX" + std::to_string(t - SCRIPT_DMX + 1) : std::string(OUT_UPPER[t]);
}
static bool dimmable(uint16_t t) { return t < 4 || scriptIsDmx(t); }
// terminal k of the nameable ones (SS-N): outputs, then inputs
static const char* namedId(int k) { return k < CHANNEL_COUNT ? OUT_NAMES[k] : IN_NAMES[k - CHANNEL_COUNT]; }

// ------------------------------------------------------------------ the messages (README "Messages")

// One table of templates and one formatter. {1} {2} {3} are the words, {k} the number; a message about a line
// gets the "line N: " prefix. The numbers in the texts are the limits below (checked at compile time).
static_assert(SCRIPT_MAX_EFFECTS == 8 && SCRIPT_MAX_LINES == 256 && SCRIPT_MAX_BYTES == 8192 && SCRIPT_MAX_LINES_PER_EFFECT == 64 &&
                  SCRIPT_MAX_NAME == 32 && SCRIPT_MAX_TRACK == 99 && SCRIPT_MAX_TRACK == SOUND_MAX_TRACK && DMX_CHANNELS == 512,
              "the message texts quote these limits");
enum Msg : uint8_t {
  M_OUTSIDE, M_COLON, M_NONAME, M_LONGNAME, M_DUPNAME, M_UNKNOWN, M_WAITFORM, M_FADEFORM, M_FLICKERFORM, M_NOSOUND,
  M_INPUT, M_LEARN, M_OUTPUT, M_DMXCH, M_LEVEL, M_ONOFF, M_NOFADE, M_NOFLICKER, M_TIME, M_TOOLONG, M_SOUND,
  M_SOUNDRANGE, M_DMXOFF, M_ROLE, M_OWNED, M_SHARED, M_EMPTY, M_NOPAUSE, M_EFFECTLINES, M_EFFECTS, M_LINES, M_BYTES,
  W_STILLON, W_TWOSOUND, M_RELAYFAST, M_HINTSET, M_HINTPLAY, M_HINTTURN, M_HINTSLEEP, M_PIXEL,
  M_TWONAMES, M_BADNAME, W_NAMEIS, W_WAITNAME,
  M_LIMITFORM, M_LIMITRUNS, M_LIMITLONG, M_TWOLIMITS, M_NOTATIME, M_PULSEFORM, M_NOPULSE, M_PULSESHORT, M_LAMP, M_VOLUME,
  M_PIXEMPTY, M_PIXEFFECT, M_PIXCOLOR, M_PIXNOCOLOR, M_PIXHASH, M_PIXLINE,
  M_TELLFORM, M_TELLBOARD, M_TELLNAME, M_TELLKEY, M_TELLREFUSED, M_TELLVALUE, M_TELLLONG, M_TELLSPACE,
  M_PATEMPTY, M_PATNAME, M_PRESSFORM, M_PRESSNAME, M_PRESSEQ, M_NAMES, M_NOBUTTON, M_NOBUTTONS, M_NOBUTTONANY, M_NOPLAYER
};
static const char* const MSG[] = {
    "this line is not inside an effect. Start one first with \"effect <name>:\".",
    "put a colon after the effect name: \"effect {1}:\".",
    "give the effect a name, for example \"effect fog:\".",
    "the effect name is longer than 32 characters.",
    "there is already an effect called \"{1}\" (line {k}). Give each effect its own name.",
    "I do not understand \"{1}\". A line is effect, wait for, an output = on/off/N%, wait, fade, flicker, audio_single, audio_loop, audio_volume or audio_stop.",
    "write it as \"wait for trig1\".",
    "write a fade as \"fade out1 to 50% over 2\".",
    "write a flicker as \"flicker out1 for 3\".",
    "say which sound by its number, for example \"audio_single = 1\" (the card's 01/001-<name>.mp3).",
    "there is no {1} on this board. The inputs are trig1, trig2 and user1 to user3.",
    "{1} is a learn-mode button (buttons = learn). Set buttons to triggers to use it in a script.",
    "there is no {1} on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.",
    "there is no DMX channel {1}. DMX channels are 1 to 512.",
    "\"{1}\" is not a level. Use on, off or a percentage from 0% to 100%.",
    "{1} can only be on or off, not {2}. Dimming works on out1 to out4.",
    "{1} cannot fade. Fading works on out1 to out4.",
    "{1} cannot flicker. Flickering works on out1 to out4.",
    "\"{1}\" is not a time. Write seconds (wait 2.5) or add a unit: ms, s or min.",
    "that is longer than 24 hours.",
    "\"{1}\" is not a sound number. Sounds are numbered 1 to 99: Studio's SD card tool puts them on the card for you (sound 5 is 05/001-<name>.mp3).",
    "sound {k} is out of range. Sounds are numbered 1 to 99: Studio's SD card tool puts them on the card for you (sound 5 is 05/001-<name>.mp3).",
    "{1} needs the DMX add-on, and it is not turned on. Turn it on with the setting dmx_port = s4.",
    "{1} is set to {2} ({3}_role). Set {3}_role to shows to use it in a script.",
    "{1} belongs to the built-in pulse-and-recover effect. Turn the effect off (setting effect = none) or use another terminal.",
    "{1} is already used by effect \"{2}\" (line {k}). Each {3} belongs to one effect.",
    "effect \"{1}\" has no lines.",
    "effect \"{1}\" never waits, so it would repeat without a pause. Add a \"wait 5\" or a \"wait for trig1\".",
    "effect \"{1}\" has {k} lines. An effect can have at most 64.",
    "the script has {k} effects. A board runs at most 8.",
    "the script has {k} lines. A board takes at most 256.",
    "the script is {k} bytes. A board takes at most 8192.",
    "effect \"{1}\" ends with {2} still on. It may stay on until the next trigger.",
    "effects \"{1}\" and \"{2}\" both play sound. There is one sound player: a new sound replaces the one playing.",
    "{1} switches again after {k} ms. Relays and contacts switch at most every 100 ms: wait at least 0.1 between its changes.",
    "write it as \"{1} = {2}\".",
    "to play a sound, write \"audio_single = {1}\" (a sound is its number: the card's 01/001-<name>.mp3 is 1).",
    "write it as three lines: \"out1 = on\", \"wait 2\", \"out1 = off\".",
    "to wait a time, write \"wait {1}\".",
    "to light the pixel strip, write \"pixels = {1}\".",
    "\"{1}\" is the name of both {2} and {3}. Write {2} or {3}, or give them different names.",
    "\"{1}\" is the name of {2}, but a script cannot use it: {3}. Write {2}, or give it another name.",
    "{1} is also the name given to {2}. Here {1} means the terminal {1}.",
    "\"{1}\" is the name of {2} and of the setting {3}. \"wait for {1}\" waits for {2}; to wait the setting's time, write \"wait {3}\".",
    "write it as \"limit 3 runs per 3 min\".",
    "\"{1}\" is not a number of runs. Use 1 to 20, or the setting runs_per_cooldown.",
    "a limit's time is at most 60 minutes.",
    "effect \"{1}\" already has a limit (line {k}). An effect has one.",
    "{1} is a number of runs, not a time.",
    "write a pulse as \"pulse cc1 for 200 ms\".",
    "{1} cannot pulse. Pulses work on out1 to out4, rly1, rly2, cc1 and cc2.",
    "{1} is a relay or a contact: pulse it for at least 0.1 s.",
    "{1} is the ready lamp (setting out4_ready_lamp). Turn it off to use {1} in a script.",
    "\"{1}\" is not a volume. Use a percentage from 0% to 100%, or off.",
    "say what the pixel strip shows, for example \"pixels = rainbow\" or \"pixels = chase red\".",
    "\"{1}\" is not a pixel effect. The effects are off, fill, chase, rainbow, flash, fade, flicker, comet, sparkle, breathe and fire, then a colour: \"pixels = chase red\".",
    "\"{1}\" is not a colour. Use red, orange, yellow, green, cyan, blue, purple, pink, white or warm white, or 0xRRGGBB for an exact colour (0xff8000).",
    "{1} takes no colour: write \"pixels = {1}\".",
    "\"#{1}\" starts a comment, so the colour is lost: write it as 0x{1}.",
    "the pixel strip has its own line: \"pixels = <effect> <colour>\", for example \"pixels = chase red\".",
    "write it as \"tell porch blackout\" (a board's name, then one of its effects) or \"tell porch pixel_speed = 150\" (a setting).",
    "\"{1}\" is not a board's name. Use the name the board has on the network (letters, digits and -, as in porch.local), or all.",
    "\"{1}\" cannot be sent as an effect's name: it has at most 32 characters, and no / or +.",
    "\"{1}\" is not a setting's name. A setting is one word of letters, digits and _, for example pixel_speed.",
    "{1} cannot be changed by another board: its name, passwords, Wi-Fi, MQTT, board link, page lock, remote trigger and built-in effect stay with the board.",
    "say the value, for example \"tell porch pixel_speed = 150\".",
    "the value is longer than 64 characters.",
    "the tell lines hold more than 512 characters of board names, effects, settings and values in all.",
    "say which pattern, for example \"pixels = pattern ghost\" (a pattern Studio sent to the board).",
    "\"{1}\" is not a pattern's name. A name has at most 32 letters, digits, spaces and - _ ( ).",
    "say which button, for example \"pixels press clue1\" (a button of the pattern the strip shows).",
    "\"{1}\" is not a button's name. A name has at most 32 letters, digits, spaces and - _ ( ).",
    "a button press has no \"=\": write \"pixels press {1}\".",
    "a script names at most 16 patterns and buttons.",
    "pattern \"{1}\" has no button \"{2}\". Its buttons are {3}.",
    "pattern \"{1}\" has no buttons to press (a button is an export function trigger... in the pattern).",
    "no pattern this script shows has a button \"{1}\".",
    "this board has no pattern player (an ESP32, an ESP32-S3 or a Pico 2 W has one; the Pico W, the Pico and the ESP32-C3 do not).",
};
static_assert(sizeof MSG / sizeof MSG[0] == M_NOPLAYER + 1, "one text per message");
static_assert(SCRIPT_TELL_BYTES == 512 && SCRIPT_TELL_VALUE_MAX == 64, "the message texts quote these limits");
static_assert(SCRIPT_LIMIT_MAX_RUNS == 20 && SCRIPT_LIMIT_MAX_MS == 60UL * 60000UL && RELAY_MIN_DWELL_MS == 100,
              "the message texts quote these limits");

static void appendNum(std::string& o, size_t v) {  // size_t: a count is size_t on every host (long is 32-bit on Windows)
  char b[24];
  int i = 23;
  b[i] = 0;
  do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
  o += b + i;
}

static std::string msg(Msg id, uint16_t n, const char* a = "", const char* b = "", const char* c = "", size_t k = 0) {
  std::string o;
  if (n) { o = "line "; appendNum(o, n); o += ": "; }
  for (const char* t = MSG[id]; *t; t++) {
    if (*t == '{' && t[1] && t[2] == '}') {
      char w = t[1];
      if (w == '1') o += a;
      else if (w == '2') o += b;
      else if (w == '3') o += c;
      else appendNum(o, k);
      t += 2;
    } else {
      o += *t;
    }
  }
  return o;
}

// ------------------------------------------------------------------ reading a line

std::string scriptClean(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    unsigned char c = (unsigned char)in[i];
    if (c == '\r') {
      out += '\n';
      if (i + 1 < in.size() && in[i + 1] == '\n') i++;
    } else if (c == '\t') {
      out += ' ';
    } else if (c == 0xC2 && i + 1 < in.size() && (unsigned char)in[i + 1] == 0xA0) {
      out += ' ';  // U+00A0
      i++;
    } else if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x80) {
      unsigned char d = (unsigned char)in[i + 2];
      if (d == 0x98 || d == 0x99) { out += '\''; i += 2; }      // U+2018, U+2019
      else if (d == 0x9C || d == 0x9D) { out += '"'; i += 2; }  // U+201C, U+201D
      else if (d == 0xAF) { out += ' '; i += 2; }               // U+202F
      else out += (char)c;
    } else {
      out += (char)c;
    }
  }
  return out;
}

static std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && s[a] == ' ') a++;
  while (b > a && s[b - 1] == ' ') b--;
  return s.substr(a, b - a);
}

static std::string lower(const std::string& s) {
  std::string o = s;
  for (auto& c : o)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return o;
}

// runs of spaces made one
static std::string collapse(const std::string& s) {
  std::string o;
  for (char c : s)
    if (c != ' ' || (!o.empty() && o.back() != ' ')) o += c;
  return trim(o);
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }
static bool startsWith(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }

// A decimal number that is the whole of `s` (digits, an optional '.' and digits): value = num / den.
static bool decimal(const std::string& s, uint64_t& num, uint64_t& den) {
  num = 0;
  den = 1;
  size_t i = 0;
  int digits = 0, frac = 0;
  while (i < s.size() && isDigit(s[i])) {
    if (++digits > 12) return false;
    num = num * 10 + (uint64_t)(s[i++] - '0');
  }
  if (i < s.size() && s[i] == '.') {
    i++;
    while (i < s.size() && isDigit(s[i])) {
      if (++frac > 6) return false;
      num = num * 10 + (uint64_t)(s[i++] - '0');
      den *= 10;
    }
  }
  return i == s.size() && digits + frac > 0;
}

// Leading digits of `s` (for a number at the start).
static size_t leadingDigits(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && isDigit(s[i])) i++;
  return i;
}

// a run of digits as a number, saturating (enough to tell "too big" from a real value)
static uint64_t digitsValue(const std::string& k) {
  uint64_t v = 0;
  for (char c : k) v = v > 100000000ULL ? v : v * 10 + (uint64_t)(c - '0');
  return v;
}

// rounded to the nearest, halves up
static uint64_t roundDiv(uint64_t a, uint64_t b) { return (a * 2 + b) / (b * 2); }

// Time: 1 ok, 0 not a time, -1 longer than 24 h.
static int parseTime(const std::string& s0, uint32_t& ms) {
  std::string s = trim(s0);
  size_t i = 0;
  while (i < s.size() && (isDigit(s[i]) || s[i] == '.')) i++;
  uint64_t num = 0, den = 0;
  if (!decimal(s.substr(0, i), num, den)) return 0;
  std::string unit = trim(s.substr(i));
  uint64_t scale = unit.empty() ? 1000 : 0;
  for (auto& u : SCRIPT_TIME_UNITS)
    if (unit == u.name) scale = u.ms;
  if (!scale) return 0;
  uint64_t v = roundDiv(num * scale, den);
  if (v > SCRIPT_MAX_TIME_MS) return -1;
  ms = (uint32_t)v;
  return 1;
}

// audio_volume's level (on, off or N%, already checked) on the player's 0-30: round(N x 30 / 100), halves up,
// from the decimal text (as DMX levels are).
static uint16_t volumeOf(const std::string& s0);

// Level: on, off or N% (0-100); for an output permille, for DMX 0-255.
static bool parseLevel(const std::string& s0, bool dmx, uint16_t& out) {
  std::string s = trim(s0);
  if (s == SCRIPT_LEVEL_WORDS[0]) { out = dmx ? DMX_FULL : FULL; return true; }
  if (s == SCRIPT_LEVEL_WORDS[1]) { out = 0; return true; }
  if (s.empty() || s.back() != '%') return false;
  uint64_t num = 0, den = 0;
  if (!decimal(trim(s.substr(0, s.size() - 1)), num, den)) return false;
  if (num > 100 * den) return false;
  out = (uint16_t)(dmx ? roundDiv(num * DMX_FULL, den * 100) : roundDiv(num * 10, den));
  return true;
}

static uint16_t volumeOf(const std::string& s0) {
  std::string s = trim(s0);
  if (s == "on") return 30;
  if (s == "off") return 0;
  uint64_t num = 0, den = 0;
  if (!decimal(trim(s.substr(0, s.size() - 1)), num, den)) return 0;
  return (uint16_t)roundDiv(num * 30, den * 100);
}

// A setting a line names for a time or a count (SCRIPT_SETTINGS, or a profile key alias of one), lower case:
// k + 1, or 0 when it is none.
static uint8_t settingNamed(const std::string& w, const ScriptBoard& board) {
  for (uint8_t k = 0; k < SCRIPT_SETTING_COUNT; k++)
    if (w == SCRIPT_SETTINGS[k]) return (uint8_t)(k + 1);
  for (auto& a : board.setting_aliases) {
    std::string al = a.first;
    for (auto& c : al) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (al != w) continue;
    for (uint8_t k = 0; k < SCRIPT_SETTING_COUNT; k++)
      if (a.second == SCRIPT_SETTINGS[k]) return (uint8_t)(k + 1);
  }
  return 0;
}

// A time or a setting that holds one: 1 ok (ms, or setting k + 1), 0 not a time, -1 longer than 24 h, -2 the
// setting is a count.
static int parseTimeOrSetting(const std::string& v, const ScriptBoard& board, uint32_t& ms, uint8_t& setting) {
  setting = 0;
  int pt = parseTime(v, ms);
  if (pt != 0) return pt;
  uint8_t k = settingNamed(trim(v), board);
  if (!k) return 0;
  if (k - 1 == SCRIPT_SETTING_RUNS) return -2;
  setting = k;
  ms = 0;
  return 1;
}

// ------------------------------------------------------------------ the sound list

std::string soundListJson(const SoundList& s) {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  for (auto& e : s) o[e.first] = e.second;
  std::string out;
  serializeJson(d, out);
  return out;
}

bool soundListParse(const std::string& json, SoundList& out, std::string& why) {
  out.clear();
  if (json.empty()) return true;
  JsonDocument d;
  if (deserializeJson(d, json) || !d.is<JsonObject>()) { why = "the sound list must be a JSON object such as {\"chime.mp3\": 1}"; return false; }
  for (JsonPair p : d.as<JsonObject>()) {
    if (!p.value().is<uint32_t>() || p.value().as<uint32_t>() < 1 || p.value().as<uint32_t>() > SCRIPT_MAX_TRACK) {
      why = std::string("sound ") + p.key().c_str() + ": the number must be 1 to " + std::to_string(SCRIPT_MAX_TRACK);
      return false;
    }
    if (out.size() >= SCRIPT_MAX_SOUNDS) { why = "the sound list has more than " + std::to_string(SCRIPT_MAX_SOUNDS) + " sounds"; return false; }
    out.push_back({p.key().c_str(), (uint16_t)p.value().as<uint32_t>()});
  }
  return true;
}

// ------------------------------------------------------------------ names given to terminals (SS-N)

static bool isTerminalWord(const std::string& w) {
  for (auto o : OUT_NAMES) if (w == o) return true;
  for (auto i : IN_NAMES) if (w == i) return true;
  return startsWith(w, "dmx") && w.size() > 3 && leadingDigits(w.substr(3)) == w.size() - 3;
}

// A word the line reader takes as the start of another line before it can be an output (`sleep = on` is a sleep,
// the old spelling of wait).
static bool takenFirst(const std::string& w) {
  return startsWith(w, "wait") || (startsWith(w, "sleep") && (w.size() == 5 || isDigit(w[5]))) || w == "fade" ||
         w == "flicker" || w == "pulse" || w == "limit" || w == "takes" || w == "tell";
}

// Why a given name (lower case, runs of spaces made one) cannot stand for its terminal in a script; null when
// it can.
static const char* nameProblem(const std::string& w) {
  static const char* const WORDS[] = {"effect", "audio_single", "audio_loop", "audio_stop", "on", "off", "to", "over", "for",
                                      "turn", "play", "pixel", "pixels"};
  if (w.find(' ') != std::string::npos) return "a name in a script is one word";
  bool ok = w[0] >= 'a' && w[0] <= 'z';
  for (char c : w) ok &= (c >= 'a' && c <= 'z') || isDigit(c) || c == '_';
  if (!ok) return "a name in a script starts with a letter and has only letters, digits and _";
  if (isTerminalWord(w)) return "it is the name of a terminal";
  bool word = takenFirst(w) || startsWith(w, "effect");
  for (auto k : WORDS) word |= w == k;
  return word ? "it is a word of the script language" : nullptr;
}

// ------------------------------------------------------------------ the checker

// In line order, keeping the order within a line (an insertion sort: a handful of messages).
static void byLine(std::vector<ScriptMessage>& v) {
  for (size_t i = 1; i < v.size(); i++)
    for (size_t j = i; j > 0 && v[j - 1].line > v[j].line; j--) std::swap(v[j - 1], v[j]);
}

namespace {
struct FxWork {
  std::string name;
  uint16_t line = 0;
  size_t lines = 0;  // non-blank lines under the header
  bool has_wait = false;    // a wait for line (with or without an error)
  bool takes_time = false;  // a wait <time>, fade or flicker line whose time is not 0 (or has an error)
  bool plays_sound = false;
  std::vector<ScriptStep> steps;
  std::vector<std::pair<uint16_t, uint16_t>> last;  // (target, level after its last step), first appearance order
  uint16_t limit_line = 0;  // the limit line (0: none)
  uint8_t limit_runs = 0, limit_runs_setting = 0, limit_ms_setting = 0;
  uint32_t limit_ms = 0;
};
struct Named {
  std::string word;     // lower case, runs of spaces made one
  int terminal;         // 0-7 outputs, 8-12 inputs
  const char* problem;  // why a script cannot use it, or null
};
}  // namespace

// The advanced lines' reading, kept out of scriptCompile so its stack frame stays small (the boards build with a
// 1 KB frame limit).
namespace {
struct LimitLine {
  int msg = -1;      // an error (Msg), or -1
  std::string word;  // the word the error quotes
  uint8_t runs = 0, runs_setting = 0, ms_setting = 0;
  uint32_t ms = 0;
};
struct PulseLine {
  bool form = false;        // "pulse <target> for <time>"
  std::string target, tm, tm_raw;
  int pt = 0;               // parseTimeOrSetting's answer
  uint32_t ms = 0;
  uint8_t setting = 0;
};
}  // namespace

// limit <N> run(s) per <time> (SS-G3)
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void readLimit(const std::string& l, const std::string& raw, const ScriptBoard& board, LimitLine& o) {
  std::string rest = " " + collapse(l.substr(5)) + " ";
  std::string rcol = " " + collapse(raw.substr(5)) + " ";  // the same, as written
  size_t per = rest.find(" per ");
  std::string left = per == std::string::npos ? std::string() : trim(rest.substr(0, per));
  size_t sp = left.rfind(' ');
  std::string nw = sp == std::string::npos ? std::string() : trim(left.substr(0, sp));
  std::string unit = sp == std::string::npos ? left : left.substr(sp + 1);
  std::string tm = per == std::string::npos ? std::string() : trim(rest.substr(per + 5));
  if (per == std::string::npos || nw.empty() || (unit != "run" && unit != "runs") || tm.empty()) { o.msg = M_LIMITFORM; return; }
  bool runs_ok = false;
  if (leadingDigits(nw) == nw.size()) {
    uint64_t v = digitsValue(nw);
    runs_ok = v >= 1 && v <= SCRIPT_LIMIT_MAX_RUNS;
    o.runs = (uint8_t)(runs_ok ? v : 0);
  } else {
    uint8_t k = settingNamed(nw, board);
    if (k && k - 1 == SCRIPT_SETTING_RUNS) { o.runs_setting = k; runs_ok = true; }
  }
  int pt = parseTimeOrSetting(tm, board, o.ms, o.ms_setting);
  if (!runs_ok) { o.msg = M_LIMITRUNS; o.word = trim(trim(rcol.substr(0, per)).substr(0, sp)); }
  else if (pt == 0) { o.msg = M_TIME; o.word = trim(rcol.substr(per + 5)); }
  else if (pt == -2) { o.msg = M_NOTATIME; o.word = tm; }
  else if (pt < 0 || (!o.ms_setting && o.ms > SCRIPT_LIMIT_MAX_MS)) o.msg = M_LIMITLONG;
}

// pulse <target> for <time> (SS-G5): the words; the target's checks are the caller's
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void readPulse(const std::string& l, const std::string& raw, const ScriptBoard& board, PulseLine& o) {
  std::string rest = " " + collapse(l.substr(5)) + " ";
  std::string rcol = " " + collapse(raw.substr(5)) + " ";
  size_t fo = rest.find(" for ");
  if (fo == std::string::npos) return;
  o.target = trim(rest.substr(0, fo));
  o.tm = trim(rest.substr(fo + 5));
  o.tm_raw = trim(rcol.substr(fo + 5));
  o.form = !o.target.empty() && !o.tm.empty();
  if (!o.tm.empty()) o.pt = parseTimeOrSetting(o.tm, board, o.ms, o.setting);
}

// A colour word (lower case): a name from SCRIPT_COLOURS or 0xRRGGBB.
static bool pixelColour(const std::string& w, uint32_t& rgb) {
  for (auto& c : SCRIPT_COLOURS)
    if (w == c.name) { rgb = c.rgb; return true; }
  if (w.size() != 8 || w[0] != '0' || w[1] != 'x') return false;
  uint32_t v = 0;
  for (size_t i = 2; i < 8; i++) {
    char c = w[i];
    int d = isDigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
    if (d < 0) return false;
    v = v * 16 + (uint32_t)d;
  }
  rgb = v;
  return true;
}

namespace {
struct PixelLine {
  int msg = -1;      // an error (Msg), or -1
  std::string word;  // the word the error quotes
  uint8_t effect = 0;
  uint32_t rgb = 0;
  std::string name;  // pixels = pattern <name>: the name, lower case, runs of spaces made one
};

// A pattern's or a button's name in a script line (lower case, runs of spaces made one): 1-32 letters, digits,
// spaces and - _ ( ) (pattern_store.h patternNameOk).
bool scriptNameOk(const std::string& w) {
  if (w.empty() || w.size() > SCRIPT_MAX_NAME) return false;
  for (char c : w)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')')) return false;
  return true;
}
}  // namespace

// pixels = <effect> [<colour>] | <colour> (SHOW-SCRIPT.md): `v` lower case, `v_raw` as written, `comment` the text
// after a # on the line (a #rrggbb colour there was cut off as a comment)
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void readPixels(const std::string& v_raw, const std::string& comment, PixelLine& o) {
  std::string v = collapse(lower(v_raw));
  std::string hex = lower(trim(comment)).substr(0, 6);
  bool hex_comment = hex.size() == 6;
  for (char c : hex) hex_comment &= isDigit(c) || (c >= 'a' && c <= 'f');
  if (v.empty()) {
    if (hex_comment) { o.msg = M_PIXHASH; o.word = hex; }
    else o.msg = M_PIXEMPTY;
    return;
  }
  if (pixelColour(v, o.rgb)) { o.effect = (uint8_t)Effect::Kind::Fill; return; }  // a colour alone fills the strip
  size_t sp = v.find(' ');
  std::string w = v.substr(0, sp), rest = sp == std::string::npos ? std::string() : v.substr(sp + 1);
  if (w == "pattern") {
    // pixels = pattern <name>: a stored LED pattern
    o.effect = (uint8_t)Effect::Kind::Pattern;
    o.name = rest;
    if (rest.empty()) o.msg = M_PATEMPTY;
    else if (!scriptNameOk(rest)) { o.msg = M_PATNAME; o.word = trim(collapse(v_raw).substr(w.size())); }
    return;
  }
  if (w == "press") { o.msg = M_PRESSEQ; o.word = rest.empty() ? "clue1" : rest; return; }
  int k = -1;
  for (int i = 0; i < SCRIPT_PIXEL_EFFECT_COUNT; i++)
    if (w == SCRIPT_PIXEL_EFFECTS[i]) k = i;
  if (k < 0) { o.msg = M_PIXEFFECT; o.word = collapse(v_raw).substr(0, w.size()); return; }
  o.effect = (uint8_t)k;
  if (!scriptPixelColoured(o.effect)) {
    if (!rest.empty()) { o.msg = M_PIXNOCOLOR; o.word = w; }
    return;
  }
  if (rest.empty()) {
    if (hex_comment) { o.msg = M_PIXHASH; o.word = hex; }
    else o.rgb = SCRIPT_PIXEL_DEFAULT_RGB;
    return;
  }
  if (!pixelColour(rest, o.rgb)) { o.msg = M_PIXCOLOR; o.word = trim(collapse(v_raw).substr(w.size())); }
}

// tell <board> <effect> / tell <board> <setting> = <value>: msg -1 when it is right
struct TellLine {
  int msg = -1;
  std::string word;  // for the message
  bool set = false;
  std::string board, what, value;
};
static bool boardName(const std::string& w) {
  if (w.empty() || w.size() > SCRIPT_TELL_BOARD_MAX || w == "broadcast" || w.front() == '-' || w.back() == '-') return false;
  for (char c : w)
    if (!((c >= 'a' && c <= 'z') || isDigit(c) || c == '-')) return false;
  return true;
}
static void readTell(const std::string& l, const std::string& raw, TellLine& o) {
  // l is raw in lower case (the same length): the value keeps its case
  size_t a = l.find_first_not_of(' ', 4);
  if (a == std::string::npos) { o.msg = M_TELLFORM; return; }
  size_t b = l.find(' ', a);
  std::string board = l.substr(a, b == std::string::npos ? std::string::npos : b - a);
  size_t eqb = board.find_first_of("=:");
  if (eqb != std::string::npos) board = board.substr(0, eqb), b = a + eqb;  // tell porch=... : no setting
  std::string rest = b == std::string::npos ? std::string() : trim(l.substr(b));
  if (board.empty() || rest.empty() || (eqb != std::string::npos)) { o.msg = M_TELLFORM; return; }
  if (!boardName(board)) { o.msg = M_TELLBOARD; o.word = trim(raw.substr(a, board.size())); return; }
  o.board = board;
  size_t eq = rest.find_first_of("=:");
  if (eq == std::string::npos) {
    // an effect, by its name as a remote start takes it: lower case, spaces as _
    std::string name = collapse(rest);
    for (auto& c : name) if (c == ' ') c = '_';
    if (name.size() > SCRIPT_MAX_NAME || name.find_first_of("/+") != std::string::npos) { o.msg = M_TELLNAME; o.word = collapse(rest); return; }
    o.what = name;
    return;
  }
  o.set = true;
  std::string key = trim(rest.substr(0, eq));
  size_t vat = b + (l.substr(b).find_first_of("=:")) + 1;  // where the value starts in raw
  std::string value = trim(raw.substr(vat));
  bool ok = !key.empty() && key.size() <= SCRIPT_MAX_NAME && key[0] >= 'a' && key[0] <= 'z';
  for (char c : key) ok &= (c >= 'a' && c <= 'z') || isDigit(c) || c == '_';
  if (key.empty()) { o.msg = M_TELLFORM; return; }
  if (!ok) { o.msg = M_TELLKEY; o.word = key; return; }
  if (scriptTellRefused(key)) { o.msg = M_TELLREFUSED; o.word = key; return; }
  if (value.size() >= 2 && (value[0] == '"' || value[0] == '\'') && value.back() == value[0]) value = value.substr(1, value.size() - 2);
  if (value.empty()) { o.msg = M_TELLVALUE; return; }
  if (value.size() > SCRIPT_TELL_VALUE_MAX) { o.msg = M_TELLLONG; return; }
  o.what = key;
  o.value = value;
}

bool scriptTellOf(const ScriptProgram& p, const ScriptStep& st, ScriptTell& out) {
  if (st.op != ScriptStep::Op::Tell || (size_t)st.ms + st.value > p.tell_bytes || st.value == 0) return false;
  const char* t = p.tell + st.ms;
  std::string all(t, st.value);
  size_t z1 = all.find('\0');
  size_t z2 = z1 == std::string::npos ? z1 : all.find('\0', z1 + 1);
  if (z2 == std::string::npos) return false;
  out.set = st.target == 1;
  out.board = all.substr(0, z1);
  out.what = all.substr(z1 + 1, z2 - z1 - 1);
  out.value = all.substr(z2 + 1);
  return !out.board.empty() && !out.what.empty() && (out.set == !out.value.empty());
}

ScriptCheck scriptCompile(const std::string& text, const SoundList& sounds, const ScriptBoard& board, ScriptProgram& out) {
  ScriptCheck r;
  (void)sounds;  // a stored sound list is kept with the script but not read: a sound is its number
  out.reset();  // in place (a fresh ScriptProgram() would be a 3.4 KB temporary on the stack)
  // A text over the byte limit gets the size message alone, before any of it is copied or read: checking the lines
  // of a much longer text (anyone on the network can send one) ran the ESP32 out of memory in the line copies, and
  // with no exceptions a failed allocation restarts the board (hardware 2026-10-23: 20-22 KB texts over HTTP and
  // serial). Within the limit every message is given as before.
  if (text.size() > SCRIPT_MAX_BYTES) {
    r.errors.push_back({0, msg(M_BYTES, 0, "", "", "", text.size())});
    return r;
  }
  std::string clean = scriptClean(text);
  // each line as (start, length) in `clean`: a line is copied out only while it is checked (a copy of every
  // line at once would be the script's size again, plus a string header per line)
  std::vector<std::pair<uint32_t, uint32_t>> lines;
  {
    size_t a = 0;
    for (size_t i = 0; i < clean.size(); i++)
      if (clean[i] == '\n') { lines.push_back({(uint32_t)a, (uint32_t)(i - a)}); a = i + 1; }
    if (a < clean.size()) lines.push_back({(uint32_t)a, (uint32_t)(clean.size() - a)});
  }

  // SS-N: the names given to the terminals (the wiring label and the profile's alias)
  std::vector<Named> named;
  for (auto& g : board.names) {
    std::string w = collapse(lower(g.name));
    int k = g.terminal;
    bool dup = w.empty() || k >= SCRIPT_NAMED;
    for (auto& x : named) dup |= x.terminal == k && x.word == w;
    if (!dup) named.push_back(Named{w, k, nameProblem(w)});
  }
  std::stable_sort(named.begin(), named.end(), [](const Named& a, const Named& b) { return a.terminal < b.terminal; });
  bool shadow_told[SCRIPT_NAMED] = {false};
  std::string called[CHANNEL_COUNT];  // the name the script used for an output, for the per-effect messages
  auto full = [&](uint16_t t) -> std::string {
    return scriptIsDmx(t) || called[t].empty() ? targetName(t) : called[t] + " (" + targetName(t) + ")";
  };

  std::vector<FxWork> fx;
  // the patterns and buttons the script names (ScriptProgram::name), and the pattern and press lines for the
  // check against the board's stored patterns at the end
  std::vector<std::string> names;
  std::vector<std::pair<uint16_t, uint8_t>> press_lines, pattern_lines;  // line, name

  for (size_t li = 0; li < lines.size(); li++) {
    uint16_t n = (uint16_t)(li + 1);
    std::string raw = clean.substr(lines[li].first, lines[li].second);
    size_t hash = raw.find('#');
    std::string comment;  // after the #: a pixels line looks there for a #rrggbb colour
    if (hash != std::string::npos) { comment = raw.substr(hash + 1); raw = raw.substr(0, hash); }
    raw = trim(raw);
    if (!raw.empty() && raw.back() == '.') raw = trim(raw.substr(0, raw.size() - 1));
    if (raw.empty()) continue;
    std::string l = lower(raw);
    auto err = [&](Msg id, const char* x = "", const char* y = "", const char* z = "", size_t k = 0) {
      r.errors.push_back({n, msg(id, n, x, y, z, k)});
    };
    // a pattern's or button's name in the script's table; false (with the message) when the table is full
    auto nameIndex = [&](const std::string& w, uint16_t& at) -> bool {
      for (size_t i = 0; i < names.size(); i++)
        if (names[i] == w) { at = (uint16_t)i; return true; }
      if (names.size() >= SCRIPT_MAX_NAMES) { err(M_NAMES); return false; }
      at = (uint16_t)names.size();
      names.push_back(w);
      return true;
    };

    // effect <name>:
    if (startsWith(l, "effect")) {
      bool colon = l.back() == ':';
      std::string body = colon ? l.substr(0, l.size() - 1) : l;
      std::string name = (body.size() == 6 || body[6] == ' ' || body[6] == ':') ? collapse(body.substr(6)) : collapse(body);
      FxWork f;
      f.name = name;
      f.line = n;
      if (!colon) err(M_COLON, name.c_str());
      else if (name.empty()) err(M_NONAME);
      else if (name.size() > SCRIPT_MAX_NAME) err(M_LONGNAME);
      else
        for (auto& o : fx)
          if (o.name == name) { err(M_DUPNAME, name.c_str(), "", "", o.line); break; }
      fx.push_back(f);
      continue;
    }
    if (fx.empty()) { err(M_OUTSIDE); continue; }
    int cur = (int)fx.size() - 1;
    FxWork& f = fx[cur];
    f.lines++;

    ScriptStep st;
    st.line = n;
    bool is_wait = false, good = false, declared = false;  // declared: a limit or turns line (no step)
    bool timed = false;     // a wait <time>, fade or flicker line (with or without an error)
    std::string via;  // the name this line used for its terminal ("": the terminal's own name)
    // a word that is not a terminal's own name: 1 and the terminal (in [lo, hi)) it is the name of; 0 when it
    // is nobody's name; -1 with the message when two terminals have it or a script cannot use it
    auto byName = [&](const std::string& w0, int lo, int hi, int& k) -> int {
      std::string w = collapse(w0);
      const Named* a = nullptr;
      const Named* b = nullptr;
      for (auto& x : named)
        if (x.terminal >= lo && x.terminal < hi && x.word == w) {
          if (!a) a = &x;
          else if (!b && x.terminal != a->terminal) b = &x;
        }
      if (!a) return 0;
      if (b) { err(M_TWONAMES, w.c_str(), namedId(a->terminal), namedId(b->terminal)); return -1; }
      if (a->problem) { err(M_BADNAME, w.c_str(), namedId(a->terminal), a->problem); return -1; }
      k = a->terminal;
      via = w;
      return 1;
    };
    // a terminal's own name that another terminal was given: it means the terminal (said once)
    auto shadowed = [&](int k) {
      for (auto& x : named)
        if (x.terminal != k && x.word == namedId(k) && !shadow_told[k]) {
          shadow_told[k] = true;
          r.warnings.push_back({n, msg(W_NAMEIS, n, namedId(k), namedId(x.terminal))});
        }
    };
    // the terminal as the messages show it: as written, or "lamp (out1)" when the line used a name
    auto shown = [&](uint16_t t, const std::string& written) -> std::string {
      return via.empty() ? written : via + " (" + targetName(t) + ")";
    };
    // what every line that changes a target checks after the target and the level, in the README's order
    auto targetChecks = [&](uint16_t t) -> bool {
      std::string x = shown(t, targetName(t));
      if (scriptIsDmx(t) && !board.dmx_on) { err(M_DMXOFF, x.c_str()); return false; }
      if (!scriptIsDmx(t) && t >= 4 && board.roles[t - 4] != 0) {
        err(M_ROLE, x.c_str(), ROLE_WORDS[board.roles[t - 4] & 3], targetName(t).c_str());
        return false;
      }
      if (!scriptIsDmx(t) && (board.effect_channels & (1u << t))) { err(M_OWNED, x.c_str()); return false; }
      if (!scriptIsDmx(t) && t == 3 && board.ready_lamp) { err(M_LAMP, x.c_str()); return false; }
      // two effects may set the same output: an effect waits for the outputs it uses to be free (SS-R8)
      return true;
    };
    // a target word: an output or dmxN; false (with the message) when there is none
    auto target = [&](const std::string& w, uint16_t& t) -> bool {
      for (int i = 0; i < CHANNEL_COUNT; i++)
        if (w == OUT_NAMES[i]) { t = (uint16_t)i; shadowed(i); return true; }
      if (startsWith(w, "dmx") && w.size() > 3 && leadingDigits(w.substr(3)) == w.size() - 3) {
        std::string k = w.substr(3);
        uint64_t num = digitsValue(k);
        if (num < 1 || num > DMX_CHANNELS) { err(M_DMXCH, k.c_str()); return false; }
        t = (uint16_t)(SCRIPT_DMX + num - 1);
        return true;
      }
      int k = 0;
      int found = byName(w, 0, CHANNEL_COUNT, k);
      if (found > 0) { t = (uint16_t)k; return true; }
      if (found == 0) {
        if (w == "pixel" || w == "pixels") err(M_PIXLINE);  // the strip is not an output: it has its own line
        else err(M_OUTPUT, w.c_str());
      }
      return false;
    };
    // a name the line reader would take for another line (an output called "sleep"): said plainly
    bool name_taken = false;
    {
      size_t q = l.find_first_of("=:");
      int k = 0;
      if (q != std::string::npos && l[q] == '=' && takenFirst(collapse(l.substr(0, q))))
        name_taken = byName(l.substr(0, q), 0, SCRIPT_NAMED, k) != 0;
    }

    if (name_taken) {
      // the message is given; the line adds no step
    } else if (startsWith(l, "tell") && (l.size() == 4 || l[4] == ' ')) {
      // tell <board> <effect> / tell <board> <setting> = <value> (SHOW-SCRIPT.md §11): the other board's effects
      // and settings are not known here, only the form
      TellLine tl;
      readTell(l, raw, tl);
      if (tl.msg >= 0) err((Msg)tl.msg, tl.word.c_str());
      else if (out.tell_bytes + tl.board.size() + tl.what.size() + tl.value.size() + 2 > SCRIPT_TELL_BYTES) err(M_TELLSPACE);
      else {
        st.op = ScriptStep::Op::Tell;
        st.target = tl.set ? 1 : 0;
        st.ms = out.tell_bytes;
        std::string pool = tl.board + '\0' + tl.what + '\0' + tl.value;
        memcpy(out.tell + out.tell_bytes, pool.data(), pool.size());
        st.value = (uint16_t)pool.size();
        out.tell_bytes = (uint16_t)(out.tell_bytes + pool.size());
        if (tl.board == "all") out.tell_all = true;
        good = true;
      }
    } else if (startsWith(l, "wait")) {
      // wait <time> and wait for <time> pause (a time starts with a digit or is a time setting); wait for <input>
      // waits for an input or a name given to one, and an input's name wins over a setting's (SS-L)
      std::string rest = trim(l.substr(4));
      bool has_for = startsWith(rest, "for");
      std::string in = has_for ? trim(rest.substr(3)) : std::string();
      std::string tm = has_for ? in : rest;  // what a time would be
      int k = -1, found = 0;
      if (has_for) {
        for (int i = 0; i < INPUT_COUNT; i++)
          if (in == IN_NAMES[i]) k = i;
        if (k >= 0) shadowed(CHANNEL_COUNT + k);
        else if (!in.empty()) {
          int nk = 0;
          found = byName(in, CHANNEL_COUNT, SCRIPT_NAMED, nk);
          if (found > 0) k = nk - CHANNEL_COUNT;
        }
      }
      bool is_time = !tm.empty() && (isDigit(tm[0]) || tm[0] == '.' || settingNamed(tm, board));
      // a bare wait and an input (wait trig1): the for is missing; any other word after a bare wait is a time
      bool bare_input = false;
      if (!has_for && !rest.empty()) {
        for (int i = 0; i < INPUT_COUNT; i++) bare_input |= rest == IN_NAMES[i];
        for (auto& x : named) bare_input |= x.terminal >= CHANNEL_COUNT && x.word == collapse(rest);
      }
      if (k >= 0 || found < 0 || (!is_time && (has_for || rest.empty() || bare_input))) {
        is_wait = true;
        std::string x = k < 0 || via.empty() ? in : via + " (" + IN_NAMES[k] + ")";
        uint8_t set = k >= 0 ? settingNamed(in, board) : 0;
        if (set) r.warnings.push_back({n, msg(W_WAITNAME, n, in.c_str(), IN_NAMES[k], SCRIPT_SETTINGS[set - 1])});
        if (!has_for || in.empty()) err(M_WAITFORM);
        else if (found < 0) {
          // the message is given
        } else if (k < 0) err(M_INPUT, in.c_str());
        else if (k >= 2 && !board.buttons_are_triggers) err(M_LEARN, x.c_str());
        else if (board.effect_inputs & (1u << k)) err(M_OWNED, x.c_str());
        else { st.op = ScriptStep::Op::Wait; st.target = (uint16_t)k; good = true; }
      } else {
        timed = true;
        std::string v_raw = trim(raw.substr(raw.size() - tm.size()));
        uint8_t set = 0;
        int pt = parseTimeOrSetting(tm, board, st.ms, set);
        if (pt == 0) err(M_TIME, v_raw.c_str());
        else if (pt == -2) err(M_NOTATIME, tm.c_str());
        else if (pt < 0) err(M_TOOLONG);
        else { st.op = ScriptStep::Op::Sleep; st.setting = set; good = true; }
      }
    } else if (startsWith(l, "limit") && (l.size() == 5 || l[5] == ' ')) {
      // limit <N> runs per <time>: a declaration of the effect, no step
      LimitLine lim;
      readLimit(l, raw, board, lim);
      if (lim.msg >= 0) err((Msg)lim.msg, lim.word.c_str());
      else if (f.limit_line) err(M_TWOLIMITS, f.name.c_str(), "", "", f.limit_line);
      else {
        f.limit_line = n;
        f.limit_runs = lim.runs;
        f.limit_runs_setting = lim.runs_setting;
        f.limit_ms = lim.ms;
        f.limit_ms_setting = lim.ms_setting;
        declared = true;
      }
    } else if (startsWith(l, "pulse") && (l.size() == 5 || l[5] == ' ')) {
      PulseLine pl;
      readPulse(l, raw, board, pl);
      uint16_t t = 0;
      if (!pl.form) err(M_PULSEFORM);
      else if (target(pl.target, t)) {
        if (scriptIsDmx(t)) err(M_NOPULSE, shown(t, pl.target).c_str());
        else if (pl.pt == 0) err(M_TIME, pl.tm_raw.c_str());
        else if (pl.pt == -2) err(M_NOTATIME, pl.tm.c_str());
        else if (pl.pt < 0) err(M_TOOLONG);
        else if (t >= 4 && !pl.setting && pl.ms < RELAY_MIN_DWELL_MS) err(M_PULSESHORT, shown(t, pl.target).c_str());
        else if (targetChecks(t)) {
          st.op = ScriptStep::Op::Pulse;
          st.target = t;
          st.value = FULL;
          st.ms = pl.ms;
          st.setting = pl.setting;
          good = true;
        }
      }
    } else if (startsWith(l, "fade") && (l.size() == 4 || l[4] == ' ')) {
      timed = true;
      std::string rest = " " + collapse(l.substr(4)) + " ";
      std::string rcol = " " + collapse(raw.substr(4)) + " ";  // the same, as written
      size_t to = rest.find(" to "), over = rest.find(" over ");
      if (to == std::string::npos || over == std::string::npos || over < to + 3) err(M_FADEFORM);
      else {
        std::string w = trim(rest.substr(0, to)), lv = trim(rest.substr(to + 4, over - to - 4)), tm = trim(rest.substr(over + 6));
        std::string lv_raw = trim(rcol.substr(to + 4, over - to - 4)), tm_raw = trim(rcol.substr(over + 6));
        uint16_t t = 0, v = 0;
        // the time is read first (it only fills st.ms); the checks below keep their order
        int pt = tm.empty() ? 0 : parseTime(tm, st.ms);
        if (w.empty() || lv.empty() || tm.empty()) err(M_FADEFORM);
        else if (target(w, t)) {
          if (!parseLevel(lv, scriptIsDmx(t), v)) err(M_LEVEL, lv_raw.c_str());
          else if (!dimmable(t)) err(M_NOFADE, shown(t, w).c_str());
          else if (pt == 0) err(M_TIME, tm_raw.c_str());
          else if (pt < 0) err(M_TOOLONG);
          else if (targetChecks(t)) { st.op = ScriptStep::Op::Fade; st.target = t; st.value = v; good = true; }
        }
      }
    } else if (startsWith(l, "flicker") && (l.size() == 7 || l[7] == ' ')) {
      timed = true;
      std::string rest = " " + collapse(l.substr(7)) + " ";
      std::string rcol = " " + collapse(raw.substr(7)) + " ";
      size_t fo = rest.find(" for ");
      if (fo == std::string::npos) err(M_FLICKERFORM);
      else {
        std::string w = trim(rest.substr(0, fo)), tm = trim(rest.substr(fo + 5)), tm_raw = trim(rcol.substr(fo + 5));
        uint16_t t = 0;
        int pt = tm.empty() ? 0 : parseTime(tm, st.ms);  // read first (it only fills st.ms)
        if (w.empty() || tm.empty()) err(M_FLICKERFORM);
        else if (target(w, t)) {
          if (!dimmable(t)) err(M_NOFLICKER, shown(t, w).c_str());
          else if (pt == 0) err(M_TIME, tm_raw.c_str());
          else if (pt < 0) err(M_TOOLONG);
          else if (targetChecks(t)) { st.op = ScriptStep::Op::Flicker; st.target = t; good = true; }
        }
      }
    } else if (startsWith(l, "sleep") && (l.size() == 5 || l[5] == ' ' || isDigit(l[5]))) {
      // sleep <time>: the older spelling of wait <time>, read the same
      timed = true;
      std::string v_raw = trim(raw.substr(5));
      uint8_t set = 0;
      int pt = parseTimeOrSetting(lower(v_raw), board, st.ms, set);
      if (pt == 0) err(M_TIME, v_raw.c_str());
      else if (pt == -2) err(M_NOTATIME, lower(v_raw).c_str());
      else if (pt < 0) err(M_TOOLONG);
      else { st.op = ScriptStep::Op::Sleep; st.setting = set; good = true; }
    } else if (l == "audio_stop") {
      st.op = ScriptStep::Op::Stop;
      good = true;
    } else {
      size_t eq = l.find_first_of("=:");
      std::string key = eq == std::string::npos ? std::string() : trim(l.substr(0, eq));
      std::string v_raw = eq == std::string::npos ? std::string() : trim(raw.substr(eq + 1));
      if (key.empty()) {
        // the commonest mistakes get the line written the right way
        size_t sp = l.find(' ');
        std::string w0 = l.substr(0, sp), w1 = sp == std::string::npos ? std::string() : trim(l.substr(sp + 1));
        bool is_out = false;
        for (auto o : OUT_NAMES) is_out |= w0 == o;
        for (auto& x : named) is_out |= x.terminal < CHANNEL_COUNT && !x.problem && x.word == w0;
        std::string press = collapse(l);
        if (startsWith(press, "pixels press") && (press.size() == 12 || press[12] == ' ')) {
          // pixels press <button>: a trigger button of the pattern the strip shows
          std::string b = trim(press.substr(12));
          uint16_t at = 0;
          if (!board.pattern_player) err(M_NOPLAYER);
          else if (b.empty()) err(M_PRESSFORM);
          else if (!scriptNameOk(b)) err(M_PRESSNAME, trim(collapse(raw).substr(12)).c_str());
          else if (nameIndex(b, at)) {
            st.op = ScriptStep::Op::Press;
            st.value = at;
            good = true;
            press_lines.push_back({n, (uint8_t)at});
          }
        } else if (startsWith(l, "turn ")) err(M_HINTTURN);
        else if (startsWith(l, "play ") && l.size() > 5) {
          std::string what = trim(raw.substr(5));
          if (startsWith(lower(what), "sound ")) what = trim(what.substr(6));  // play sound 1 -> audio_single = 1
          if (what.empty() || leadingDigits(what) != what.size()) what = "1";  // a sound is its number, never a name
          err(M_HINTPLAY, what.c_str());
        } else if ((w0 == "delay" || w0 == "pause") && !w1.empty()) err(M_HINTSLEEP, trim(raw.substr(raw.size() - w1.size())).c_str());
        else if (is_out && !w1.empty()) err(M_HINTSET, w0.c_str(), w1.c_str());
        else if ((w0 == "pixels" || w0 == "pixel") && !w1.empty()) err(M_HINTSET, "pixels", w1.c_str());
        else err(M_UNKNOWN, raw.c_str());
      } else if (key == "audio_single" || key == "audio_loop") {
        std::string file = v_raw;
        bool quoted = file.size() >= 2 && (file[0] == '"' || file[0] == '\'') && file.back() == file[0];
        if (quoted) file = trim(file.substr(1, file.size() - 2));
        std::string lf = lower(file);
        if (key == "audio_loop" && ((quoted && file.empty()) || lf == "off" || lf == "false")) {
          // audio_loop = "" / off / false: the background loop is off (SS-R7)
          st.op = ScriptStep::Op::LoopOff;
          good = true;
        } else if (file.empty()) err(M_NOSOUND);
        else {
          // a sound is its number (sound.h): never a file name
          size_t d = leadingDigits(file);
          uint64_t num = d == file.size() ? digitsValue(file) : 0;
          if (d != file.size()) err(M_SOUND, file.c_str());
          else {
            if (num < 1 || num > SCRIPT_MAX_TRACK) err(M_SOUNDRANGE, "", "", "", (size_t)num);
            else {
              st.op = key == "audio_single" ? ScriptStep::Op::Single : ScriptStep::Op::Loop;
              st.value = (uint16_t)num;
              good = true;
            }
          }
        }
      } else if (key == "audio_volume") {
        // audio_volume = N% / off: the player's volume from here on (0-30, halves up), until it changes or a restart
        uint16_t pm = 0;
        std::string lv = lower(v_raw);
        if (lv == "0") lv = "off";
        if (v_raw.empty() || !parseLevel(lv, false, pm)) err(M_VOLUME, v_raw.c_str());
        else { st.op = ScriptStep::Op::Volume; st.value = volumeOf(lv); good = true; }
      } else if (key == "pixel") {
        err(M_PIXEL, v_raw.empty() ? "rainbow" : collapse(lower(v_raw)).c_str());  // the strip's line is "pixels"
      } else if (key == "pixels") {
        PixelLine px;
        readPixels(v_raw, comment, px);
        uint16_t at = 0;
        if (px.effect == (uint8_t)Effect::Kind::Pattern && !board.pattern_player) err(M_NOPLAYER);
        else if (px.msg >= 0) err((Msg)px.msg, px.word.c_str());
        else if (px.effect == (uint8_t)Effect::Kind::Pattern) {
          if (nameIndex(px.name, at)) {
            st.op = ScriptStep::Op::Pixels;
            st.target = px.effect;
            st.value = at;
            good = true;
            pattern_lines.push_back({n, (uint8_t)at});
          }
        } else { st.op = ScriptStep::Op::Pixels; st.target = px.effect; st.ms = px.rgb; good = true; }
      } else {
        uint16_t t = 0, v = 0;
        if (target(key, t)) {
          if (!parseLevel(lower(v_raw), scriptIsDmx(t), v)) err(M_LEVEL, v_raw.c_str());
          else if (!dimmable(t) && lower(v_raw) != "on" && lower(v_raw) != "off") err(M_ONOFF, shown(t, key).c_str(), v_raw.c_str());
          else if (targetChecks(t)) { st.op = ScriptStep::Op::Set; st.target = t; st.value = v; good = true; }
        }
      }
    }
    f.has_wait |= is_wait;
    if (timed && (!good || st.ms > 0 || st.setting)) f.takes_time = true;  // a time setting may take time
    if (!good || declared) continue;
    if (!via.empty() && st.op != ScriptStep::Op::Wait && !scriptIsDmx(st.target) && called[st.target].empty()) called[st.target] = via;
    f.steps.push_back(st);
    if (st.op == ScriptStep::Op::Single || st.op == ScriptStep::Op::Loop) f.plays_sound = true;
    if (st.op == ScriptStep::Op::Set || st.op == ScriptStep::Op::Fade || st.op == ScriptStep::Op::Flicker) {
      bool seen = false;
      for (auto& x : f.last)
        if (x.first == st.target) {
          seen = true;
          if (st.op != ScriptStep::Op::Flicker) x.second = st.value;  // a flicker gives its level back
        }
      if (!seen) f.last.push_back({st.target, st.op == ScriptStep::Op::Flicker ? (uint16_t)0 : st.value});
    }
  }

  // per effect, on its header line
  int sound_first = -1;
  for (size_t e = 0; e < fx.size(); e++) {
    FxWork& f = fx[e];
    // one error per line: the header's own error first, then at most one of these
    bool header_has = false;
    for (auto& m : r.errors) header_has |= m.line == f.line;
    if (header_has) {
    } else if (f.lines == 0) r.errors.push_back({f.line, msg(M_EMPTY, f.line, f.name.c_str())});
    else if (!f.has_wait && !f.takes_time) r.errors.push_back({f.line, msg(M_NOPAUSE, f.line, f.name.c_str())});
    else if (f.lines > SCRIPT_MAX_LINES_PER_EFFECT) r.errors.push_back({f.line, msg(M_EFFECTLINES, f.line, f.name.c_str(), "", "", f.lines)});
    // the lines in the order they run: from the first wait for to the last line, then on through the lines above
    // it; an effect with no wait for goes round and round (its second time round is the one that counts)
    size_t top = 0;
    while (top < f.steps.size() && f.steps[top].op != ScriptStep::Op::Wait) top++;
    const bool waits = top < f.steps.size();
    if (!waits) top = 0;
    const size_t laps = waits ? 1 : 2, count = f.steps.size();
    // a relay or contact switched again sooner than RELAY_MIN_DWELL_MS after its last change (a wait for resets it;
    // two lines with no time between them never reach the pin)
    long since[4] = {-1, -1, -1, -1};
    uint16_t was[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < laps * count; i++) {
      const ScriptStep& st = f.steps[(top + i) % count];
      const bool told = i + count >= laps * count;  // the last time round
      if (st.op == ScriptStep::Op::Wait) { for (auto& x : since) x = -1; continue; }
      if (st.op == ScriptStep::Op::Sleep || st.op == ScriptStep::Op::Fade || st.op == ScriptStep::Op::Flicker)
        for (auto& x : since) if (x >= 0) x = std::min<long>(x + (long)std::min<uint32_t>(st.ms, RELAY_MIN_DWELL_MS), RELAY_MIN_DWELL_MS);
      if (st.op == ScriptStep::Op::Pulse && st.target >= 4) { since[st.target - 4] = 0; was[st.target - 4] = 0; continue; }
      if (st.op != ScriptStep::Op::Set || scriptIsDmx(st.target) || st.target < 4) continue;
      int k = st.target - 4;
      if (told && since[k] > 0 && since[k] < (long)RELAY_MIN_DWELL_MS && st.value != was[k])
        r.errors.push_back({st.line, msg(M_RELAYFAST, st.line, full(st.target).c_str(), "", "", (unsigned long)since[k])});
      if (since[k] != 0 || st.value != was[k]) since[k] = 0;
      was[k] = st.value;
    }
    // an output left on when the effect is back at its first wait for: after its last line and the lines above
    // that wait for, which set the outputs they set on purpose; an effect with no wait for never sits and waits
    for (auto& x : f.last) {
      bool set_above = !waits;
      for (size_t i = 0; i < top && !set_above; i++)
        set_above = f.steps[i].target == x.first && (f.steps[i].op == ScriptStep::Op::Set || f.steps[i].op == ScriptStep::Op::Fade ||
                                                     f.steps[i].op == ScriptStep::Op::Pulse);
      if (x.second > 0 && !set_above) r.warnings.push_back({f.line, msg(W_STILLON, f.line, f.name.c_str(), full(x.first).c_str())});
    }
    if (f.plays_sound) {
      if (sound_first < 0) sound_first = (int)e;
      else r.warnings.push_back({f.line, msg(W_TWOSOUND, f.line, fx[sound_first].name.c_str(), f.name.c_str())});
    }
  }
  // a press of a button none of the script's patterns has, when the board has every one of them stored (a board
  // without the pattern player stores none)
#if PT_PATTERNS
  if (!press_lines.empty() && !pattern_lines.empty()) {
    std::vector<const ScriptBoard::PatternButtons*> shown;
    bool all_stored = true;
    for (auto& pl : pattern_lines) {
      const ScriptBoard::PatternButtons* found = nullptr;
      for (auto& bp : board.patterns)
        if (bp.pattern == names[pl.second]) found = &bp;
      if (!found) { all_stored = false; break; }
      if (std::find(shown.begin(), shown.end(), found) == shown.end()) shown.push_back(found);
    }
    for (auto& pr : press_lines) {
      if (!all_stored) break;
      const std::string& b = names[pr.second];
      bool has = false;
      for (auto* x : shown) has |= std::find(x->buttons.begin(), x->buttons.end(), b) != x->buttons.end();
      if (has) continue;
      if (shown.size() > 1) { r.errors.push_back({pr.first, msg(M_NOBUTTONANY, pr.first, b.c_str())}); continue; }
      const auto& bs = shown[0]->buttons;
      std::string list;
      for (size_t i = 0; i < bs.size(); i++) list += (i == 0 ? "" : i + 1 == bs.size() ? " and " : ", ") + bs[i];
      if (bs.empty()) r.errors.push_back({pr.first, msg(M_NOBUTTONS, pr.first, shown[0]->pattern.c_str())});
      else r.errors.push_back({pr.first, msg(M_NOBUTTON, pr.first, shown[0]->pattern.c_str(), b.c_str(), list.c_str())});
    }
  }
#endif
  byLine(r.errors);
  byLine(r.warnings);
  // the whole script, at the end
  if (fx.size() > SCRIPT_MAX_EFFECTS) r.errors.push_back({0, msg(M_EFFECTS, 0, "", "", "", fx.size())});
  if (lines.size() > SCRIPT_MAX_LINES) r.errors.push_back({0, msg(M_LINES, 0, "", "", "", lines.size())});
  if (text.size() > SCRIPT_MAX_BYTES) r.errors.push_back({0, msg(M_BYTES, 0, "", "", "", text.size())});
  if (!r.ok()) { out.reset(); return r; }

  // the table (the limits above make it fit; the tell pool is filled as the lines are read)
  for (auto& w : names) strncpy(out.name[out.names++], w.c_str(), SCRIPT_MAX_NAME);
  for (auto& f : fx) {
    ScriptEffect& e = out.fx[out.effects++];
    strncpy(e.name, f.name.c_str(), SCRIPT_MAX_NAME);
    e.line = f.line;
    e.first = out.steps;
    e.count = (uint16_t)f.steps.size();
    e.limit = f.limit_line != 0;
    e.limit_runs = f.limit_runs;
    e.limit_runs_setting = f.limit_runs_setting;
    e.limit_ms = f.limit_ms;
    e.limit_ms_setting = f.limit_ms_setting;
    for (auto& s : f.steps) {
      out.step[out.steps++] = s;
      if (s.op == ScriptStep::Op::Wait) out.inputs |= (uint8_t)(1u << s.target);
      else if (s.op == ScriptStep::Op::Pixels) out.pixels = true;
      else if (s.op == ScriptStep::Op::Set || s.op == ScriptStep::Op::Fade || s.op == ScriptStep::Op::Flicker ||
               s.op == ScriptStep::Op::Pulse) {
        if (scriptIsDmx(s.target)) out.dmx = true;
        else out.channels |= (uint8_t)(1u << s.target);
      }
    }
  }
  return r;
}

// The step table as JSON, the one place it is written (README "The step table": keys in this order).
void scriptTableJson(const ScriptProgram& p, JsonObject o) {
  static const char* const OP[] = {"wait_for", "set", "sleep", "fade", "flicker", "audio_single", "audio_loop", "audio_stop", "pulse", "audio_loop_off", "audio_volume", "pixels", "tell", "pixels_press"};
  // what each op writes after "op": T target, L level, M ms (or the setting), S sound, V volume, I input,
  // P the pixel effect and its colour ("#rrggbb", for an effect that takes one)
  // B the tell line's board and its effect, or its setting and value; K the pattern's button a press presses
  static const char* const KEYS[] = {"I", "TL", "M", "TLM", "TM", "S", "S", "", "TM", "", "V", "P", "B", "K"};
  PT_ONE_PER(OP, ScriptStep::Op::Press);
  PT_ONE_PER(KEYS, ScriptStep::Op::Press);
  JsonArray effects = o["effects"].to<JsonArray>();
  for (int e = 0; e < p.effects; e++) {
    const ScriptEffect& f = p.fx[e];
    JsonObject fo = effects.add<JsonObject>();
    fo["name"] = std::string(f.name);  // copied: the table may go before the JSON
    fo["line"] = f.line;
    // the advanced lines, only where the effect has them (README "The step table")
    if (f.limit) {
      JsonObject lo = fo["limit"].to<JsonObject>();
      if (f.limit_runs_setting) lo["runs"] = SCRIPT_SETTINGS[f.limit_runs_setting - 1];
      else lo["runs"] = f.limit_runs;
      if (f.limit_ms_setting) lo["ms"] = SCRIPT_SETTINGS[f.limit_ms_setting - 1];
      else lo["ms"] = f.limit_ms;
    }
    JsonArray steps = fo["steps"].to<JsonArray>();
    for (int i = 0; i < f.count; i++) {
      const ScriptStep& s = p.step[f.first + i];
      JsonObject so = steps.add<JsonObject>();
      so["line"] = s.line;
      so["op"] = OP[idx(s.op)];
      for (const char* k = KEYS[idx(s.op)]; *k; k++) {
        if (*k == 'I') so["input"] = IN_UPPER[s.target];
        else if (*k == 'T') so["target"] = targetUpper(s.target);
        else if (*k == 'L') so["level"] = s.value;
        else if (*k == 'M' && s.setting) so["setting"] = SCRIPT_SETTINGS[s.setting - 1];
        else if (*k == 'M') so["ms"] = s.ms;
        else if (*k == 'V') so["volume"] = s.value;
        else if (*k == 'K') so["button"] = std::string(s.value < p.names ? p.name[s.value] : "");
        else if (*k == 'P' && s.target == (uint16_t)Effect::Kind::Pattern) {
          so["effect"] = "pattern";
          so["pattern"] = std::string(s.value < p.names ? p.name[s.value] : "");
        } else if (*k == 'P') {
          so["effect"] = SCRIPT_PIXEL_EFFECTS[s.target < SCRIPT_PIXEL_EFFECT_COUNT ? s.target : 0];
          if (scriptPixelColoured((uint8_t)s.target)) so["color"] = colorText(Rgb((uint8_t)(s.ms >> 16), (uint8_t)(s.ms >> 8), (uint8_t)s.ms));
        }
        else if (*k == 'B') {
          ScriptTell t;
          if (!scriptTellOf(p, s, t)) continue;
          so["board"] = t.board;
          if (t.set) { so["setting"] = t.what; so["value"] = t.value; }
          else so["effect"] = t.what;
        }
        else so["sound"] = s.value;
      }
    }
  }
}

void scriptCheckJson(const ScriptCheck& c, const ScriptProgram* table, JsonObject o) {
  o["ok"] = c.ok();
  JsonArray er = o["errors"].to<JsonArray>();
  for (auto& m : c.errors) { JsonObject x = er.add<JsonObject>(); x["line"] = m.line; x["message"] = m.text; }
  JsonArray wa = o["warnings"].to<JsonArray>();
  for (auto& m : c.warnings) { JsonObject x = wa.add<JsonObject>(); x["line"] = m.line; x["message"] = m.text; }
  if (c.ok() && table) scriptTableJson(*table, o["table"].to<JsonObject>());
}

void scriptReplyJson(const ScriptCheck& c, int effects, JsonObject o) {
  scriptCheckJson(c, nullptr, o);
  if (c.ok()) o["effects"] = effects;
}

// ------------------------------------------------------------------ the runner

void ScriptRunner::load(const ScriptProgram* p, Millis now) {
  p_ = p && p->effects ? p : nullptr;
  for (auto& s : st_) s = State();
  for (auto& l : level_) l = 0;
  memset(dmx_, 0, sizeof dmx_);
  for (auto& g : gate_) g.reset();
  for (auto& x : local_go_) x = false;
  for (auto& x : pending_) x = false;
  for (auto& x : queued_) x = false;
  for (auto& x : conflicts_) x = 0;
  for (auto& x : pulse_until_) x = 0;
  for (auto& x : press_armed_) x = false;  // armed after a release: held at load never starts
  pending_sound_.clear();
  tell_head_ = tell_n_ = 0;
  press_n_ = 0;
  pixel_step_ = -1;  // the strip keeps no effect from an earlier script
  rng_ = FLICKER_SEED;
  waits_ = want_ = 0;
  if (!p_) return;
  for (int e = 0; e < p_->effects; e++) {
    order_[e] = (uint8_t)e;
    recovery_[e] = 0;
    const ScriptEffect& f = p_->fx[e];
    top_[e] = 0;
    while (top_[e] < f.count && p_->step[f.first + top_[e]].op != ScriptStep::Op::Wait) top_[e]++;
    if (top_[e] < f.count) waits_ = (uint8_t)(waits_ | (1u << e));
    else top_[e] = 0;
    if (!waits(e)) continue;
    for (uint16_t i = (uint16_t)(top_[e] + 1); i < f.count && !recovery_[e]; i++) {
      const ScriptStep& x = p_->step[p_->fx[e].first + i];
      if ((x.op == ScriptStep::Op::Set || x.op == ScriptStep::Op::Fade || x.op == ScriptStep::Op::Pulse) && x.target == 1 && x.value > 0)
        recovery_[e] = (uint16_t)(i + 1);
    }
  }
  // stable: by the first wait for's input (an effect with no wait for after them), then the script's order
  auto key = [&](int e) -> int { return waits(e) ? p_->step[p_->fx[e].first + top_[e]].target : INPUT_COUNT; };
  for (int i = 1; i < p_->effects; i++)
    for (int j = i; j > 0 && key(order_[j]) < key(order_[j - 1]); j--) {
      uint8_t t = order_[j]; order_[j] = order_[j - 1]; order_[j - 1] = t;
    }
  // the outputs each effect uses (its lines that set, fade, flicker or pulse one, and the pixel strip for a pixels
  // line): two effects that share one never run at the same time (SS-R8)
  auto uses = [](const ScriptStep& x) -> int32_t {  // the output a step uses, -1 none
    if (x.op == ScriptStep::Op::Pixels) return 0x10000;  // past every target
    bool out = x.op == ScriptStep::Op::Set || x.op == ScriptStep::Op::Fade || x.op == ScriptStep::Op::Flicker || x.op == ScriptStep::Op::Pulse;
    return out ? (int32_t)x.target : -1;
  };
  for (int e = 0; e < p_->effects; e++)
    for (int f = 0; f < p_->effects; f++) {
      if (e == f) continue;
      const ScriptEffect &a = p_->fx[e], &b = p_->fx[f];
      bool shared = false;
      for (int i = 0; i < a.count && !shared; i++) {
        int32_t x = uses(p_->step[a.first + i]);
        if (x < 0) continue;
        for (int j = 0; j < b.count && !shared; j++) shared = uses(p_->step[b.first + j]) == x;
      }
      if (shared) conflicts_[e] = (uint8_t)(conflicts_[e] | (1u << f));
    }
  for (int e = 0; e < p_->effects; e++) {
    gate_[e].setMaxRuns(limitRuns(e));
    enter(e, now);
  }
}

PT_STEP_CODE void ScriptRunner::setSettings(const uint32_t values[SCRIPT_SETTING_COUNT]) {
  for (int k = 0; k < SCRIPT_SETTING_COUNT; k++) values_[k] = values[k];
  if (!p_) return;
  for (int e = 0; e < p_->effects; e++) gate_[e].setMaxRuns(limitRuns(e));  // a changed limit: from the next run (SM-L7)
}

PT_STEP_CODE void ScriptRunner::setRemote(bool enabled, uint8_t free_effects) {
  remote_enabled_ = enabled;
  remote_free_ = free_effects;
  if (!enabled)  // the enable went off: a queued start that needed it goes (SM-Q5)
    for (int e = 0; e < SCRIPT_MAX_EFFECTS; e++)
      if (!(remote_free_ & (1u << e))) queued_[e] = false;
}

PT_STEP_CODE uint32_t ScriptRunner::limitRuns(int e) const {
  if (!p_ || !p_->fx[e].limit) return 1;
  const ScriptEffect& f = p_->fx[e];
  uint32_t n = f.limit_runs_setting ? values_[f.limit_runs_setting - 1] : f.limit_runs;
  return n < 1 ? 1 : n > SCRIPT_LIMIT_MAX_RUNS ? SCRIPT_LIMIT_MAX_RUNS : n;
}

PT_STEP_CODE uint32_t ScriptRunner::limitMs(int e) const {
  if (!p_ || !p_->fx[e].limit) return 0;  // no window: nothing is held back
  const ScriptEffect& f = p_->fx[e];
  uint32_t ms = f.limit_ms_setting ? values_[f.limit_ms_setting - 1] : f.limit_ms;
  return ms > SCRIPT_LIMIT_MAX_MS ? SCRIPT_LIMIT_MAX_MS : ms;
}

int ScriptRunner::lead() const {
  if (p_)
    for (int e = 0; e < p_->effects; e++)
      if (waits(e)) return e;
  return 0;
}

int ScriptRunner::effectNamed(const std::string& name) const {
  if (!p_) return -1;
  std::string n;
  for (char c : name) n += c == '_' ? ' ' : (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  n = collapse(n);
  for (int e = 0; e < p_->effects; e++)
    if (n == p_->fx[e].name) return e;
  return -1;
}

PT_STEP_CODE bool ScriptRunner::busy(int e) const {
  for (int f = 0; f < p_->effects; f++)
    if ((conflicts_[e] & (1u << f)) && st_[f].running) return true;
  return false;
}

PT_STEP_CODE bool ScriptRunner::canStart(int e, Millis now) const {
  return atTop(e) && now >= boot_end_ && !paused_ && !windowFull(e, now) && !busy(e);
}

PT_STEP_CODE bool ScriptRunner::ready(int e, Millis now) const { return p_ && e < p_->effects && canStart(e, now); }

PT_STEP_CODE bool ScriptRunner::armed(int e, Millis now) const {
  if (!ready(e, now)) return false;
  if (readyRule(e)) return gate_[e].released;
  return returnRule(e) ? press_armed_[e] : st_[e].released;
}

// the first wait for's input follows the "ready" rule (trig1_wait / trig2_wait)
PT_STEP_CODE bool ScriptRunner::readyRule(int e) const {
  if (!waits(e)) return false;
  int k = p_->step[p_->fx[e].first + head(e)].target;
  return k < 2 && ready_rule_[k];
}

PT_STEP_CODE bool ScriptRunner::returnRule(int e) const {
  if (!waits(e)) return false;
  int k = p_->step[p_->fx[e].first + head(e)].target;
  return k < 2 && rule_set_[k] && !ready_rule_[k];
}

bool ScriptRunner::anyQueued() const {
  for (auto q : queued_) if (q) return true;
  return false;
}

uint32_t ScriptRunner::totalRuns() const {
  uint32_t n = 0;
  if (p_)
    for (int e = 0; e < p_->effects; e++) n += st_[e].runs;
  return n;
}

ScriptRunner::Answer ScriptRunner::remoteStart(int e, Millis now, bool allowed) {
  if (!p_ || e < 0 || e >= p_->effects) return Answer::NoEffect;
  if (!waits(e)) return Answer::NeverWaits;                   // nothing starts it: it goes round by itself
  if (!allowed) return Answer::Disabled;                      // SM-Q4
  if (windowFull(e, now)) return Answer::CooldownFull;        // SM-L5
  if (canStart(e, now)) { queued_[e] = true; return Answer::Starts; }  // on the next sample
  if (queued_[e]) return Answer::AlreadyQueued;               // SM-Q1
  queued_[e] = true;
  return Answer::Queued;
}

bool ScriptRunner::cancelRemote() {
  bool any = false;
  for (auto& q : queued_) { any |= q; q = false; }
  return any;
}

bool ScriptRunner::takeTell(uint16_t& step) {
  if (!tell_n_) return false;
  step = tell_ring_[tell_head_];
  tell_head_ = (uint8_t)((tell_head_ + 1) % SCRIPT_TELL_RING);
  tell_n_--;
  return true;
}

uint16_t ScriptRunner::line(int e) const {
  if (!p_ || e >= p_->effects) return 0;
  const ScriptEffect& f = p_->fx[e];
  return f.count ? p_->step[f.first + st_[e].pc].line : f.line;
}

PT_STEP_CODE uint32_t ScriptRunner::rnd() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x;
  return x;
}

PT_STEP_CODE uint16_t ScriptRunner::get(uint16_t t) const { return scriptIsDmx(t) ? dmx_[t - SCRIPT_DMX] : level_[t]; }
PT_STEP_CODE void ScriptRunner::put(uint16_t t, uint16_t v) {
  if (scriptIsDmx(t)) dmx_[t - SCRIPT_DMX] = (uint8_t)v;
  else { level_[t] = v; pulse_until_[t] = 0; }  // a line that sets the output ends a pulse on it
}

PT_STEP_CODE void ScriptRunner::enter(int e, Millis now) {
  State& s = st_[e];
  const ScriptStep& st = p_->step[p_->fx[e].first + s.pc];
  s.entered = true;
  s.since = now;
  switch (st.op) {
    case ScriptStep::Op::Wait: s.released = false; break;
    case ScriptStep::Op::Sleep: s.until = now + timeOf(st); break;
    case ScriptStep::Op::Fade: s.from = get(st.target); s.until = now + st.ms; break;
    case ScriptStep::Op::Flicker: s.before = get(st.target); s.until = now + st.ms; s.next_flicker = now; break;
    case ScriptStep::Op::Loop: case ScriptStep::Op::Set: case ScriptStep::Op::Single: case ScriptStep::Op::Stop:
    case ScriptStep::Op::Pulse: case ScriptStep::Op::LoopOff: case ScriptStep::Op::Volume: case ScriptStep::Op::Pixels:
    case ScriptStep::Op::Tell:
    case ScriptStep::Op::Press:
    default: break;
  }
}

// The next line; after the last one the effect goes back to its first. Back at its first wait for (an effect with
// no wait for: at its first line) it is no longer running.
PT_STEP_CODE void ScriptRunner::advance(int e, Millis now) {
  State& s = st_[e];
  s.pc++;
  if (s.pc >= p_->fx[e].count) s.pc = 0;
  if (s.pc == top_[e]) s.running = s.in_run = false;
  enter(e, now);
}

// A run starts at the effect's first wait for (its window counts it, SM-L1; a new release is needed, SM-T6).
PT_STEP_CODE void ScriptRunner::start(int e, Millis now) {
  gate_[e].started(now, limitMs(e));
  local_go_[e] = false;
  pending_[e] = false;
  queued_[e] = false;
  State& s = st_[e];
  s.running = s.in_run = true;
  s.runs++;
  advance(e, now);
}

PT_STEP_CODE ScriptInputs ScriptRunner::view(int e, const ScriptInputs& in) const {
  ScriptInputs v = in;
  bool free = (remote_free_ >> e) & 1u;
  for (int k = 0; k < INPUT_COUNT; k++) {
    if (free) {
      // joined with the pin: closed while either is; since the earlier closing, or the later opening
      const bool pin = in.closed[k], test = in.test[k];
      if (pin && test) v.since[k] = in.since[k] < in.test_since[k] ? in.since[k] : in.test_since[k];
      else if (test) v.since[k] = in.test_since[k];
      else if (!pin && in.test_since[k] > in.since[k]) v.since[k] = in.test_since[k];
      v.closed[k] = pin || test;
    }
    v.test[k] = false;
  }
  return v;
}

PT_STEP_CODE void ScriptRunner::sampleWait(int e, Millis now, const ScriptInputs& in_all) {
  const ScriptInputs in = view(e, in_all);
  State& s = st_[e];
  const ScriptStep& st = p_->step[p_->fx[e].first + s.pc];
  if (st.op != ScriptStep::Op::Wait || s.released) return;
  if (s.pc == head(e) && readyRule(e)) return;  // the "ready" rule's release is the gate's
  if (s.pc == head(e) && windowFull(e, now)) return;  // a release in a used-up window does not count (SM-T7)
  int k = st.target;
  if (in.closed[k]) return;
  Millis from = in.since[k] > s.since ? in.since[k] : s.since;
  if (now >= from && now - from >= release_ms_) s.released = true;
}

PT_STEP_CODE bool ScriptRunner::stepOnce(int e, Millis now, const ScriptInputs& in_all, std::vector<SoundCommand>& sound) {
  const ScriptInputs in = view(e, in_all);
  State& s = st_[e];
  if (!s.running && !atTop(e)) {
    // the lines above the first wait for at power-on (or after a pause), or an effect with no wait for at its first
    // line: they begin straight away (the boot lockout holds back inputs, not these) once the outputs they use are
    // free (SS-R8). An effect with no wait for gives way to an effect it shares outputs with that is being started,
    // and begins its first line at most every SCRIPT_MIN_ROUND_MS.
    if (paused_ || busy(e)) return false;
    if (!waits(e) && ((conflicts_[e] & want_) || (s.rounded && now - s.round < SCRIPT_MIN_ROUND_MS))) return false;
    s.running = true;
    if (!waits(e)) { s.round = now; s.rounded = true; }
    enter(e, now);
  }
  const ScriptStep& st = p_->step[p_->fx[e].first + s.pc];
  const bool was_running = s.running;
  switch (st.op) {
    case ScriptStep::Op::Wait: {
      const bool at_top = atTop(e);
      const bool can = at_top && canStart(e, now);
      if (at_top) {
        // at the top: a queued remote start first, then a remembered start, then the input (STATE-MACHINE.md §13).
        // A remembered start runs only if its input is still held now that it can (memory with check, SS-R8)
        if (can && pending_[e] && !queued_[e] && !in.closed[st.target]) pending_[e] = false;
        if (can && (queued_[e] || pending_[e] || (readyRule(e) && local_go_[e]))) {
          start(e, now);
          return true;
        }
        if (readyRule(e) || returnRule(e) || paused_ || windowFull(e, now)) return false;  // a used-up window ignores presses (SM-L5)
      }
      sampleWait(e, now, in);
      int k = st.target;
      if (!s.released || !in.closed[k] || now < boot_end_) return false;
      Millis from = in.since[k] > boot_end_ ? in.since[k] : boot_end_;
      if (now - from < debounce_ms_) return false;
      if (!at_top) { advance(e, now); return s.running; }  // false: that was its last line
      if (can) { start(e, now); return true; }
      // its outputs are busy: the start is remembered (one deep) and runs when they are free if the input is
      // still held then (SS-R8)
      pending_[e] = true;
      s.released = false;
      return false;
    }
    case ScriptStep::Op::Set: put(st.target, st.value); break;
    case ScriptStep::Op::Pulse: {
      put(st.target, st.value);
      uint32_t ms = timeOf(st);
      if (st.target >= 4 && ms < RELAY_MIN_DWELL_MS) ms = RELAY_MIN_DWELL_MS;  // a relay or contact: at least 0.1 s
      pulse_until_[st.target] = now + (ms ? ms : 1);
      break;
    }
    case ScriptStep::Op::Sleep:
      if (now < s.until) return false;
      break;
    case ScriptStep::Op::Fade:
      if (now < s.until) {
        int32_t d = (int32_t)st.value - (int32_t)s.from;
        put(st.target, (uint16_t)((int32_t)s.from + (int32_t)((int64_t)d * (int64_t)(now - s.since) / (int64_t)st.ms)));
        return false;
      }
      put(st.target, st.value);
      break;
    case ScriptStep::Op::Flicker:
      if (now < s.until) {
        if (now >= s.next_flicker) {
          put(st.target, (uint16_t)((uint32_t)s.before * (FLICKER_LOW_PERMILLE + rnd() % (1001 - FLICKER_LOW_PERMILLE)) / 1000));
          s.next_flicker = now + FLICKER_MIN_MS + rnd() % (FLICKER_MAX_MS - FLICKER_MIN_MS + 1);
        }
        return false;
      }
      put(st.target, s.before);
      break;
    case ScriptStep::Op::Single: sound.push_back(soundPlay(st.value)); break;
    case ScriptStep::Op::Loop: sound.push_back(soundLoop(st.value)); break;
    case ScriptStep::Op::LoopOff: sound.push_back(soundLoop(0)); break;
    case ScriptStep::Op::Volume: sound.push_back(SoundCommand::volume((uint8_t)st.value)); break;
    case ScriptStep::Op::Stop: sound.push_back(SoundCommand::simple(SoundCommand::Kind::Stop)); break;
    case ScriptStep::Op::Pixels:
      // the strip's effect from now until another pixels line (the controller renders it)
      pixel_step_ = (int16_t)(p_->fx[e].first + s.pc);
      pixel_since_ = now;
      break;
    case ScriptStep::Op::Tell:
      // handed to the network (Controller::linkTake); the line never waits
      if (tell_n_ < SCRIPT_TELL_RING) {
        tell_ring_[(tell_head_ + tell_n_) % SCRIPT_TELL_RING] = (uint16_t)(p_->fx[e].first + s.pc);
        tell_n_++;
      } else {
        tell_dropped_++;
      }
      break;
    case ScriptStep::Op::Press:
      // handed to the strip at the next tick (the controller), never run here
      if (press_n_ < SCRIPT_MAX_PRESSES) press_q_[press_n_++] = (uint8_t)st.value;
      else presses_lost++;
      break;
  }
  advance(e, now);
  return !(was_running && !s.running);  // back at its top: the caller decides what starts next
}

// Every 1 ms: the "ready" rule's gates, then every effect steps (SS-R1).
PT_STEP_CODE void ScriptRunner::sample(Millis now, const ScriptInputs& in) {
  if (!p_) return;
  for (int e = 0; e < p_->effects; e++) {
    sampleWait(e, now, in);
    if (!readyRule(e)) {
      local_go_[e] = false;
      continue;
    }
    // release since the last run, hold to confirm while the effect could start (SM-T, gate.h)
    int k = p_->step[p_->fx[e].first + head(e)].target;
    gate_[e].sampleRaw(now, view(e, in).closed[k]);
  }
  if (!paused_) run(now, in, pending_sound_);
}

PT_STEP_CODE void ScriptRunner::tick(Millis now, const ScriptInputs& in, std::vector<SoundCommand>& sound) {
  (void)in;
  if (!p_) return;
  sound.insert(sound.end(), pending_sound_.begin(), pending_sound_.end());
  pending_sound_.clear();
  for (int c = 0; c < CHANNEL_COUNT; c++)
    if (pulse_until_[c] && now >= pulse_until_[c]) { level_[c] = 0; pulse_until_[c] = 0; }
}

// An effect at its first wait for that is being started (wants): an effect with no wait for that shares an output
// with it does not begin its next time round until it has.
PT_STEP_CODE bool ScriptRunner::wants(int e, Millis now, const ScriptInputs& in) const {
  if (!atTop(e) || now < boot_end_ || windowFull(e, now)) return false;
  if (queued_[e] || pending_[e] || local_go_[e]) return true;
  const int k = p_->step[p_->fx[e].first + top_[e]].target;
  if (!view(e, in).closed[k]) return false;
  return readyRule(e) ? gate_[e].released : !returnRule(e) && st_[e].released;
}

// One pass, in the order of the effects' inputs (an earlier effect goes first when several could start). An
// effect whose run ends goes again at once: a waiting start may begin. Then once more for the effects at their
// top: one whose outputs another effect freed in this pass starts on the same sample.
PT_STEP_CODE void ScriptRunner::run(Millis now, const ScriptInputs& in_all, std::vector<SoundCommand>& sound) {
  const ScriptInputs& in = in_all;  // each effect looks through view(): stepOnce, sampleWait and gateUpdate
  auto gateUpdate = [&](int e) {
    const ScriptInputs v = view(e, in_all);
    if (windowFull(e, now)) { queued_[e] = false; pending_[e] = false; }  // a used-up window drops them (SM-L5)
    if (returnRule(e)) {
      // the "return" rule: a press (debounced, after a debounced release) is remembered one deep and starts the
      // effect as soon as it can, its own run included, if the input is still held then; in the boot lockout,
      // while stopped or in a used-up window it is dropped (the input must be released and pressed again)
      int k = p_->step[p_->fx[e].first + head(e)].target;
      Millis held = now >= v.since[k] ? now - v.since[k] : 0;
      if (!v.closed[k]) {
        if (held >= debounce_ms_ + release_ms_) press_armed_[e] = true;  // the debounced release, then trig_release_ms
      } else if (press_armed_[e] && held >= debounce_ms_) {
        press_armed_[e] = false;  // one press
        if (now >= boot_end_ && !paused_ && !windowFull(e, now)) pending_[e] = true;
      }
      return;
    }
    if (!readyRule(e)) return;
    int k = p_->step[p_->fx[e].first + head(e)].target;
    bool r = canStart(e, now);
    bool go = gate_[e].update(now, v.closed[k], true, r, release_ms_, debounce_ms_);
    local_go_[e] = r && (local_go_[e] || go);  // a hold confirmed on a sample stays until it starts or ready is lost
  };
  auto steps = [&](int e) {
    for (uint16_t k = 0; k <= p_->fx[e].count; k++)  // each step at most once per pass
      if (!stepOnce(e, now, in, sound)) break;
  };
  // the "ready" rule's holds are confirmed for every effect first, so effects whose presses are confirmed on the
  // same sample all count: those that share outputs with one that starts are remembered and run right after it
  for (int e = 0; e < p_->effects; e++) gateUpdate(e);
  want_ = 0;
  if (waits_ != (uint8_t)((1u << p_->effects) - 1))  // an effect with no wait for gives way to these
    for (int e = 0; e < p_->effects; e++)
      if (wants(e, now, in_all)) want_ = (uint8_t)(want_ | (1u << e));
  uint8_t started = 0;
  // effects in the order of their inputs (TRIG1, TRIG2, USER1 ...; the script's order for the same input): of
  // starts on the same sample that share outputs, the lowest input goes first
  for (int round = 0; round < 2; round++)
    for (int i = 0; i < p_->effects; i++) {
      const int e = order_[i];
      if (round == 1 && !atTop(e)) continue;
      if (readyRule(e) && local_go_[e] && atTop(e) && busy(e) && (conflicts_[e] & started)) {
        pending_[e] = true;  // pressed together with an effect it shares outputs with, which went first
        local_go_[e] = false;
      }
      if (round == 1) gateUpdate(e);
      bool was = st_[e].running;
      uint32_t runs = st_[e].runs;
      steps(e);
      if (st_[e].runs != runs) started = (uint8_t)(started | (1u << e));
      if (was && !st_[e].running) {
        // its run ended and its outputs are free: the starts that waited go first, a queued remote start before a
        // remembered one, in script order (STATE-MACHINE.md §13), then the effects waiting to begin their first line
        // (an effect with no wait for, the lines above a first wait for), so two that share an output take turns;
        // then the effect itself goes on at its top
        for (int kind = 0; kind < 3; kind++)
          for (int j = 0; j < p_->effects; j++) {
            const int f = order_[j];
            if (kind == 2 ? (f == e || st_[f].running || atTop(f)) : (!(kind == 0 ? queued_[f] : pending_[f]) || !canStart(f, now))) continue;
            uint32_t r0 = st_[f].runs;
            steps(f);
            if (st_[f].runs != r0) started = (uint8_t)(started | (1u << f));
          }
        gateUpdate(e);
        runs = st_[e].runs;
        steps(e);
        if (st_[e].runs != runs) started = (uint8_t)(started | (1u << e));
      }
    }
}

}  // namespace pt
