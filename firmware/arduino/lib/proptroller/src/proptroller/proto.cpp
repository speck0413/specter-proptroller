#include "proto.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace pt {

bool isDiscover(const char* data, size_t len) {
  JsonDocument d;
  if (deserializeJson(d, data, len) || !d.is<JsonObject>()) return false;
  return d["proptroller"].is<const char*>() && !strcmp(d["proptroller"], "discover") && d["v"].is<uint16_t>();
}

std::string hostLabel(const std::string& name, const std::string& id) {
  std::string dashed;
  for (unsigned char c : name) dashed += isalnum(c) && c < 128 ? (char)tolower(c) : '-';
  // collapse runs of '-' and trim them
  std::string s;
  size_t i = 0;
  while (i < dashed.size()) {
    while (i < dashed.size() && dashed[i] == '-') i++;
    size_t j = i;
    while (j < dashed.size() && dashed[j] != '-') j++;
    if (j > i) {
      if (!s.empty()) s += '-';
      s += dashed.substr(i, j - i);
    }
    i = j;
  }
  if (s.size() > 40) s.resize(40);
  while (!s.empty() && s.back() == '-') s.pop_back();
  if (s.empty()) return "proptroller-" + (id.size() >= 4 ? id.substr(id.size() - 4) : id);
  return s;
}

std::string shortId(const std::string& id) {
  std::string s = id.size() >= 4 ? id.substr(id.size() - 4) : id;
  for (auto& c : s) c = (char)tolower((unsigned char)c);
  return s;
}

std::string mdnsLabel(const std::string& label, const std::string& id, bool taken) {
  if (!taken) return label;
  std::string tail = "-" + shortId(id);
  // a nameless board's label (proptroller-ac2c) already carries the id
  if (label.size() >= tail.size() && label.compare(label.size() - tail.size(), tail.size(), tail) == 0) return label;
  return label + tail;
}

// ------------------------------------------------------------------ values

bool Value::operator==(const Value& o) const {
  if (type != o.type) return false;
  if (type == Type::Bool) return b == o.b;
  if (type == Type::Int) return i == o.i;
  return s == o.s;
}

std::string Value::plain() const {
  if (type == Type::Bool) return b ? "true" : "false";
  if (type == Type::Int) {
    char buf[24];
    (void)snprintf(buf, sizeof buf, "%lld", (long long)i);
    return buf;
  }
  return s;
}

void Value::toJson(JsonObject o, const char* key) const {
  if (type == Type::Bool) o[key] = b;
  else if (type == Type::Int) o[key] = (long long)i;
  else o[key] = s;
}

bool Value::fromJson(JsonVariantConst v, Value& out) {
  if (v.is<bool>()) { out = boolean(v.as<bool>()); return true; }
  if (v.is<long long>()) { out = integer(v.as<long long>()); return true; }
  if (v.is<const char*>()) { out = text(v.as<const char*>()); return true; }
  return false;
}

ParamDesc ParamDesc::integer(const std::string& key, const std::string& label, const std::string& group, int64_t min, int64_t max, const std::string& unit) {
  ParamDesc d;
  d.key = key; d.label = label; d.group = group; d.kind = Kind::Int; d.min = min; d.max = max; d.unit = unit;
  return d;
}
ParamDesc ParamDesc::boolean(const std::string& key, const std::string& label, const std::string& group) {
  ParamDesc d;
  d.key = key; d.label = label; d.group = group; d.kind = Kind::Bool;
  return d;
}
ParamDesc ParamDesc::text(const std::string& key, const std::string& label, const std::string& group, uint16_t max_len, bool secret) {
  ParamDesc d;
  d.key = key; d.label = label; d.group = group; d.kind = Kind::Text; d.max_len = max_len; d.secret = secret;
  return d;
}
ParamDesc ParamDesc::choice(const std::string& key, const std::string& label, const std::string& group, std::initializer_list<const char*> options) {
  ParamDesc d;
  d.key = key; d.label = label; d.group = group; d.kind = Kind::Choice;
  for (auto o : options) d.options.push_back(o);
  return d;
}

const char* ParamDesc::check(const Value& v) const {
  switch (kind) {
    case Kind::Int: if (v.type != Value::Type::Int) return "wrong type"; return v.i >= min && v.i <= max ? nullptr : "out of range";
    case Kind::Bool: return v.type == Value::Type::Bool ? nullptr : "wrong type";
    case Kind::Text: if (v.type != Value::Type::Text) return "wrong type"; return v.s.size() <= max_len ? nullptr : "too long";
    case Kind::Choice:
      if (v.type != Value::Type::Text) return "wrong type";
      for (auto& o : options)
        if (o == v.s) return nullptr;
      return "not an option";
  }
  return "wrong type";
}

std::string ParamDesc::explain(const Value& v) const {
  if (!check(v)) return "";
  char b[96];
  if (kind == Kind::Int && v.type == Value::Type::Int) {
    std::string u = unit.empty() ? "" : " " + unit;
    (void)snprintf(b, sizeof b, "%lld", (long long)v.i);
    std::string val = b;
    (void)snprintf(b, sizeof b, "%lld-%lld", (long long)min, (long long)max);
    return label + " is set to " + val + u + " but the board allows " + b + u + "; " + (v.i > max ? "lower" : "raise") + " it in Settings → " + group;
  }
  if (kind == Kind::Text && v.type == Value::Type::Text) {
    (void)snprintf(b, sizeof b, " is %u characters long but at most %u fit; shorten it in Settings → ", (unsigned)v.s.size(), (unsigned)max_len);
    return label + b + group;
  }
  if (kind == Kind::Choice && v.type == Value::Type::Text) {
    std::string o;
    for (size_t i = 0; i < options.size(); i++) o += (i ? ", " : "") + options[i];
    return label + " cannot be \"" + v.s + "\"; pick one of: " + o;
  }
  if (kind == Kind::Int) return label + " needs a whole number";
  if (kind == Kind::Bool) return label + " needs true or false";
  return label + " needs text";
}

void ParamDesc::toJson(JsonObject o) const {
  o["key"] = key;
  o["label"] = label;
  o["group"] = group;
  switch (kind) {
    case Kind::Int: o["type"] = "int"; o["min"] = (long long)min; o["max"] = (long long)max; o["unit"] = unit; break;
    case Kind::Bool: o["type"] = "bool"; break;
    case Kind::Text: o["type"] = "text"; o["max_len"] = max_len; o["secret"] = secret; break;
    case Kind::Choice: {
      o["type"] = "choice";
      JsonArray a = o["options"].to<JsonArray>();
      for (auto& x : options) a.add(x);
      break;
    }
  }
  if (!help.empty()) o["help"] = help;
  o["reboot"] = reboot;
}

static std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

bool ParamDesc::parsePlain(const std::string& payload, Value& out, std::string& err) const {
  std::string s = trim(payload);
  if (kind == Kind::Bool) {
    if (s == "true") { out = Value::boolean(true); return true; }
    if (s == "false") { out = Value::boolean(false); return true; }
    err = "use true or false";
    return false;
  }
  if (kind == Kind::Int) {
    if (s.empty()) { err = "not a number"; return false; }
    size_t k = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (k == s.size()) { err = "not a number"; return false; }
    for (size_t j = k; j < s.size(); j++)
      if (!isdigit((unsigned char)s[j])) { err = "not a number"; return false; }
    out = Value::integer(strtoll(s.c_str(), nullptr, 10));
    return true;
  }
  out = Value::text(s);
  return true;
}

// ------------------------------------------------------------------ info

bool Info::has(const char* cap) const {
  for (auto& c : capabilities)
    if (c == cap) return true;
  return false;
}

const char* terminalKindName(Terminal::Kind k) {
  static const char* const n[] = {"input", "output", "sound", "pixel", "analog"};
  PT_ONE_PER(n, Terminal::Kind::AnalogT);
  return n[idx(k)];
}

void Info::toJson(JsonObject o) const {
  o["v"] = PROTOCOL_VERSION;
  o["id"] = id;
  o["name"] = name;
  o["fw"] = fw;
  o["fw_version"] = fw_version;
  o["fw_impl"] = fw_impl;
  o["fw_build"] = fw_build;
  o["mcu"] = mcu;
  o["board"] = board;
  if (has_update) {
    JsonObject u = o["update"].to<JsonObject>();
    u["slot"] = update_slot;
    u["max_bytes"] = update_max_bytes;
    u["state"] = update_pending ? "pending" : "valid";
    u["rolled_back"] = update_rolled_back;
    if (update_install_max_s) u["install_max_s"] = update_install_max_s;
  }
  JsonArray caps = o["capabilities"].to<JsonArray>();
  for (auto& c : capabilities) caps.add(c);
  JsonArray ch = o["channels"].to<JsonArray>();
  for (auto c : ALL_CHANNELS) ch.add(name_of(c));
  JsonArray in = o["inputs"].to<JsonArray>();
  for (auto i : ALL_INPUTS) in.add(pt::name(i));
  JsonArray ac = o["actions"].to<JsonArray>();
  for (auto& a : actions) {
    JsonObject x = ac.add<JsonObject>();
    x["key"] = a.key;
    x["label"] = a.label;
    x["confirm"] = a.confirm;
    x["momentary"] = a.momentary;
    if (a.hold) x["hold"] = true;
  }
  if (!stop_button) o["stop_button"] = false;
  JsonArray te = o["terminals"].to<JsonArray>();
  for (auto& t : terminals) {
    JsonObject x = te.add<JsonObject>();
    x["id"] = t.id;
    x["kind"] = terminalKindName(t.kind);
    x["label"] = t.label;
    if (t.has_role) x["role"] = t.role;
    x["shows"] = t.shows;
    x["disabled"] = t.disabled;
    if (!t.alias.empty()) x["alias"] = t.alias;
  }
}

void Info::announceJson(uint16_t port, JsonObject o) const {
  o["proptroller"] = "announce";
  o["v"] = PROTOCOL_VERSION;
  o["id"] = id;
  o["name"] = name;
  o["fw"] = fw;
  o["fw_version"] = fw_version;
  o["fw_impl"] = fw_impl;
  o["mcu"] = mcu;
  o["board"] = board;
  o["port"] = port;
}

std::string Info::announce(uint16_t port) const {
  JsonDocument d;
  announceJson(port, d.to<JsonObject>());
  std::string s;
  serializeJson(d, s);
  return s;
}

std::vector<std::pair<std::string, std::string>> Info::txt() const {
  return {{"v", "1"}, {"id", id}, {"name", name}, {"fw", fw}, {"fwv", fw_version}, {"fwi", fw_impl}, {"mcu", mcu}, {"board", board}};
}

void NetState::toJson(JsonObject o) const {
  o["mode"] = mode;
  o["ip"] = ip;
  o["ssid"] = ssid;
  if (!host.empty()) o["host"] = host;
  if (!hotspot.empty()) o["hotspot"] = hotspot;
  if (has_rssi) o["rssi"] = rssi;
}

// ------------------------------------------------------------------ requests

const char* errorCodeName(ErrorCode c) {
  static const char* const n[] = {"bad_request", "version", "invalid", "not_found", "refused", "unsupported", "pin", "internal"};
  return n[(int)c];
}

static const char* const READ_ONLY_OPS[] = {"info", "state", "list_shows", "get_show", "get_params", "get_profile", "get_script", "check_script", "get_backup", "wifi_scan", "get_board", "get_defaults", "list_patterns"};
bool Request::mutates() const {
  for (auto o : READ_ONLY_OPS)
    if (op == o) return false;
  return true;
}

Response::Response(bool has_id, uint32_t id, const char* kind) {
  doc_["v"] = PROTOCOL_VERSION;
  if (has_id) doc_["id"] = id;
  doc_["ok"] = true;
  doc_["result"]["kind"] = kind;
}

Response Response::err(bool has_id, uint32_t id, ErrorCode c, const std::string& message) {
  Response r;
  r.doc_["v"] = PROTOCOL_VERSION;
  if (has_id) r.doc_["id"] = id;
  r.doc_["ok"] = false;
  r.doc_["error"]["code"] = errorCodeName(c);
  r.doc_["error"]["message"] = message;
  return r;
}

Response Response::invalid(const Request& req, const ShowError& e) {
  Response r = err(req, ErrorCode::Invalid, e.message());
  JsonObject se = r.doc_["error"]["show_error"].to<JsonObject>();
  se["problem"] = problemName(e.problem);
  if (e.track >= 0) se["track"] = e.track;
  if (e.cue >= 0) se["cue"] = e.cue;
  return r;
}

std::string Response::json() const {
  // measured first and written into one block of the exact size: a string that grows while it is written asks
  // for twice its size at the end (a 15 KB answer then needs 32 KB in one piece beside the 16 KB it had)
  std::string s;
  s.reserve(jsonSize());
  serializeJson(doc_, s);
  return s;
}

size_t Response::jsonSize() const { return measureJson(doc_); }

static bool isU(JsonVariantConst v, uint64_t max) { return v.is<uint64_t>() && v.as<uint64_t>() <= max; }

bool reqChannel(const Request& r, const char* key, Channel& out) {
  JsonVariantConst v = r[key];
  if (!v.is<const char*>()) return false;
  if (!parseChannel(v.as<const char*>(), out)) return false;
  return strcmp(name_of(out), v.as<const char*>()) == 0;
}

bool reqInput(const Request& r, const char* key, Input& out) {
  JsonVariantConst v = r[key];
  if (!v.is<const char*>()) return false;
  return parseInput(v.as<const char*>(), out) && strcmp(name(out), v.as<const char*>()) == 0;
}

bool reqTarget(const Request& r, const char* key, Target& out) {
  JsonVariantConst v = r[key];
  return v.is<const char*>() && parseTarget(v.as<const char*>(), out);
}

bool reqSound(const Request& r, SoundCommand& c) {
  JsonVariantConst cmd = r["cmd"];
  if (!cmd.is<const char*>()) return false;
  std::string k = cmd.as<const char*>();
  auto u = [&](const char* f, uint64_t max, uint16_t& out) {
    if (!isU(r[f], max)) return false;
    out = (uint16_t)r[f].as<uint64_t>();
    return true;
  };
  if (k == "play_folder") { c.kind = SoundCommand::Kind::PlayFolder; return u("folder", 255, c.a) && u("track", 255, c.b); }
  if (k == "play_mp3") { c.kind = SoundCommand::Kind::PlayMp3; return u("track", 65535, c.a); }
  if (k == "play_index") { c.kind = SoundCommand::Kind::PlayIndex; return u("index", 65535, c.a); }
  if (k == "loop_index") { c.kind = SoundCommand::Kind::LoopIndex; return u("index", 65535, c.a); }
  if (k == "loop_folder") { c.kind = SoundCommand::Kind::LoopFolder; return u("folder", 255, c.a); }
  if (k == "loop_mp3") { c.kind = SoundCommand::Kind::LoopMp3; return u("track", 65535, c.a); }
  if (k == "volume") { c.kind = SoundCommand::Kind::Volume; return u("volume", 255, c.a); }
  static const char* const simple[] = {"stop", "pause", "resume", "next", "reset", "select_sd"};
  static const SoundCommand::Kind sk[] = {SoundCommand::Kind::Stop, SoundCommand::Kind::Pause, SoundCommand::Kind::Resume,
                                          SoundCommand::Kind::Next, SoundCommand::Kind::Reset, SoundCommand::Kind::SelectSd};
  for (int i = 0; i < 6; i++)
    if (k == simple[i]) { c = SoundCommand::simple(sk[i]); return true; }
  return false;
}

// Type check of each op's fields, like the serde enum: a missing or mistyped field is a bad request.
static bool checkOp(Request& r, std::string& why, bool allow_custom) {
  const std::string& op = r.op;
  auto need = [&](bool ok, const char* field) {
    if (!ok) why = std::string("missing or invalid field `") + field + "`";
    return ok;
  };
  auto optU = [&](const char* f, uint64_t max) { return r[f].isNull() || isU(r[f], max); };
  if (op == "info" || op == "state" || op == "list_shows" || op == "stop_all" ||
      op == "add_example" || op == "get_params" || op == "reboot" || op == "get_profile" || op == "reset_runs")
    return true;
  if (op == "factory_reset")
    return need(r["forget_board"].isNull() || r["forget_board"].is<bool>(), "forget_board") &&
           need(r["keep_wifi"].isNull() || r["keep_wifi"].is<bool>(), "keep_wifi");
  if (op == "get_defaults") return true;
  if (op == "apply_defaults") {
    if (r["only"].isNull()) return true;
    if (!r["only"].is<JsonArrayConst>() || r["only"].as<JsonArrayConst>().size() == 0) return need(false, "only");
    for (JsonVariantConst x : r["only"].as<JsonArrayConst>()) {
      const std::string w = x | "";
      if (w != "profile" && w != "settings" && w != "script") { why = "`only` takes \"profile\", \"settings\" and \"script\""; return false; }
    }
    return true;
  }
  if (op == "get_board") return need(r["preset"].isNull() || r["preset"].is<const char*>(), "preset");
  if (op == "set_board") {
    const int given = (r["package"].isNull() ? 0 : 1) + (r["preset"].isNull() ? 0 : 1) + (r["forget"].isNull() ? 0 : 1);
    if (given != 1) { why = "set_board takes one of `package` (a board package), `preset` (a built-in board's id) or `forget`: true"; return false; }
    const std::string def = r["defaults"] | "keep";
    return need(r["package"].isNull() || r["package"].is<JsonObjectConst>(), "package") &&
           need(r["preset"].isNull() || r["preset"].is<const char*>(), "preset") && need(r["forget"].isNull() || r["forget"] == true, "forget") &&
           need(r["defaults"].isNull() || def == "apply" || def == "keep", "defaults");
  }
  if (op == "get_show" || op == "delete_show" || op == "run" || op == "stop") return need(isU(r["show"], 65535), "show");
  if (op == "put_show") {
    Show s;
    std::string e;
    if (!fromJson(r["data"], s, e)) { why = "data: " + e; return false; }
    return true;
  }
  if (op == "pause") return need(r["on"].is<bool>(), "on");
  if (op == "cue") return need(r["name"].is<const char*>(), "name");
  if (op == "set_output") {
    Channel c = Channel::Out1;
    return need(reqChannel(r, "channel", c), "channel") && need(optU("level", 65535), "level") && need(optU("ms", MANUAL_PULSE_MAX_MS), "ms");
  }
  if (op == "pixel") {
    if (r["action"].isNull()) return true;
    if (!strcmp(r["action"]["do"] | "", "pattern")) return need(r["action"]["name"].is<const char*>(), "action.name");
    Action a;
    std::string e;
    if (!actionFromJson(r["action"], a, e)) { why = "action: " + e; return false; }
    return true;
  }
  if (op == "sound") {
    SoundCommand c;
    return need(reqSound(r, c), "cmd");
  }
  if (op == "set_params") {
    if (!r["values"].is<JsonArrayConst>()) return need(false, "values");
    for (JsonVariantConst pv : r["values"].as<JsonArrayConst>()) {
      Value v;
      if (!pv["key"].is<const char*>() || !Value::fromJson(pv["value"], v)) return need(false, "values");
    }
    return true;
  }
  if (op == "action") return need(r["key"].is<const char*>(), "key") && need(r["down"].isNull() || r["down"].is<bool>(), "down");
  if (op == "test_input") {
    Input i = Input::Trig1;
    return need(reqInput(r, "input", i), "input") && need(r["active"].is<bool>(), "active") && need(optU("ms", TEST_PRESS_MAX_MS), "ms");
  }
  if (op == "sim_input") {
    Input i = Input::Trig1;
    return need(reqInput(r, "input", i), "input") && need(r["active"].is<bool>(), "active") && need(optU("lease_ms", 0xFFFFFFFFull), "lease_ms");
  }
  if (op == "custom") return need(r["name"].is<const char*>(), "name");
  if (op == "set_profile") return need(r["data"].is<JsonObjectConst>(), "data");
  if (op == "get_script" || op == "get_backup" || op == "list_patterns") return true;
  if (op == "put_pattern")
    return need(r["name"].is<const char*>(), "name") && need(r["program"].is<const char*>(), "program") &&
           need(r["controls"].isNull() || r["controls"].is<JsonObjectConst>(), "controls");
  if (op == "delete_pattern") return need(r["name"].is<const char*>(), "name");
  if (op == "wifi_scan") return need(r["rescan"].isNull() || r["rescan"].is<bool>(), "rescan");
  if (op == "put_backup") return need(r["data"].is<JsonObjectConst>(), "data") && need(r["keep_name"].isNull() || r["keep_name"].is<bool>(), "keep_name");
  if (op == "check_script" || op == "set_script")
    return need(r["text"].is<const char*>(), "text") && need(r["sounds"].isNull() || r["sounds"].is<JsonObjectConst>(), "sounds") &&
           need(optU("bytes", 0xFFFFFFFFull), "bytes");
  if (allow_custom) {
    r.custom = true;
    return true;
  }
  why = "unknown op `" + op + "`: this firmware does not have it";
  return false;
}

Response unknownOp(const Request& r) { return Response::err(r, ErrorCode::BadRequest, "unknown op `" + r.op + "`: this firmware does not have it"); }

bool peekRequestId(const char* body, size_t len, uint32_t& id) {
  int depth = 0;
  bool key_is_id = false, after_colon = false, expect_key = false;
  for (size_t i = 0; i < len; i++) {
    char c = body[i];
    if (c == '"') {
      size_t j = i + 1;
      while (j < len && body[j] != '"') j += body[j] == '\\' ? 2 : 1;
      if (j >= len) return false;
      if (depth == 1 && expect_key) key_is_id = j - i - 1 == 2 && body[i + 1] == 'i' && body[i + 2] == 'd';
      expect_key = false;
      i = j;
      continue;
    }
    if (c == '{' || c == '[') {
      depth++;
      expect_key = depth == 1 && c == '{';
      after_colon = false;
      continue;
    }
    if (c == '}' || c == ']') { depth--; continue; }
    if (depth != 1) continue;
    if (c == ',') { expect_key = true; key_is_id = false; after_colon = false; continue; }
    if (c == ':') { after_colon = key_is_id; continue; }
    if (after_colon && c >= '0' && c <= '9') {
      uint64_t v = 0;
      size_t j = i;
      while (j < len && body[j] >= '0' && body[j] <= '9' && v <= 0xFFFFFFFFull) v = v * 10 + (uint64_t)(body[j++] - '0');
      if (v > 0xFFFFFFFFull || (j < len && (body[j] == '.' || body[j] == 'e' || body[j] == 'E'))) return false;
      id = (uint32_t)v;
      return true;
    }
    if (after_colon && c != ' ' && c != '\t' && c != '\r' && c != '\n') after_colon = false;  // not a number
  }
  return false;
}

Response errorFor(const char* body, size_t len, ErrorCode c, const std::string& message) {
  uint32_t id = 0;
  bool has = peekRequestId(body, len, id);
  return Response::err(has, id, c, message);
}

bool parseRequest(const char* body, size_t len, Request& r, Response*& error, bool allow_custom_ops) {
  error = nullptr;
  DeserializationError de = deserializeJson(r.doc, body, len);
  if (de || !r.doc.is<JsonObject>()) {
    // broken JSON: the id is looked for in the text, so the client can still match the answer
    error = new Response(errorFor(body, len, ErrorCode::BadRequest, de ? std::string("bad JSON: ") + de.c_str() : "expected an object"));
    return false;
  }
  if (!r.doc["id"].isNull()) {
    if (!isU(r.doc["id"], 0xFFFFFFFFull)) { error = new Response(Response::err(false, 0, ErrorCode::BadRequest, "invalid id")); return false; }
    r.has_id = true;
    r.id = r.doc["id"].as<uint32_t>();
  }
  if (!r.doc["v"].isNull()) {
    if (!isU(r.doc["v"], 65535)) { error = new Response(Response::err(r, ErrorCode::BadRequest, "invalid v")); return false; }
    r.v = r.doc["v"].as<uint16_t>();
  }
  if (!r.doc["pin"].isNull()) {
    if (!r.doc["pin"].is<const char*>()) { error = new Response(Response::err(r, ErrorCode::BadRequest, "invalid pin")); return false; }
    r.has_pin = true;
    r.pin = r.doc["pin"].as<const char*>();
  }
  if (!r.doc["op"].is<const char*>()) { error = new Response(Response::err(r, ErrorCode::BadRequest, "missing field `op`")); return false; }
  r.op = r.doc["op"].as<const char*>();
  std::string why;
  if (!checkOp(r, why, allow_custom_ops)) { error = new Response(Response::err(r, ErrorCode::BadRequest, why)); return false; }
  if (r.v > PROTOCOL_VERSION) {
    error = new Response(Response::err(r.has_id, r.id, ErrorCode::Version, "protocol version not supported"));
    return false;
  }
  return true;
}

// ------------------------------------------------------------------ events

void noticeToJson(const Notice& n, JsonObject o) {
  switch (n.kind) {
    case Notice::Kind::Started: {
      o["notice"] = "started";
      o["show"] = n.show;
      JsonObject c = o["cause"].to<JsonObject>();
      c["by"] = n.cause.byName();
      if (n.cause.by == Cause::By::ByInput || n.cause.by == Cause::By::ByRelease) c["detail"] = name(n.cause.input);
      else if (n.cause.by == Cause::By::ByCue) c["detail"] = n.cause.name;
      else if (n.cause.by == Cause::By::ByAfter) c["detail"] = n.cause.show;
      break;
    }
    case Notice::Kind::Ended: o["notice"] = "ended"; o["show"] = n.show; o["reason"] = endReasonName(n.reason); break;
    case Notice::Kind::IgnoredTrigger: o["notice"] = "ignored"; o["show"] = n.show; o["why"] = ignoredName(n.why); break;
    case Notice::Kind::Watchdog: o["notice"] = "watchdog"; o["channel"] = name_of(n.channel); break;
    case Notice::Kind::Interlock: o["notice"] = "interlock"; o["channel"] = name_of(n.channel); break;
    case Notice::Kind::PausedChanged: o["notice"] = "paused"; o["paused"] = n.paused; break;
  }
}

void EventLog::push(const char* event, const std::string& data_json) {
  seq_++;
  char head[64];
  (void)snprintf(head, sizeof head, "{\"v\":1,\"seq\":%lu,\"event\":\"%s\"", (unsigned long)seq_, event);
  std::string m = head;
  if (!data_json.empty()) { m += ",\"data\":"; m += data_json; }
  m += "}";
  if (events_.size() == CAPACITY) events_.pop_front();
  events_.push_back({seq_, m});
}

void EventLog::notice(const Notice& n) {
  JsonDocument d;
  noticeToJson(n, d.to<JsonObject>());
  std::string s;
  serializeJson(d, s);
  push("notice", s);
}

void EventLog::log(const std::string& line) {
  JsonDocument d;
  d.set(line);
  std::string s;
  serializeJson(d, s);
  push("log", s);
}

uint32_t EventLog::since(uint32_t seq, std::vector<std::string>& out) const {
  for (auto& e : events_)
    if ((int32_t)(e.first - seq) > 0) out.push_back(e.second);
  return seq_;
}

std::string stateEvent(const std::string& state_json) {
  std::string m = "{\"v\":1,\"seq\":0,\"event\":\"state\",\"data\":";
  m += state_json;
  m += "}";
  return m;
}

uint64_t stateHash(const std::string& st) {
  uint64_t h = 1469598103934665603ULL;
  size_t skip = st.find("\"uptime_ms\":"), skip_end = skip;
  if (skip != std::string::npos) skip_end = st.find_first_not_of("\"uptime_ms:0123456789", skip);
  if (skip_end == std::string::npos) skip_end = st.size();
  for (size_t i = 0; i < st.size(); i++) {
    if (i == skip) { i = skip_end - 1; continue; }
    h = (h ^ (uint8_t)st[i]) * 1099511628211ULL;
  }
  return h;
}

// ------------------------------------------------------------------ state pieces

void patternToJson(const Pattern& p, JsonObject parent, const char* key) {
  if (p.kind == Pattern::Kind::Count) parent[key]["count"] = p.count;
  else parent[key] = p.name();
}

void showStatusToJson(const ShowStatus& s, JsonObject o) {
  switch (s.kind) {
    case ShowStatus::Kind::Idle: o["state"] = "idle"; break;
    case ShowStatus::Kind::Running: o["state"] = "running"; o["elapsed_ms"] = s.elapsed_ms; o["length_ms"] = s.length_ms; break;
    case ShowStatus::Kind::Cooldown: o["state"] = "cooldown"; o["remaining_ms"] = s.remaining_ms; break;
    case ShowStatus::Kind::Disabled: o["state"] = "disabled"; break;
  }
}

}  // namespace pt
