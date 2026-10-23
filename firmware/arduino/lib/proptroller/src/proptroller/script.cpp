// Show scripts: the checker and the runner. The rules and every message are firmware/show-script-golden/README.md;
// test_script_golden runs its cases.
#include "script.h"
#include <string.h>
#include <algorithm>

namespace pt {

// ------------------------------------------------------------------ names

static const char* const OUT_NAMES[CHANNEL_COUNT] = {"out1", "out2", "out3", "out4", "rly1", "rly2", "cc1", "cc2"};
static const char* const OUT_UPPER[CHANNEL_COUNT] = {"OUT1", "OUT2", "OUT3", "OUT4", "RLY1", "RLY2", "CC1", "CC2"};
static const char* const IN_NAMES[INPUT_COUNT] = {"trig1", "trig2", "user1", "user2", "user3"};
static const char* const IN_UPPER[INPUT_COUNT] = {"TRIG1", "TRIG2", "USER1", "USER2", "USER3"};
static const char* const ROLE_WORDS[4] = {"shows", "pulse on every trigger", "pulse when a sound starts", "not used"};

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
                  SCRIPT_MAX_NAME == 32 && SCRIPT_MAX_TRACK == 99 && DMX_CHANNELS == 512,
              "the message texts quote these limits");
enum Msg : uint8_t {
  M_OUTSIDE, M_COLON, M_NONAME, M_LONGNAME, M_DUPNAME, M_UNKNOWN, M_WAITFORM, M_FADEFORM, M_FLICKERFORM, M_NOSOUND,
  M_INPUT, M_LEARN, M_OUTPUT, M_DMXCH, M_LEVEL, M_ONOFF, M_NOFADE, M_NOFLICKER, M_TIME, M_TOOLONG, M_SOUND,
  M_SOUNDRANGE, M_DMXOFF, M_ROLE, M_OWNED, M_SHARED, M_EMPTY, M_FIRST, M_EFFECTLINES, M_EFFECTS, M_LINES, M_BYTES,
  W_STILLON, W_TWOSOUND, M_RELAYFAST, M_HINTSET, M_HINTPLAY, M_HINTTURN, M_HINTSLEEP, M_HINTWAIT, M_PIXEL,
  M_TWONAMES, M_BADNAME, W_NAMEIS
};
static const char* const MSG[] = {
    "this line is not inside an effect. Start one first with \"effect <name>:\".",
    "put a colon after the effect name: \"effect {1}:\".",
    "give the effect a name, for example \"effect fog:\".",
    "the effect name is longer than 32 characters.",
    "there is already an effect called \"{1}\" (line {k}). Give each effect its own name.",
    "I do not understand \"{1}\". A line is effect, wait for, an output = on/off/N%, sleep, fade, flicker, audio_single, audio_loop or audio_stop.",
    "write it as \"wait for trig1\".",
    "write a fade as \"fade out1 to 50% over 2\".",
    "write a flicker as \"flicker out1 for 3\".",
    "say which sound, for example \"audio_single = 1\" (plays /01/001.mp3).",
    "there is no {1} on this board. The inputs are trig1, trig2 and user1 to user3.",
    "{1} is a learn-mode button (buttons = learn). Set buttons to triggers to use it in a script.",
    "there is no {1} on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.",
    "there is no DMX channel {1}. DMX channels are 1 to 512.",
    "\"{1}\" is not a level. Use on, off or a percentage from 0% to 100%.",
    "{1} can only be on or off, not {2}. Dimming works on out1 to out4.",
    "{1} cannot fade. Fading works on out1 to out4.",
    "{1} cannot flicker. Flickering works on out1 to out4.",
    "\"{1}\" is not a time. Write seconds (sleep 2.5) or add a unit: ms, s or min.",
    "that is longer than 24 hours.",
    "there is no sound called \"{1}\". Use its number: \"audio_single = 1\" plays /01/001.mp3 (the desktop app copies named sounds to the card and lists them).",
    "sound {k} is out of range. Sounds are numbered 1 to 99.",
    "{1} needs the DMX add-on, and it is not turned on. Turn it on with the setting dmx_port = s4.",
    "{1} is set to {2} ({3}_role). Set {3}_role to shows to use it in a script.",
    "{1} belongs to the built-in pulse-and-recover effect. Turn the effect off (setting effect = none) or use another terminal.",
    "{1} is already used by effect \"{2}\" (line {k}). Each {3} belongs to one effect.",
    "effect \"{1}\" has no lines.",
    "effect \"{1}\" must start with \"wait for\", for example \"wait for trig1\".",
    "effect \"{1}\" has {k} lines. An effect can have at most 64.",
    "the script has {k} effects. A board runs at most 8.",
    "the script has {k} lines. A board takes at most 256.",
    "the script is {k} bytes. A board takes at most 8192.",
    "effect \"{1}\" ends with {2} still on. It may stay on until the next trigger.",
    "effects \"{1}\" and \"{2}\" both play sound. There is one sound player: a new sound replaces the one playing.",
    "{1} switches again after {k} ms. Relays and contacts switch at most every 100 ms: sleep at least 0.1 between its changes.",
    "write it as \"{1} = {2}\".",
    "to play a sound, write \"audio_single = {1}\".",
    "write it as three lines: \"out1 = on\", \"sleep 2\", \"out1 = off\".",
    "to wait a time, write \"sleep {1}\".",
    "to wait a time, write \"sleep {1}\"; to wait for an input, write \"wait for trig1\".",
    "the pixel strip is not in scripts yet: make a show for it on the Shows tab, or use a sketch.",
    "\"{1}\" is the name of both {2} and {3}. Write {2} or {3}, or give them different names.",
    "\"{1}\" is the name of {2}, but a script cannot use it: {3}. Write {2}, or give it another name.",
    "{1} is also the name given to {2}. Here {1} means the terminal {1}.",
};
static_assert(sizeof MSG / sizeof MSG[0] == W_NAMEIS + 1, "one text per message");

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
  uint64_t scale = 0;
  if (unit.empty() || unit == "s" || unit == "sec" || unit == "secs" || unit == "second" || unit == "seconds") scale = 1000;
  else if (unit == "ms") scale = 1;
  else if (unit == "min" || unit == "mins" || unit == "minute" || unit == "minutes") scale = 60000;
  else return 0;
  uint64_t v = roundDiv(num * scale, den);
  if (v > SCRIPT_MAX_TIME_MS) return -1;
  ms = (uint32_t)v;
  return 1;
}

// Level: on, off or N% (0-100); for an output permille, for DMX 0-255.
static bool parseLevel(const std::string& s0, bool dmx, uint16_t& out) {
  std::string s = trim(s0);
  if (s == "on") { out = dmx ? DMX_FULL : FULL; return true; }
  if (s == "off") { out = 0; return true; }
  if (s.empty() || s.back() != '%') return false;
  uint64_t num = 0, den = 0;
  if (!decimal(trim(s.substr(0, s.size() - 1)), num, den)) return false;
  if (num > 100 * den) return false;
  out = (uint16_t)(dmx ? roundDiv(num * DMX_FULL, den * 100) : roundDiv(num * 10, den));
  return true;
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

// A word the line reader takes as the start of another line before it can be an output (`sleep = on` is a sleep).
static bool takenFirst(const std::string& w) {
  return startsWith(w, "wait") || (startsWith(w, "sleep") && (w.size() == 5 || isDigit(w[5]))) || w == "fade" ||
         w == "flicker";
}

// Why a given name (lower case, runs of spaces made one) cannot stand for its terminal in a script; null when
// it can.
static const char* nameProblem(const std::string& w) {
  static const char* const WORDS[] = {"effect", "audio_single", "audio_loop", "audio_stop", "on", "off", "to", "over", "for",
                                      "turn", "play"};
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
  bool first_is_wait = false;
  bool plays_sound = false;
  std::vector<ScriptStep> steps;
  std::vector<std::pair<uint16_t, uint16_t>> last;  // (target, level after its last step), first appearance order
};
struct Claim {
  uint16_t target;
  int effect;
  uint16_t line;
};
struct Named {
  std::string word;     // lower case, runs of spaces made one
  int terminal;         // 0-7 outputs, 8-12 inputs
  const char* problem;  // why a script cannot use it, or null
};
}  // namespace

ScriptCheck scriptCompile(const std::string& text, const SoundList& sounds, const ScriptBoard& board, ScriptProgram& out) {
  ScriptCheck r;
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
  std::vector<Claim> claims;  // the first good line that set each target
  auto claimOf = [&](uint16_t t) -> const Claim* {
    for (auto& c : claims)
      if (c.target == t) return &c;
    return nullptr;
  };

  for (size_t li = 0; li < lines.size(); li++) {
    uint16_t n = (uint16_t)(li + 1);
    std::string raw = clean.substr(lines[li].first, lines[li].second);
    size_t hash = raw.find('#');
    if (hash != std::string::npos) raw = raw.substr(0, hash);
    raw = trim(raw);
    if (!raw.empty() && raw.back() == '.') raw = trim(raw.substr(0, raw.size() - 1));
    if (raw.empty()) continue;
    std::string l = lower(raw);
    auto err = [&](Msg id, const char* x = "", const char* y = "", const char* z = "", size_t k = 0) {
      r.errors.push_back({n, msg(id, n, x, y, z, k)});
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
    bool first = f.lines == 1;

    ScriptStep st;
    st.line = n;
    bool is_wait = false, good = false;
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
      const Claim* c = claimOf(t);
      if (c && c->effect != cur) { err(M_SHARED, x.c_str(), fx[c->effect].name.c_str(), scriptIsDmx(t) ? "DMX channel" : "output", c->line); return false; }
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
        if (w == "pixel" || w == "pixels") err(M_PIXEL);  // not in scripts yet, though the board has a strip
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
    } else if (startsWith(l, "wait")) {
      is_wait = true;
      std::string rest = trim(l.substr(4));
      std::string in = startsWith(rest, "for") ? trim(rest.substr(3)) : std::string();
      int k = -1, found = 0;
      for (int i = 0; i < INPUT_COUNT; i++)
        if (in == IN_NAMES[i]) k = i;
      if (k >= 0) shadowed(CHANNEL_COUNT + k);
      else if (startsWith(rest, "for") && !in.empty()) {
        int nk = 0;
        found = byName(in, CHANNEL_COUNT, SCRIPT_NAMED, nk);
        if (found > 0) k = nk - CHANNEL_COUNT;
      }
      std::string x = k < 0 || via.empty() ? in : via + " (" + IN_NAMES[k] + ")";
      if (!rest.empty() && isDigit(rest[0])) err(M_HINTWAIT, trim(raw.substr(raw.size() - rest.size())).c_str());
      else if (!startsWith(rest, "for") || in.empty()) err(M_WAITFORM);
      else if (found < 0) {
        // the message is given
      } else if (k < 0) err(M_INPUT, in.c_str());
      else if (k >= 2 && !board.buttons_are_triggers) err(M_LEARN, x.c_str());
      else if (board.effect_inputs & (1u << k)) err(M_OWNED, x.c_str());
      else { st.op = ScriptStep::Op::Wait; st.target = (uint16_t)k; good = true; }
    } else if (startsWith(l, "fade") && (l.size() == 4 || l[4] == ' ')) {
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
      std::string v_raw = trim(raw.substr(5));
      int pt = parseTime(lower(v_raw), st.ms);
      if (pt == 0) err(M_TIME, v_raw.c_str());
      else if (pt < 0) err(M_TOOLONG);
      else { st.op = ScriptStep::Op::Sleep; good = true; }
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
        if (startsWith(l, "turn ")) err(M_HINTTURN);
        else if (startsWith(l, "play ") && l.size() > 5) {
          std::string what = trim(raw.substr(5));
          if (startsWith(lower(what), "sound ")) what = trim(what.substr(6));  // play sound 1 -> audio_single = 1
          err(M_HINTPLAY, what.c_str());
        } else if ((w0 == "delay" || w0 == "pause") && !w1.empty()) err(M_HINTSLEEP, trim(raw.substr(raw.size() - w1.size())).c_str());
        else if (is_out && !w1.empty()) err(M_HINTSET, w0.c_str(), w1.c_str());
        else err(M_UNKNOWN, raw.c_str());
      } else if (key == "audio_single" || key == "audio_loop") {
        std::string file = v_raw;
        if (file.size() >= 2 && (file[0] == '"' || file[0] == '\'') && file.back() == file[0]) file = trim(file.substr(1, file.size() - 2));
        if (file.empty()) err(M_NOSOUND);
        else {
          bool found = false;
          uint64_t num = 0;
          for (auto& e : sounds)
            if (e.first == file) { found = true; num = e.second; break; }
          size_t d = leadingDigits(file);
          if (!found && d == 0) err(M_SOUND, file.c_str());
          else {
            if (!found) num = digitsValue(file.substr(0, d));
            if (num < 1 || num > SCRIPT_MAX_TRACK) err(M_SOUNDRANGE, "", "", "", (unsigned long)num);
            else {
              st.op = key == "audio_single" ? ScriptStep::Op::Single : ScriptStep::Op::Loop;
              st.value = (uint16_t)num;
              good = true;
            }
          }
        }
      } else {
        uint16_t t = 0, v = 0;
        if (target(key, t)) {
          if (!parseLevel(lower(v_raw), scriptIsDmx(t), v)) err(M_LEVEL, v_raw.c_str());
          else if (!dimmable(t) && lower(v_raw) != "on" && lower(v_raw) != "off") err(M_ONOFF, shown(t, key).c_str(), v_raw.c_str());
          else if (targetChecks(t)) { st.op = ScriptStep::Op::Set; st.target = t; st.value = v; good = true; }
        }
      }
    }
    if (first) f.first_is_wait = is_wait;
    if (!good) continue;
    if (!via.empty() && st.op != ScriptStep::Op::Wait && !scriptIsDmx(st.target) && called[st.target].empty()) called[st.target] = via;
    f.steps.push_back(st);
    if (st.op == ScriptStep::Op::Single || st.op == ScriptStep::Op::Loop) f.plays_sound = true;
    if (st.op == ScriptStep::Op::Set || st.op == ScriptStep::Op::Fade || st.op == ScriptStep::Op::Flicker) {
      if (!claimOf(st.target)) claims.push_back(Claim{st.target, cur, n});
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
    else if (!f.first_is_wait) r.errors.push_back({f.line, msg(M_FIRST, f.line, f.name.c_str())});
    else if (f.lines > SCRIPT_MAX_LINES_PER_EFFECT) r.errors.push_back({f.line, msg(M_EFFECTLINES, f.line, f.name.c_str(), "", "", f.lines)});
    // a relay or contact switched again sooner than RELAY_MIN_DWELL_MS after its last change (a wait for resets it;
    // two lines with no time between them never reach the pin)
    long since[4] = {-1, -1, -1, -1};
    uint16_t was[4] = {0, 0, 0, 0};
    for (auto& st : f.steps) {
      if (st.op == ScriptStep::Op::Wait) { for (auto& x : since) x = -1; continue; }
      if (st.op == ScriptStep::Op::Sleep || st.op == ScriptStep::Op::Fade || st.op == ScriptStep::Op::Flicker)
        for (auto& x : since) if (x >= 0) x = std::min<long>(x + (long)std::min<uint32_t>(st.ms, RELAY_MIN_DWELL_MS), RELAY_MIN_DWELL_MS);
      if (st.op != ScriptStep::Op::Set || scriptIsDmx(st.target) || st.target < 4) continue;
      int k = st.target - 4;
      if (since[k] > 0 && since[k] < (long)RELAY_MIN_DWELL_MS && st.value != was[k])
        r.errors.push_back({st.line, msg(M_RELAYFAST, st.line, full(st.target).c_str(), "", "", (unsigned long)since[k])});
      if (since[k] != 0 || st.value != was[k]) since[k] = 0;
      was[k] = st.value;
    }
    for (auto& x : f.last)
      if (x.second > 0) r.warnings.push_back({f.line, msg(W_STILLON, f.line, f.name.c_str(), full(x.first).c_str())});
    if (f.plays_sound) {
      if (sound_first < 0) sound_first = (int)e;
      else r.warnings.push_back({f.line, msg(W_TWOSOUND, f.line, fx[sound_first].name.c_str(), f.name.c_str())});
    }
  }
  byLine(r.errors);
  byLine(r.warnings);
  // the whole script, at the end
  if (fx.size() > SCRIPT_MAX_EFFECTS) r.errors.push_back({0, msg(M_EFFECTS, 0, "", "", "", fx.size())});
  if (lines.size() > SCRIPT_MAX_LINES) r.errors.push_back({0, msg(M_LINES, 0, "", "", "", lines.size())});
  if (text.size() > SCRIPT_MAX_BYTES) r.errors.push_back({0, msg(M_BYTES, 0, "", "", "", text.size())});
  if (!r.ok()) return r;

  // the table (the limits above make it fit)
  for (auto& f : fx) {
    ScriptEffect& e = out.fx[out.effects++];
    strncpy(e.name, f.name.c_str(), SCRIPT_MAX_NAME);
    e.line = f.line;
    e.first = out.steps;
    e.count = (uint16_t)f.steps.size();
    for (auto& s : f.steps) {
      out.step[out.steps++] = s;
      if (s.op == ScriptStep::Op::Wait) out.inputs |= (uint8_t)(1u << s.target);
      else if (s.op == ScriptStep::Op::Set || s.op == ScriptStep::Op::Fade || s.op == ScriptStep::Op::Flicker) {
        if (scriptIsDmx(s.target)) out.dmx = true;
        else out.channels |= (uint8_t)(1u << s.target);
      }
    }
  }
  return r;
}

// The step table as JSON, the one place it is written (README "The step table": keys in this order).
void scriptTableJson(const ScriptProgram& p, JsonObject o) {
  static const char* const OP[] = {"wait_for", "set", "sleep", "fade", "flicker", "audio_single", "audio_loop", "audio_stop"};
  // what each op writes after "op": T target, L level, M ms, S sound, I input
  static const char* const KEYS[] = {"I", "TL", "M", "TLM", "TM", "S", "S", ""};
  PT_ONE_PER(OP, ScriptStep::Op::Stop);
  PT_ONE_PER(KEYS, ScriptStep::Op::Stop);
  JsonArray effects = o["effects"].to<JsonArray>();
  for (int e = 0; e < p.effects; e++) {
    const ScriptEffect& f = p.fx[e];
    JsonObject fo = effects.add<JsonObject>();
    fo["name"] = std::string(f.name);  // copied: the table may go before the JSON
    fo["line"] = f.line;
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
        else if (*k == 'M') so["ms"] = s.ms;
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
  rng_ = FLICKER_SEED;
  if (!p_) return;
  for (int e = 0; e < p_->effects; e++) enter(e, now);
}

uint16_t ScriptRunner::line(int e) const {
  if (!p_ || e >= p_->effects) return 0;
  const ScriptEffect& f = p_->fx[e];
  return f.count ? p_->step[f.first + st_[e].pc].line : f.line;
}

uint32_t ScriptRunner::rnd() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x;
  return x;
}

uint16_t ScriptRunner::get(uint16_t t) const { return scriptIsDmx(t) ? dmx_[t - SCRIPT_DMX] : level_[t]; }
void ScriptRunner::put(uint16_t t, uint16_t v) {
  if (scriptIsDmx(t)) dmx_[t - SCRIPT_DMX] = (uint8_t)v;
  else level_[t] = v;
}

void ScriptRunner::enter(int e, Millis now) {
  State& s = st_[e];
  const ScriptStep& st = p_->step[p_->fx[e].first + s.pc];
  s.entered = true;
  s.since = now;
  switch (st.op) {
    case ScriptStep::Op::Wait: s.released = false; break;
    case ScriptStep::Op::Sleep: s.until = now + st.ms; break;
    case ScriptStep::Op::Fade: s.from = get(st.target); s.until = now + st.ms; break;
    case ScriptStep::Op::Flicker: s.before = get(st.target); s.until = now + st.ms; s.next_flicker = now; break;
    case ScriptStep::Op::Loop: case ScriptStep::Op::Set: case ScriptStep::Op::Single: case ScriptStep::Op::Stop:
    default: break;
  }
}

void ScriptRunner::advance(int e, Millis now) {
  State& s = st_[e];
  s.pc++;
  if (s.pc >= p_->fx[e].count) { s.pc = 0; s.running = false; }
  enter(e, now);
}

void ScriptRunner::sampleWait(int e, Millis now, const ScriptInputs& in) {
  State& s = st_[e];
  const ScriptStep& st = p_->step[p_->fx[e].first + s.pc];
  if (st.op != ScriptStep::Op::Wait || s.released) return;
  int k = st.target;
  if (in.closed[k]) return;
  Millis from = in.since[k] > s.since ? in.since[k] : s.since;
  if (now >= from && now - from >= release_ms_) s.released = true;
}

void ScriptRunner::sample(Millis now, const ScriptInputs& in) {
  if (!p_) return;
  for (int e = 0; e < p_->effects; e++) sampleWait(e, now, in);
}

bool ScriptRunner::stepOnce(int e, Millis now, const ScriptInputs& in, std::vector<SoundCommand>& sound) {
  State& s = st_[e];
  const ScriptStep& st = p_->step[p_->fx[e].first + s.pc];
  switch (st.op) {
    case ScriptStep::Op::Wait: {
      sampleWait(e, now, in);
      int k = st.target;
      if (!s.released || !in.closed[k] || now < boot_end_) return false;
      Millis from = in.since[k] > boot_end_ ? in.since[k] : boot_end_;
      if (now - from < debounce_ms_) return false;
      if (s.pc == 0) { s.running = true; s.runs++; }
      advance(e, now);
      return true;
    }
    case ScriptStep::Op::Set: put(st.target, st.value); break;
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
    case ScriptStep::Op::Single: sound.push_back(SoundCommand::playFolder((uint8_t)st.value, 1)); break;
    case ScriptStep::Op::Loop: sound.push_back(SoundCommand::loopFolder((uint8_t)st.value)); break;
    case ScriptStep::Op::Stop: sound.push_back(SoundCommand::simple(SoundCommand::Kind::Stop)); break;
  }
  bool back_to_wait = s.pc + 1 >= p_->fx[e].count;
  advance(e, now);
  return !back_to_wait;  // back at its wait for: that is tested on the next tick
}

void ScriptRunner::tick(Millis now, const ScriptInputs& in, std::vector<SoundCommand>& sound) {
  if (!p_) return;
  for (int e = 0; e < p_->effects; e++) {
    // each step runs at most once per tick
    for (uint16_t k = 0; k <= p_->fx[e].count; k++)
      if (!stepOnce(e, now, in, sound)) break;
  }
}

}  // namespace pt
