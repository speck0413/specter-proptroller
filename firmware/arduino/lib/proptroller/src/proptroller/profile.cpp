// The profile's alias table (STATE-MACHINE.md §11, SM-AL0-7): parsing, checking, storing. The wire format is the
// reference's example table: {"topic_prefix", "aliases": [{"alias", one of "key" |
// "control" | "status" | "terminal" | "fixed", "label", "unit", "invert", "scale", "also"}]}.
#include <ctype.h>
#include <string.h>
#include "settings.h"

namespace pt {

const char* const EFFECT_CONTROLS[4] = {"trigger", "recover", "cancel", "restart"};
const char* const EFFECT_STATUS[9] = {"run_count", "runs_left", "cooldown_remaining_ms", "pulsing", "recovering", "ready",
                                      "remote_trigger_enable", "remote_trigger_by", "queued"};
static_assert(ALIAS_ALSO_MAX == 4 && ALIAS_FIXED_MAX_LEN == 32, "the messages below quote these limits");
const char* const RUN_STATUS[3] = {"runs_since_power_on", "runs_lifetime", "last_run_uptime_ms"};
// every board terminal name (terminal aliases may not equal one, in any case)
static const char* const TERMINAL_NAMES[] = {"OUT1", "OUT2", "OUT3", "OUT4", "RLY1", "RLY2", "CC1", "CC2", "TRIG1", "TRIG2",
                                             "USER1", "USER2", "USER3", "SOUND", "PIXEL", "S3"};
static const char* const KIND_NAMES[] = {"key", "control", "status", "terminal", "fixed"};
PT_ONE_PER(KIND_NAMES, AliasEntry::Kind::Fixed);

static std::string upper(std::string s) {
  for (auto& c : s) c = (char)toupper((unsigned char)c);
  return s;
}

static bool isControl(const std::string& s) {
  for (auto c : EFFECT_CONTROLS)
    if (s == c) return true;
  return false;
}
static bool isStatus(const std::string& s) {
  for (auto c : EFFECT_STATUS)
    if (s == c) return true;
  for (auto c : RUN_STATUS)
    if (s == c) return true;
  return false;
}
static bool isTerminal(const std::string& s) {
  for (auto t : TERMINAL_NAMES)
    if (upper(s) == t) return true;
  return false;
}

void Profile::toJson(JsonObject o) const {
  if (!topic_prefix.empty()) o["topic_prefix"] = topic_prefix;
  JsonArray a = o["aliases"].to<JsonArray>();
  for (auto& e : aliases) {
    JsonObject x = a.add<JsonObject>();
    x["alias"] = e.alias;
    x[KIND_NAMES[idx(e.kind)]] = e.target;
    if (!e.label.empty()) x["label"] = e.label;
    if (!e.unit.empty()) x["unit"] = e.unit;
    if (!e.group.empty()) x["group"] = e.group;
    if (e.invert) x["invert"] = true;
    if (e.scale) x["scale"] = e.scale;
    if (!e.also.empty()) {
      JsonArray al = x["also"].to<JsonArray>();
      for (auto& k : e.also) al.add(k);
    }
  }
}

bool parseProfile(JsonVariantConst v, Profile& out, std::string& why) {
  out = Profile();
  if (!v.is<JsonObjectConst>()) { why = "a profile is a JSON object with topic_prefix and aliases"; return false; }
  if (!v["topic_prefix"].isNull()) {
    if (!v["topic_prefix"].is<const char*>()) { why = "topic_prefix must be text"; return false; }
    std::string p = v["topic_prefix"].as<const char*>();
    if (p.empty() || p.size() > TOPIC_PREFIX_MAX_LEN || p.find_first_of("+#/ ") != std::string::npos) {
      why = "topic_prefix needs 1 to 32 characters, without + # / or spaces";
      return false;
    }
    out.topic_prefix = p;
  }
  JsonVariantConst list = v["aliases"];
  if (list.isNull()) return true;
  if (!list.is<JsonArrayConst>()) { why = "aliases must be a list"; return false; }
  if (list.size() > PROFILE_MAX_ALIASES) { why = "at most 64 aliases"; return false; }
  for (JsonVariantConst x : list.as<JsonArrayConst>()) {
    AliasEntry e;
    if (!x["alias"].is<const char*>()) { why = "every entry needs an \"alias\""; return false; }
    e.alias = x["alias"].as<const char*>();
    int kinds = 0;
    for (int k = 0; k < (int)(sizeof KIND_NAMES / sizeof KIND_NAMES[0]); k++)
      if (x[KIND_NAMES[k]].is<const char*>()) { e.kind = (AliasEntry::Kind)k; e.target = x[KIND_NAMES[k]].as<const char*>(); kinds++; }
    const std::string& a = e.alias;
    if (kinds != 1) { why = "alias " + a + ": give exactly one of key, control, status, terminal or fixed"; return false; }
    if (x["label"].is<const char*>()) e.label = x["label"].as<const char*>();
    if (x["unit"].is<const char*>()) e.unit = x["unit"].as<const char*>();
    if (x["group"].is<const char*>()) e.group = x["group"].as<const char*>();
    if (e.label.size() > ALIAS_LABEL_MAX_LEN || e.unit.size() > ALIAS_UNIT_MAX_LEN || e.group.size() > ALIAS_LABEL_MAX_LEN) {
      why = "alias " + a + ": label and group up to 32, unit up to 8 characters";
      return false;
    }
    e.invert = x["invert"] | false;
    if (!x["scale"].isNull()) {
      if (!x["scale"].is<int64_t>() || x["scale"].as<int64_t>() < 1) { why = "alias " + a + ": scale must be a whole number of at least 1"; return false; }
      e.scale = x["scale"].as<int64_t>();
    }
    if (!x["also"].isNull()) {
      if (e.kind != AliasEntry::Kind::Key || !x["also"].is<JsonArrayConst>() || x["also"].size() < 1 || x["also"].size() > ALIAS_ALSO_MAX) {
        why = "alias " + a + ": also is a list of 1 to 4 more settings, for a key alias";
        return false;
      }
      for (JsonVariantConst k : x["also"].as<JsonArrayConst>()) {
        if (!k.is<const char*>()) { why = "alias " + a + ": also lists setting names"; return false; }
        e.also.push_back(k.as<const char*>());
      }
    }
    if (e.kind == AliasEntry::Kind::Terminal) {
      // terminal names: 1-16 characters A-Z 0-9 _, compared without case (SM-AL0)
      bool ok = !a.empty() && a.size() <= TERMINAL_ALIAS_MAX_LEN;
      for (char c : a) ok &= (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
      if (!ok) { why = "terminal alias " + a + ": 1 to 16 characters A-Z, 0-9 and _"; return false; }
      if (!isTerminal(e.target)) { why = "terminal alias " + a + ": there is no terminal " + e.target; return false; }
      e.target = upper(e.target);
      if (isTerminal(a)) { why = "alias \"" + a + "\": that is the name of a terminal"; return false; }
      for (auto& o : out.aliases)
        if (o.kind == AliasEntry::Kind::Terminal && upper(o.alias) == upper(a)) { why = "alias \"" + a + "\": used twice"; return false; }
      if (e.invert || e.scale) { why = "terminal alias " + a + ": invert and scale are for keys"; return false; }
      out.aliases.push_back(e);
      continue;
    }
    // key, control and status aliases: exact names (SM-AL4)
    bool ok = !a.empty() && a.size() <= ALIAS_MAX_LEN && a != "broadcast" && a.find_first_of("+#/ ") == std::string::npos;
    if (!ok) { why = "alias " + a + ": 1 to 32 characters, without + # / or spaces, and not \"broadcast\""; return false; }
    if (Settings::find(a) || isControl(a) || isStatus(a)) { why = "alias \"" + a + "\": that name is already a setting, control, status or terminal"; return false; }
    for (auto& o : out.aliases)
      if (o.kind != AliasEntry::Kind::Terminal && o.alias == a) { why = "alias \"" + a + "\": used twice"; return false; }
    if (e.kind == AliasEntry::Kind::Key) {
      const ParamDesc* d = Settings::find(e.target);
      if (!d) { why = "alias \"" + a + "\": there is no setting \"" + e.target + "\""; return false; }
      if (d->isSecret()) { why = "alias \"" + a + "\": \"" + e.target + "\" is a secret (set-only): it cannot have an alias"; return false; }
      if (e.invert && d->kind != ParamDesc::Kind::Bool) { why = "alias " + a + ": invert is for on/off settings"; return false; }
      if (e.scale && d->kind != ParamDesc::Kind::Int) { why = "alias " + a + ": scale is for whole-number settings"; return false; }
      for (auto& k : e.also) {
        const ParamDesc* o = Settings::find(k);
        if (!o) { why = "alias \"" + a + "\": there is no setting \"" + k + "\""; return false; }
        if (k == e.target || o->kind != d->kind || o->isSecret()) {
          why = "alias " + a + ": also needs other settings of the same kind as " + e.target + ", and no secret";
          return false;
        }
      }
    } else if (e.kind == AliasEntry::Kind::Fixed) {
      if (e.target.size() > ALIAS_FIXED_MAX_LEN) { why = "alias " + a + ": a fixed value has up to 32 characters"; return false; }
      if (e.invert || e.scale) { why = "alias " + a + ": invert and scale are for keys"; return false; }
    } else if (e.kind == AliasEntry::Kind::Control) {
      if (!isControl(e.target)) { why = "alias " + a + ": there is no control " + e.target + " (trigger, recover, cancel, restart)"; return false; }
      if (e.invert || e.scale) { why = "alias " + a + ": invert and scale are for keys"; return false; }
    } else {
      if (!isStatus(e.target)) { why = "alias " + a + ": there is no status value " + e.target; return false; }
      if (e.invert || e.scale) { why = "alias " + a + ": invert and scale are for keys"; return false; }
    }
    out.aliases.push_back(e);
  }
  return true;
}

}  // namespace pt
