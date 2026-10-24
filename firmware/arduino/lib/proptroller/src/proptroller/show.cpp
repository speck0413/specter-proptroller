#include "show.h"
#include "sound.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>

namespace pt {

bool ShowTrigger::operator==(const ShowTrigger& o) const {
  if (on != o.on) return false;
  switch (on) {
    case Kind::OnInput: return input == o.input && until_release == o.until_release;
    case Kind::OnRelease: return input == o.input;
    case Kind::Every: case Kind::Boot: return ms == o.ms;
    case Kind::Cue: return name == o.name;
    case Kind::After: return show == o.show;
  }
  return true;
}

bool Action::operator==(const Action& o) const {
  if (kind != o.kind) return false;
  switch (kind) {
    case Kind::On: case Kind::Off: return true;
    case Kind::SetLevel: return level == o.level;
    case Kind::Ramp: return level == o.level && ms == o.ms;
    case Kind::Pulse: return ms == o.ms;
    case Kind::Blink: return on_ms == o.on_ms && off_ms == o.off_ms && count == o.count;
    case Kind::Play: return folder == o.folder && track == o.track;
    case Kind::LoopFolder: return folder == o.folder;
    case Kind::Volume: return volume == o.volume;
    case Kind::Fill: case Kind::Flicker: return color == o.color;
    case Kind::Chase: return color == o.color && step_ms == o.step_ms && width == o.width;
    case Kind::Rainbow: return period_ms == o.period_ms;
    case Kind::Flash: return color == o.color && period_ms == o.period_ms;
    case Kind::Fade: return color == o.color && ms == o.ms;
    case Kind::Emit: return cue == o.cue;
  }
  return true;
}

static const char* const ACTION_NAMES[] = {"on", "off", "level", "ramp", "pulse", "blink", "play", "loop_folder",
                                           "volume", "fill", "chase", "rainbow", "flash", "fade", "flicker", "emit"};
PT_ONE_PER(ACTION_NAMES, Action::Kind::Emit);
const char* Action::name() const { return ACTION_NAMES[idx(kind)]; }

static Level steadyLevel(const Action& a) {
  switch (a.kind) {
    case Action::Kind::On: return FULL;
    case Action::Kind::SetLevel: return a.level < FULL ? a.level : FULL;
    case Action::Kind::Ramp: return a.level < FULL ? a.level : FULL;
    case Action::Kind::Blink: case Action::Kind::Chase: case Action::Kind::Emit: case Action::Kind::Fade: case Action::Kind::Fill: case Action::Kind::Flash: case Action::Kind::Flicker: case Action::Kind::LoopFolder: case Action::Kind::Off: case Action::Kind::Play: case Action::Kind::Pulse: case Action::Kind::Rainbow: case Action::Kind::Volume:
    default: return 0;
  }
}

uint32_t Action::duration() const {
  switch (kind) {
    case Kind::Ramp: case Kind::Pulse: case Kind::Fade: return ms;
    case Kind::Blink: {
      uint64_t d = (uint64_t)(on_ms + off_ms) * count;
      return d > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)d;
    }
    case Kind::Chase: case Kind::Emit: case Kind::Fill: case Kind::Flash: case Kind::Flicker: case Kind::LoopFolder: case Kind::Off: case Kind::On: case Kind::Play: case Kind::Rainbow: case Kind::SetLevel: case Kind::Volume:
    default: return 0;
  }
}

bool Action::effect(Effect& e) const {
  e = Effect();
  switch (kind) {
    case Kind::Off: e.kind = Effect::Kind::Off; return true;
    case Kind::Fill: e.kind = Effect::Kind::Fill; e.color = color; return true;
    case Kind::Chase: e.kind = Effect::Kind::Chase; e.color = color; e.step_ms = step_ms; e.width = width; return true;
    case Kind::Rainbow: e.kind = Effect::Kind::Rainbow; e.period_ms = period_ms; return true;
    case Kind::Flash: e.kind = Effect::Kind::Flash; e.color = color; e.period_ms = period_ms; return true;
    case Kind::Fade: e.kind = Effect::Kind::Fade; e.color = color; e.ms = ms; return true;
    case Kind::Flicker: e.kind = Effect::Kind::Flicker; e.color = color; return true;
    case Kind::Blink: case Kind::Emit: case Kind::LoopFolder: case Kind::On: case Kind::Play: case Kind::Pulse: case Kind::Ramp: case Kind::SetLevel: case Kind::Volume:
    default: return false;
  }
}

bool Action::sound(SoundCommand& c) const {
  switch (kind) {
    case Kind::Play: c = folder == 0 ? soundPlay(track) : SoundCommand::playFolder(folder, (uint8_t)track); return true;
    case Kind::LoopFolder: c = SoundCommand::loopFolder(folder); return true;
    case Kind::Volume: c = SoundCommand::volume(volume); return true;
    case Kind::Off: c = SoundCommand::simple(SoundCommand::Kind::Stop); return true;
    case Kind::Blink: case Kind::Chase: case Kind::Emit: case Kind::Fade: case Kind::Fill: case Kind::Flash: case Kind::Flicker: case Kind::On: case Kind::Pulse: case Kind::Rainbow: case Kind::Ramp: case Kind::SetLevel:
    default: return false;
  }
}

void Show::normalize() {
  for (auto& t : tracks) std::stable_sort(t.cues.begin(), t.cues.end(), [](const Cue& a, const Cue& b) { return a.at < b.at; });
}

uint32_t Show::naturalLength() const {
  uint32_t m = 0;
  for (auto& t : tracks)
    for (auto& c : t.cues) {
      uint64_t e = (uint64_t)c.at + c.action.duration();
      uint32_t e32 = e > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)e;
      if (e32 > m) m = e32;
    }
  return m;
}

bool Show::untilRelease() const {
  for (auto& t : triggers)
    if (t.until_release) return true;
  return false;
}

uint32_t Show::length() const {
  if (has_length) return length_ms;
  uint32_t n = naturalLength();
  return untilRelease() && n < UNTIL_RELEASE_MAX_MS ? UNTIL_RELEASE_MAX_MS : n;
}

const Track* Show::track(Target t) const {
  for (auto& x : tracks)
    if (x.target == t) return &x;
  return nullptr;
}

Track& Show::trackMut(Target t) {
  for (auto& x : tracks)
    if (x.target == t) return x;
  Track n;
  n.target = t;
  tracks.push_back(n);
  return tracks.back();
}

Compiled::Compiled(const Track& track) {
  std::vector<Cue> sorted = track.cues;
  std::stable_sort(sorted.begin(), sorted.end(), [](const Cue& a, const Cue& b) { return a.at < b.at; });
  cues.reserve(sorted.size());
  start.reserve(sorted.size());
  for (auto& c : sorted) {
    Level s = 0;
    if (!levelAt(c.at, s)) s = 0;
    cues.push_back(c);
    start.push_back(s);
  }
}

bool Compiled::levelAt(uint64_t t, Level& out) const {
  // partition point: first cue with at > t
  size_t lo = 0, hi = cues.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if ((uint64_t)cues[mid].at <= t) lo = mid + 1;
    else hi = mid;
  }
  if (lo == 0) return false;
  const Cue& cue = cues[lo - 1];
  uint64_t dt = t - cue.at;
  const Action& a = cue.action;
  switch (a.kind) {
    case Action::Kind::On: out = FULL; break;
    case Action::Kind::Off: out = 0; break;
    case Action::Kind::SetLevel: out = a.level < FULL ? a.level : FULL; break;
    case Action::Kind::Ramp: {
      int64_t from = start[lo - 1];
      int64_t to = a.level < FULL ? a.level : FULL;
      out = (a.ms == 0 || dt >= a.ms) ? (Level)to : (Level)(from + (to - from) * (int64_t)dt / (int64_t)a.ms);
      break;
    }
    case Action::Kind::Pulse: out = dt < a.ms ? FULL : 0; break;
    case Action::Kind::Blink: {
      uint64_t per = (uint64_t)a.on_ms + a.off_ms;
      if (per < 1) per = 1;
      if (a.count > 0 && dt >= per * a.count) out = 0;
      else out = dt % per < a.on_ms ? FULL : 0;
      break;
    }
    case Action::Kind::Chase: case Action::Kind::Emit: case Action::Kind::Fade: case Action::Kind::Fill: case Action::Kind::Flash: case Action::Kind::Flicker: case Action::Kind::LoopFolder: case Action::Kind::Play: case Action::Kind::Rainbow: case Action::Kind::Volume:
    default: out = steadyLevel(a);
  }
  return true;
}

static const char* const PROBLEM_NAMES[] = {"", "bad_id", "bad_name", "too_big", "duplicate_target", "too_long",
                                            "wrong_target", "out_of_range", "too_fast", "bad_cue_name"};
static const char* const PROBLEM_DEBUG[] = {"", "BadId", "BadName", "TooBig", "DuplicateTarget", "TooLong",
                                            "WrongTarget", "OutOfRange", "TooFast", "BadCueName"};
static const char* const PROBLEM_HELP[] = {
    "",
    "The show id must be 1 or more.",
    "Give the show a name of 1 to 40 characters.",
    "Too many shows, tracks or cues: at most 16 shows, 16 tracks per show and 1000 cues per track.",
    "Two tracks drive the same output: merge them into one track.",
    "The show (or a cue) is longer than one hour: shorten it.",
    "That action does not fit that output (relays and contacts are only on/off, sound and pixels have their own actions).",
    "A value is out of range (levels 0-1000, volume 0-30, folder 1-99, track 1-255).",
    "Too fast: a looping show or a timer must be at least 100 ms long.",
    "A cue name must be 1 to 24 characters."};
const char* problemName(Problem p) { return PROBLEM_NAMES[(int)p]; }
const char* problemDebug(Problem p) { return PROBLEM_DEBUG[(int)p]; }
const char* problemHelp(Problem p) { return PROBLEM_HELP[(int)p]; }

std::string ShowError::message() const {
  std::string m = problemDebug(problem);
  char b[32];
  if (track >= 0) { (void)snprintf(b, sizeof b, " in track %d", track); m += b; }
  if (cue >= 0) { (void)snprintf(b, sizeof b, " at cue %d", cue); m += b; }
  return m;
}

static ShowError err(Problem p) {
  ShowError e;
  e.problem = p;
  e.track = -1;
  e.cue = -1;
  return e;
}
// track and cue indexes are below MAX_TRACKS / MAX_CUES (checked before they are named), so they fit an int
static ShowError err(Problem p, size_t t, size_t c = SIZE_MAX) {
  ShowError e = err(p);
  e.track = (int)t;
  e.cue = c == SIZE_MAX ? -1 : (int)c;
  return e;
}

static size_t utf8Chars(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) n++;
  return n;
}

static bool cueNameOk(const std::string& n) { return !n.empty() && n.size() <= MAX_CUE_NAME; }

ShowError validate(const Show& show) {
  if (show.id == 0) return err(Problem::BadId);
  if (show.name.empty() || utf8Chars(show.name) > MAX_NAME) return err(Problem::BadName);
  if (show.tracks.size() > MAX_TRACKS) return err(Problem::TooBig);
  for (auto& tr : show.triggers) {
    if (tr.on == ShowTrigger::Kind::Every && tr.ms < 100) return err(Problem::TooFast);
    if (tr.on == ShowTrigger::Kind::Cue && !cueNameOk(tr.name)) return err(Problem::BadCueName);
  }
  if (show.length() > MAX_LENGTH_MS) return err(Problem::TooLong);
  if (show.looping && show.length() < MIN_LOOP_MS) return err(Problem::TooFast);
  if (show.runs_per_cooldown < 1 || show.runs_per_cooldown > MAX_RUNS_PER_COOLDOWN) return err(Problem::OutOfRange);
  for (size_t ti = 0; ti < show.tracks.size(); ti++) {
    const Track& t = show.tracks[ti];
    for (size_t o = 0; o < ti; o++)
      if (show.tracks[o].target == t.target) return err(Problem::DuplicateTarget, ti);
    if (t.cues.size() > MAX_CUES) return err(Problem::TooBig, ti);
    for (size_t ci = 0; ci < t.cues.size(); ci++) {
      const Cue& c = t.cues[ci];
      const Action& a = c.action;
      if (c.at > MAX_LENGTH_MS) return err(Problem::TooLong, ti, ci);
      switch (t.target.kind) {
        case Target::Kind::CHANNEL: {
          Channel ch = t.target.channel;
          bool ok = a.kind == Action::Kind::On || a.kind == Action::Kind::Off || a.kind == Action::Kind::Pulse || a.kind == Action::Kind::Blink ||
                    a.kind == Action::Kind::SetLevel || a.kind == Action::Kind::Ramp;
          if (!ok) return err(Problem::WrongTarget, ti, ci);
          if ((a.kind == Action::Kind::SetLevel || a.kind == Action::Kind::Ramp) && a.level > FULL) return err(Problem::OutOfRange, ti, ci);
          bool partial = a.kind == Action::Kind::SetLevel && a.level > 0 && a.level < FULL;
          bool ramp = a.kind == Action::Kind::Ramp;
          if ((partial || ramp) && !isPwm(ch)) return err(Problem::WrongTarget, ti, ci);
          if (a.kind == Action::Kind::Blink && !isPwm(ch) && (a.on_ms < RELAY_MIN_DWELL_MS || a.off_ms < RELAY_MIN_DWELL_MS))
            return err(Problem::TooFast, ti, ci);
          break;
        }
        case Target::Kind::SOUND: {
          bool ok = false;
          switch (a.kind) {
            case Action::Kind::Play: ok = a.folder == 0 ? a.track >= 1 && a.track <= SOUND_MAX_TRACK : a.folder <= 99 && a.track >= 1 && a.track <= 255; break;
            case Action::Kind::LoopFolder: ok = a.folder >= 1 && a.folder <= 99; break;
            case Action::Kind::Volume: ok = a.volume <= 30; break;
            case Action::Kind::Off: ok = true; break;
            case Action::Kind::Blink: case Action::Kind::Chase: case Action::Kind::Emit: case Action::Kind::Fade: case Action::Kind::Fill: case Action::Kind::Flash: case Action::Kind::Flicker: case Action::Kind::On: case Action::Kind::Pulse: case Action::Kind::Rainbow: case Action::Kind::Ramp: case Action::Kind::SetLevel:
            default: return err(Problem::WrongTarget, ti, ci);
          }
          if (!ok) return err(Problem::OutOfRange, ti, ci);
          break;
        }
        case Target::Kind::PIXEL: {
          Effect e;
          if (!a.effect(e)) return err(Problem::WrongTarget, ti, ci);
          break;
        }
        case Target::Kind::NET:
          if (a.kind != Action::Kind::Emit) return err(Problem::WrongTarget, ti, ci);
          if (!cueNameOk(a.cue)) return err(Problem::BadCueName, ti, ci);
          break;
      }
    }
  }
  return ShowError();
}

// ------------------------------------------------------------------ JSON

bool readU64(JsonVariantConst o, const char* key, uint64_t max, uint64_t& out, bool required, std::string& err) {
  JsonVariantConst v = o[key];
  if (v.isNull()) {
    if (required) { err = std::string("missing field `") + key + "`"; return false; }
    return true;
  }
  if (!v.is<uint64_t>() || v.as<uint64_t>() > max) { err = std::string("invalid value for `") + key + "`"; return false; }
  out = v.as<uint64_t>();
  return true;
}

bool readBool(JsonVariantConst o, const char* key, bool& out, bool required, std::string& err) {
  JsonVariantConst v = o[key];
  if (v.isNull()) {
    if (required) { err = std::string("missing field `") + key + "`"; return false; }
    return true;
  }
  if (!v.is<bool>()) { err = std::string("invalid type for `") + key + "`, expected a boolean"; return false; }
  out = v.as<bool>();
  return true;
}

bool readStr(JsonVariantConst o, const char* key, std::string& out, bool required, std::string& err) {
  JsonVariantConst v = o[key];
  if (v.isNull()) {
    if (required) { err = std::string("missing field `") + key + "`"; return false; }
    return true;
  }
  if (!v.is<const char*>()) { err = std::string("invalid type for `") + key + "`, expected a string"; return false; }
  out = v.as<const char*>();
  return true;
}

static bool readColor(JsonVariantConst o, const char* key, Rgb& out, std::string& err) {
  std::string s;
  if (!readStr(o, key, s, true, err)) return false;
  if (!parseColor(s.c_str(), out)) { err = "bad colour, expected #rrggbb"; return false; }
  return true;
}

void toJson(const Action& a, uint32_t at, JsonObject o) {
  o["at"] = at;
  o["do"] = a.name();
  switch (a.kind) {
    case Action::Kind::SetLevel: o["level"] = a.level; break;
    case Action::Kind::Ramp: o["to"] = a.level; o["ms"] = a.ms; break;
    case Action::Kind::Pulse: o["ms"] = a.ms; break;
    case Action::Kind::Blink: o["on_ms"] = a.on_ms; o["off_ms"] = a.off_ms; o["count"] = a.count; break;
    case Action::Kind::Play: o["folder"] = a.folder; o["track"] = a.track; break;
    case Action::Kind::LoopFolder: o["folder"] = a.folder; break;
    case Action::Kind::Volume: o["volume"] = a.volume; break;
    case Action::Kind::Fill: case Action::Kind::Flicker: o["color"] = colorText(a.color); break;
    case Action::Kind::Chase: o["color"] = colorText(a.color); o["step_ms"] = a.step_ms; o["width"] = a.width; break;
    case Action::Kind::Rainbow: o["period_ms"] = a.period_ms; break;
    case Action::Kind::Flash: o["color"] = colorText(a.color); o["period_ms"] = a.period_ms; break;
    case Action::Kind::Fade: o["color"] = colorText(a.color); o["ms"] = a.ms; break;
    case Action::Kind::Emit: o["cue"] = a.cue; break;
    case Action::Kind::Off: case Action::Kind::On:
    default: break;
  }
}

bool actionFromJson(JsonVariantConst v, Action& a, std::string& e) {
  if (!v.is<JsonObjectConst>()) { e = "expected an object"; return false; }
  std::string d;
  if (!readStr(v, "do", d, true, e)) return false;
  a = Action();
  int k = -1;
  for (int i = 0; i < 16; i++)
    if (d == ACTION_NAMES[i]) k = i;
  if (k < 0) { e = "unknown value `" + d + "`"; return false; }
  a.kind = (Action::Kind)k;
  switch (a.kind) {
    case Action::Kind::SetLevel: return readUint(v, "level", a.level, true, e);
    case Action::Kind::Ramp: return readUint(v, "to", a.level, true, e) && readUint(v, "ms", a.ms, true, e);
    case Action::Kind::Pulse: return readUint(v, "ms", a.ms, true, e);
    case Action::Kind::Blink:
      return readUint(v, "on_ms", a.on_ms, true, e) && readUint(v, "off_ms", a.off_ms, true, e) && readUint(v, "count", a.count, false, e);
    case Action::Kind::Play: return readUint(v, "folder", a.folder, true, e) && readUint(v, "track", a.track, true, e);
    case Action::Kind::LoopFolder: return readUint(v, "folder", a.folder, true, e);
    case Action::Kind::Volume: return readUint(v, "volume", a.volume, true, e);
    case Action::Kind::Fill: case Action::Kind::Flicker: return readColor(v, "color", a.color, e);
    case Action::Kind::Chase:
      return readColor(v, "color", a.color, e) && readUint(v, "step_ms", a.step_ms, true, e) && readUint(v, "width", a.width, false, e);
    case Action::Kind::Rainbow: return readUint(v, "period_ms", a.period_ms, true, e);
    case Action::Kind::Flash: return readColor(v, "color", a.color, e) && readUint(v, "period_ms", a.period_ms, true, e);
    case Action::Kind::Fade: return readColor(v, "color", a.color, e) && readUint(v, "ms", a.ms, true, e);
    case Action::Kind::Emit: return readStr(v, "cue", a.cue, true, e);
    case Action::Kind::Off: case Action::Kind::On:
    default: return true;
  }
}

static const char* const TRIGGER_NAMES[] = {"input", "release", "every", "boot", "cue", "after"};
PT_ONE_PER(TRIGGER_NAMES, ShowTrigger::Kind::After);

void toJson(const ShowTrigger& t, JsonObject o) {
  o["on"] = TRIGGER_NAMES[idx(t.on)];
  switch (t.on) {
    case ShowTrigger::Kind::OnInput:
      o["input"] = name(t.input);
      if (t.until_release) o["until_release"] = true;
      break;
    case ShowTrigger::Kind::OnRelease: o["input"] = name(t.input); break;
    case ShowTrigger::Kind::Every: o["period_ms"] = t.ms; break;
    case ShowTrigger::Kind::Boot: o["delay_ms"] = t.ms; break;
    case ShowTrigger::Kind::Cue: o["name"] = t.name; break;
    case ShowTrigger::Kind::After: o["show"] = t.show; break;
  }
}

bool triggerFromJson(JsonVariantConst v, ShowTrigger& t, std::string& e) {
  if (!v.is<JsonObjectConst>()) { e = "expected an object"; return false; }
  std::string on;
  if (!readStr(v, "on", on, true, e)) return false;
  t = ShowTrigger();
  int k = -1;
  for (int i = 0; i < 6; i++)
    if (on == TRIGGER_NAMES[i]) k = i;
  if (k < 0) { e = "unknown value `" + on + "`"; return false; }
  t.on = (ShowTrigger::Kind)k;
  if (!v["until_release"].isNull()) {
    if (t.on != ShowTrigger::Kind::OnInput) { e = "until_release: only an input trigger ends at the release"; return false; }
    if (!readBool(v, "until_release", t.until_release, false, e)) return false;
  }
  switch (t.on) {
    case ShowTrigger::Kind::OnInput:
    case ShowTrigger::Kind::OnRelease: {
      std::string s;
      if (!readStr(v, "input", s, true, e)) return false;
      if (!parseInput(s.c_str(), t.input) || s != name(t.input)) { e = "unknown value `" + s + "`"; return false; }
      return true;
    }
    case ShowTrigger::Kind::Every: return readUint(v, "period_ms", t.ms, true, e);
    case ShowTrigger::Kind::Boot: return readUint(v, "delay_ms", t.ms, false, e);
    case ShowTrigger::Kind::Cue: return readStr(v, "name", t.name, true, e);
    case ShowTrigger::Kind::After: return readUint(v, "show", t.show, true, e);
  }
  return true;
}

static const char* const RETRIGGER_NAMES[] = {"ignore", "restart", "stop"};

void toJson(const Show& s, JsonObject o) {
  o["id"] = s.id;
  o["name"] = s.name;
  o["enabled"] = s.enabled;
  JsonArray tr = o["triggers"].to<JsonArray>();
  for (auto& t : s.triggers) toJson(t, tr.add<JsonObject>());
  o["cooldown_ms"] = s.cooldown_ms;
  if (s.runs_per_cooldown != 1) o["runs_per_cooldown"] = s.runs_per_cooldown;
  o["retrigger"] = RETRIGGER_NAMES[(int)s.retrigger];
  o["looping"] = s.looping;
  o["priority"] = s.priority;
  if (s.has_length) o["length_ms"] = s.length_ms;
  o["stop_sound"] = s.stop_sound;
  JsonArray tracks = o["tracks"].to<JsonArray>();
  for (auto& t : s.tracks) {
    JsonObject jt = tracks.add<JsonObject>();
    jt["target"] = name(t.target);
    JsonArray cues = jt["cues"].to<JsonArray>();
    for (auto& c : t.cues) toJson(c.action, c.at, cues.add<JsonObject>());
  }
}

bool fromJson(JsonVariantConst v, Show& s, std::string& e) {
  if (!v.is<JsonObjectConst>()) { e = "expected a show object"; return false; }
  s = Show();
  if (!readUint(v, "id", s.id, true, e) || !readStr(v, "name", s.name, true, e)) return false;
  if (!readBool(v, "enabled", s.enabled, false, e) || !readUint(v, "cooldown_ms", s.cooldown_ms, false, e) ||
      !readBool(v, "looping", s.looping, false, e) || !readUint(v, "priority", s.priority, false, e) ||
      !readBool(v, "stop_sound", s.stop_sound, false, e) || !readUint(v, "runs_per_cooldown", s.runs_per_cooldown, false, e))
    return false;
  if (!v["length_ms"].isNull()) {
    if (!readUint(v, "length_ms", s.length_ms, true, e)) return false;
    s.has_length = true;
  }
  std::string rt;
  if (!readStr(v, "retrigger", rt, false, e)) return false;
  if (!rt.empty()) {
    int k = -1;
    for (int i = 0; i < 3; i++)
      if (rt == RETRIGGER_NAMES[i]) k = i;
    if (k < 0) { e = "unknown value `" + rt + "`"; return false; }
    s.retrigger = (Retrigger)k;
  }
  JsonVariantConst trs = v["triggers"];
  if (!trs.isNull()) {
    if (!trs.is<JsonArrayConst>()) { e = "triggers: expected an array"; return false; }
    for (JsonVariantConst t : trs.as<JsonArrayConst>()) {
      ShowTrigger x;
      if (!triggerFromJson(t, x, e)) return false;
      s.triggers.push_back(x);
    }
  }
  JsonVariantConst tracks = v["tracks"];
  if (!tracks.isNull()) {
    if (!tracks.is<JsonArrayConst>()) { e = "tracks: expected an array"; return false; }
    for (JsonVariantConst t : tracks.as<JsonArrayConst>()) {
      Track tr;
      std::string target;
      if (!readStr(t, "target", target, true, e)) return false;
      if (!parseTarget(target.c_str(), tr.target)) { e = "unknown target"; return false; }
      JsonVariantConst cues = t["cues"];
      if (!cues.isNull()) {
        if (!cues.is<JsonArrayConst>()) { e = "cues: expected an array"; return false; }
        for (JsonVariantConst c : cues.as<JsonArrayConst>()) {
          Cue cue;
          if (!readUint(c, "at", cue.at, true, e) || !actionFromJson(c, cue.action, e)) return false;
          tr.cues.push_back(cue);
        }
      }
      s.tracks.push_back(tr);
    }
  }
  return true;
}

std::string showToString(const Show& s) {
  JsonDocument d;
  toJson(s, d.to<JsonObject>());
  std::string out;
  serializeJson(d, out);
  return out;
}

bool showFromString(const std::string& json, Show& s, std::string& e) {
  JsonDocument d;
  if (deserializeJson(d, json)) { e = "bad JSON"; return false; }
  return fromJson(d.as<JsonVariantConst>(), s, e);
}

std::string explain(const ShowError& e, const Show& show) {
  char b[400];
  const Track* track = e.track >= 0 && (size_t)e.track < show.tracks.size() ? &show.tracks[e.track] : nullptr;
  std::string what = track ? name(track->target) : "the show";
  const Cue* cue = track && e.cue >= 0 && (size_t)e.cue < track->cues.size() ? &track->cues[e.cue] : nullptr;
  std::string when;
  if (cue) {
    (void)snprintf(b, sizeof b, " at %u.%02u s", (unsigned)(cue->at / 1000), (unsigned)((cue->at % 1000) / 10));
    when = b;
  }
  const char* w = what.c_str();
  const char* t = when.c_str();
  switch (e.problem) {
    case Problem::BadId: return "a show needs a number from 1 to 65535";
    case Problem::BadName: (void)snprintf(b, sizeof b, "a show needs a name of 1 to %u characters", (unsigned)MAX_NAME); break;
    case Problem::TooBig:
      (void)snprintf(b, sizeof b, "the show is too big: at most %u tracks and %u steps, and %u shows per board", (unsigned)MAX_TRACKS, (unsigned)MAX_CUES, (unsigned)MAX_SHOWS);
      break;
    case Problem::DuplicateTarget: (void)snprintf(b, sizeof b, "%s has two tracks; put all its steps in one", w); break;
    case Problem::TooLong: return "the show is longer than one hour; shorten it or split it into two shows";
    case Problem::WrongTarget:
      (void)snprintf(b, sizeof b, "%s cannot do that%s: dimming and fades work on OUT1-OUT4 only, sounds on SOUND, colours on PIXEL", w, t);
      break;
    case Problem::OutOfRange:
      if (!cue) (void)snprintf(b, sizeof b, "a value in the show settings is out of range: runs per cooldown 1-%u", (unsigned)MAX_RUNS_PER_COOLDOWN);
      else if (cue->action.kind == Action::Kind::Volume) (void)snprintf(b, sizeof b, "%s%s: volume %u is too loud; the player goes from 0 to 30", w, t, (unsigned)cue->action.volume);
      else if (cue->action.kind == Action::Kind::SetLevel || cue->action.kind == Action::Kind::Ramp)
        (void)snprintf(b, sizeof b, "%s%s: %u %% is more than full power; use 0 to 100 %%", w, t, (unsigned)(cue->action.level / 10));
      else if (cue->action.kind == Action::Kind::Play && cue->action.folder == 0)
        (void)snprintf(b, sizeof b, "%s%s: there is no sound %u; sounds are numbered 1 to %u", w, t, (unsigned)cue->action.track, (unsigned)SOUND_MAX_TRACK);
      else if (cue->action.kind == Action::Kind::Play)
        (void)snprintf(b, sizeof b, "%s%s: there is no /%02u/%03u.mp3 slot; folders are 01-99 and files 001-255", w, t, (unsigned)cue->action.folder, (unsigned)cue->action.track);
      else (void)snprintf(b, sizeof b, "%s%s: a value is out of range", w, t);
      break;
    case Problem::TooFast:
      if (cue) (void)snprintf(b, sizeof b, "%s%s: relays and contacts switch at most every %u ms; make the blink's on and off times at least %u ms", w, t, (unsigned)RELAY_MIN_DWELL_MS, (unsigned)RELAY_MIN_DWELL_MS);
      else (void)snprintf(b, sizeof b, "%s repeats faster than every %u ms; make the loop or the timer longer", w, (unsigned)MIN_LOOP_MS);
      break;
    case Problem::BadCueName: (void)snprintf(b, sizeof b, "a cue name needs 1 to %u characters", (unsigned)MAX_CUE_NAME); break;
    case Problem::None:
    default: return "";
  }
  return b;
}

}  // namespace pt
