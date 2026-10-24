// Backups (PROTOCOL.md "Backups"):
//
// * get_backup: everything the board needs to do what it does now. Never in it: the secrets (page password,
//   Wi-Fi password, ap_pass) and the board's own access and identity (wifi_ssid, page_lock, name: the
//   document's top-level "name", remote_trigger_enable). The profile, kept inside the settings record on this
//   firmware, is the document's top-level "profile" (null when there is none).
// * put_backup: every setting, show, the profile and the script checked with this board's rules first; the
//   first problem is the answer (invalid) and nothing changes. The shows replace the board's shows.
// * C++ only: shows a sketch defines in code are not in a backup and a restore leaves them; the sketch's own
//   applied values (Settings::sketch) stay the board's.
#include "device.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <string.h>

namespace pt {

// put_backup applied everything but the script could not be stored (an `internal` error, not `invalid`)
static const char* const RESTORED_BUT = "the backup was restored, but the show script could not be stored: ";

static const char* const BACKUP_FORMAT = "proptroller-backup";
static const uint16_t BACKUP_FORMAT_VERSION = 1;
static const char* const NOT_BACKED_UP[] = {"name", "wifi_ssid", "page_lock", "remote_trigger_enable"};

namespace backup_text {
static const char* const NOT_A_BACKUP = "not a Proptroller backup (format must be \"proptroller-backup\")";
static const char* const SHOWS_TOO_BIG = "the shows do not fit this board's storage";
static const char* const SCRIPT_TOO_BIG = "script: the script and its sound list do not fit the script store";
}  // namespace backup_text

static bool backedUp(const ParamDesc& d) {
  if (d.isSecret()) return false;
  for (auto k : NOT_BACKED_UP)
    if (d.key == k) return false;
  return true;
}

void Controller::backupJson(JsonObject o) const {
  Info in = info();
  o["format"] = BACKUP_FORMAT;
  o["format_version"] = BACKUP_FORMAT_VERSION;
  JsonObject from = o["from"].to<JsonObject>();
  from["id"] = in.id;
  from["name"] = in.name;
  from["fw"] = in.fw;
  from["fw_version"] = in.fw_version;
  from["fw_impl"] = in.fw_impl;
  from["mcu"] = in.mcu;
  from["board"] = in.board;
  o["name"] = settings_.name;
  JsonArray st = o["settings"].to<JsonArray>();
  for (auto& d : Settings::params()) {
    if (!backedUp(d)) continue;
    Value v;
    if (!settings_.get(d.key, v)) continue;
    JsonObject x = st.add<JsonObject>();
    x["key"] = d.key;
    v.toJson(x, "value");
  }
  JsonArray sh = o["shows"].to<JsonArray>();
  for (size_t i = 0; i < engine_.showCount(); i++) {
    const Show& s = engine_.showAt(i);
    if (std::find(code_shows_.begin(), code_shows_.end(), s.id) != code_shows_.end()) continue;
    toJson(s, sh.add<JsonObject>());
  }
  if (script_text_.empty()) o["script"] = nullptr;
  else {
    JsonObject sc = o["script"].to<JsonObject>();
    sc["text"] = script_text_;
    JsonObject so = sc["sounds"].to<JsonObject>();
    for (auto& e : script_sounds_) so[e.first] = e.second;
  }
  if (settings_.profile.empty()) o["profile"] = nullptr;
  else settings_.profile.toJson(o["profile"].to<JsonObject>());
}

// A backup's shape: "" or what is wrong (answered bad_request).
static std::string backupShape(JsonVariantConst d) {
  if (!d.is<JsonObjectConst>()) return "data: expected a backup document";
  if (!d["format"].is<const char*>()) return "data: missing field `format`";
  if (!d["format_version"].is<uint16_t>()) return "data: missing field `format_version`";
  JsonVariantConst f = d["from"];
  if (!f.is<JsonObjectConst>()) return "data: missing field `from`";
  for (auto k : {"id", "name", "fw", "fw_version", "fw_impl", "mcu"})
    if (!f[k].is<const char*>()) return std::string("data: from: missing field `") + k + "`";
  if (!d["name"].is<const char*>()) return "data: missing field `name`";
  if (!d["settings"].isNull()) {
    if (!d["settings"].is<JsonArrayConst>()) return "data: settings: expected a list";
    for (JsonVariantConst pv : d["settings"].as<JsonArrayConst>()) {
      Value v;
      if (!pv["key"].is<const char*>() || !Value::fromJson(pv["value"], v)) return "data: settings: expected {\"key\", \"value\"}";
    }
  }
  if (!d["shows"].isNull() && !d["shows"].is<JsonArrayConst>()) return "data: shows: expected a list";
  for (JsonVariantConst sv : d["shows"].as<JsonArrayConst>()) {
    Show s;
    std::string e;
    if (!fromJson(sv, s, e)) return "data: shows: " + e;
  }
  JsonVariantConst sc = d["script"];
  if (!sc.isNull()) {
    if (!sc.is<JsonObjectConst>() || !sc["text"].is<const char*>()) return "data: script: missing field `text`";
    if (!sc["sounds"].isNull() && !sc["sounds"].is<JsonObjectConst>()) return "data: script: sounds: expected an object";
  }
  if (!d["profile"].isNull() && !d["profile"].is<JsonObjectConst>()) return "data: profile: expected an object";
  return "";
}

Response Controller::backupOp(Request& r) {
  if (r.op == "get_backup") {
    Response resp = Response::ok(r, "backup");
    // built with the lock given back: only the step runs meanwhile, and it changes no setting, show or script
    // (window_lock.h)
    Unlocked u(*this);
    backupJson(resp.result()["data"].to<JsonObject>());
    return resp;
  }
  std::string shape = backupShape(r["data"]);
  if (!shape.empty()) return Response::err(r, ErrorCode::BadRequest, shape);
  if (!r["keep_name"].isNull() && !r["keep_name"].is<bool>()) return Response::err(r, ErrorCode::BadRequest, "keep_name: expected true or false");
  bool keep_name = r["keep_name"] | true;
  std::vector<std::string> skipped;
  std::string why = restore(r, keep_name, skipped);
  if (!why.empty()) return Response::err(r, why.compare(0, strlen(RESTORED_BUT), RESTORED_BUT) ? ErrorCode::Invalid : ErrorCode::Internal, why);
  Response resp = Response::ok(r, "restored");
  JsonArray a = resp.result()["skipped"].to<JsonArray>();
  for (auto& k : skipped) a.add(k);
  return resp;
}

// put_backup: "" when restored, else the first problem (nothing changed).
std::string Controller::restore(Request& r, bool keep_name, std::vector<std::string>& skipped) {
  JsonVariantConst data = r["data"];
  if (data["format"].as<std::string>() != BACKUP_FORMAT) return backup_text::NOT_A_BACKUP;
  if (store_newer_ || store_unreadable_)
    return std::string("the stored settings ") + (store_newer_ ? "are from a newer firmware" : "could not be read") +
           " and are kept, so a restore could not be saved: factory reset first, then restore";
  std::string held = scriptHeldText();
  if (!held.empty()) return held;
  uint16_t fv = data["format_version"].as<uint16_t>();
  if (fv > BACKUP_FORMAT_VERSION)
    return "the backup is format version " + std::to_string(fv) + "; this firmware reads up to " + std::to_string(BACKUP_FORMAT_VERSION) +
           ": update it first";
  // settings, onto the current ones (secrets, Wi-Fi and page lock stay this board's)
  std::unique_ptr<Settings> next_box(new Settings(settings_));  // about 1 KB: on the heap
  Settings& next = *next_box;
  for (JsonVariantConst pv : data["settings"].as<JsonArrayConst>()) {
    std::string key = pv["key"].as<std::string>();
    const ParamDesc* d = Settings::find(key);
    if (!d || !backedUp(*d)) { skipped.push_back(key); continue; }
    Value v;
    (void)Value::fromJson(pv["value"], v);
    std::string why = d->explain(v);
    if (!why.empty()) return "settings: " + why;
    const char* e = next.set(key, v);
    if (e) return "settings: " + d->label + ": " + e;
  }
  if (!keep_name) {
    const char* e = next.set("name", Value::text(data["name"].as<std::string>()));
    if (e) return std::string("name: ") + e;
  }
  // the profile first: the script may use its terminal aliases (SS-N)
  Profile profile;
  if (!data["profile"].isNull()) {
    std::string why;
    if (!parseProfile(data["profile"], profile, why)) return "profile: " + why;
  }
  next.profile = profile;
  // the script, checked with the new settings
  std::string text;
  SoundList sounds;
  JsonVariantConst sc = data["script"];
  if (!sc.isNull() && sc["text"].as<JsonString>().size() > 0) {
    text = sc["text"].as<std::string>();
    // the document's copy of the text goes now (its strings are freed one by one): one copy of the script
    // while it is checked and stored, not two
    r.doc["data"]["script"].as<JsonObject>().remove("text");
    std::string sj, why;
    if (!sc["sounds"].isNull()) serializeJson(sc["sounds"], sj);
    if (!soundListParse(sj, sounds, why)) return "script: " + why;
  }
  std::unique_ptr<ScriptProgram> prog(new ScriptProgram());
  ScriptBoard nb = scriptBoardFor(next);
  ScriptCheck c;
  {
    Unlocked u(*this);
    c = scriptCompile(text, sounds, nb, *prog);
  }
  if (!c.ok()) return "script: " + c.errors[0].text;
  if (script_store_ && !text.empty()) {
    std::string rec = scriptRecord(text, sounds, prog.get());
    if (rec.empty() || rec.size() > script_store_->capacity()) return backup_text::SCRIPT_TOO_BIG;
  }

  // shows, with this board's rules and the new settings' roles
  // (one at a time, each measured for the settings record and dropped: the shows are read from the parsed
  // backup again when they are applied, so there is never a second copy of every show beside the document)
  std::vector<uint16_t> ids;
  size_t shows_size = 0;
  // counted first: the board takes MAX_SHOWS, the sketch's own shows included (they stay)
  size_t backup_shows = data["shows"].as<JsonArrayConst>().size();
  auto tooMany = [&](size_t room) {
    return "the backup has " + std::to_string(backup_shows) + " shows; this board takes at most " + std::to_string(room) + " shows";
  };
  if (backup_shows > MAX_SHOWS) return tooMany(MAX_SHOWS - std::min(code_shows_.size(), MAX_SHOWS));
  for (JsonVariantConst sv : data["shows"].as<JsonArrayConst>()) {
    Show s;
    std::string e;
    (void)fromJson(sv, s, e);
    std::string id = std::to_string(s.id);
    if (std::find(ids.begin(), ids.end(), s.id) != ids.end()) return "show " + id + ": two shows have this number";
    ShowError se = validate(s);
    if (!se.ok()) return "show " + id + ": " + explain(se, s);
    std::string m = checkReserved(s, next);
    if (!m.empty()) return ("show " + id + ": ").append(m);
    ids.push_back(s.id);
    if (store_) {
      JsonDocument d;
      toJson(s, d.to<JsonObject>());
      shows_size += measureJson(d) + 1;
    }
  }
  size_t code_kept = 0;  // the sketch's shows a backup show does not replace
  for (auto id : code_shows_) code_kept += std::find(ids.begin(), ids.end(), id) == ids.end();
  if (ids.size() + code_kept > MAX_SHOWS) return tooMany(MAX_SHOWS - code_kept);
  // the settings record with these (and the sketch's shows, which stay)
  // (measured a part at a time: never a second copy of every show)
  if (store_) {
    size_t n = 64;  // the envelope: {"version":N,"settings":...,"shows":[...]}
    {
      JsonDocument d;
      next.toJson(d.to<JsonObject>());
      n += measureJson(d);
    }
    n += shows_size;
    if (n > store_->capacity()) return backup_text::SHOWS_TOO_BIG;
  }
  // everything passed: apply
  bool df_changed = next.df_ack != settings_.df_ack || next.df_checksum != settings_.df_checksum;
  settings_ = next;
  std::vector<uint16_t> old;
  for (size_t i = 0; i < engine_.showCount(); i++) {
    uint16_t id = engine_.showAt(i).id;
    if (std::find(code_shows_.begin(), code_shows_.end(), id) == code_shows_.end()) old.push_back(id);
  }
  for (auto id : old) engine_.deleteShow(id);
  for (JsonVariantConst sv : data["shows"].as<JsonArrayConst>()) {
    Show s;
    std::string e;
    (void)fromJson(sv, s, e);
    ShowError se = engine_.setShow(std::move(s));
    if (!se.ok()) log("restore: show rejected: " + se.message());  // validated above: a refusal is a bug
  }
  // the parsed backup is no longer needed: its memory goes before the script record is made
  r.doc.clear();
  applySettings();
  if (df_changed) df_ = DfPlayer(dfConfig(settings_, false));
  // the script: stored and running, or removed when the text has no effects
  bool empty = prog->effects == 0;
  std::string why;
  std::string script_failed;
  if (!scriptWrite(text, sounds, prog.get(), empty, why)) {
    log("restore: " + why);
    script_failed = RESTORED_BUT + why;
  } else {
    script_text_ = empty ? std::string() : text;
    script_sounds_ = empty ? SoundList() : sounds;
    script_error_.clear();
    scriptInstall(empty ? nullptr : std::move(prog));
  }
  for (auto& cmd : sound_.setConfig(soundConfig(), now_)) df_.command(cmd);
  events_.push("script_changed");
  saveNow();
  events_.push("params_changed");
  events_.push("shows_changed");
  log("a backup was restored");
  return script_failed;
}

}  // namespace pt
