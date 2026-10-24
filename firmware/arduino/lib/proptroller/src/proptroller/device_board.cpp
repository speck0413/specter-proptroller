// Board packages on the generic controller (BOARD-PACKAGES.md): get_board, set_board, and the package's defaults
// at power-on. The package itself is read before the controller exists (boardAtBoot, at power-on, before any pin
// is touched); here it is reported, replaced in the store (applied by the next restart) or forgotten.
#include "device.h"
#include <ctype.h>

namespace pt {

static const char* const BOARD_UNSUPPORTED = "this firmware takes no board packages: its pins are built in";
static const char* const BOARD_NOT_MQTT = "set_board works over USB or Wi-Fi (the board's page or Specter Studio), not over MQTT";

void Controller::setBoardPackages(Storage* store, const BoardBoot& running, const ChipRules& chip, const std::vector<BuiltinBoard>& builtins) {
  board_store_ = store;
  board_boot_ = running;
  chip_ = chip;
  builtins_ = builtins;
  identity_.board = running.cfg.id;
  board_stored_ = running.source == BoardSource::Stored;
  board_stored_id_ = board_stored_ ? running.cfg.id : "";
  board_stored_name_ = board_stored_ ? running.cfg.name : "";
  board_pending_ = false;
  if (running.source == BoardSource::None) {
    // a record that is there but cannot be used: kept as it is; a new image fails its self-check (and goes back to
    // the one that could use it)
    records_ok_ = false;
    log(running.problem);
  }
}

// After a factory reset that kept the Wi-Fi: the name, the network, its password and the hotspot's password as they
// were before the reset (the defaults may have set a name), then the record written at once without its "reset"
// mark, so the next power-on is not fresh again.
void Controller::restoreKept() {
  if (!reset_kept_) return;
  settings_.name = reset_kept_->name;
  settings_.wifi_ssid = reset_kept_->wifi_ssid;
  settings_.wifi_pass = reset_kept_->wifi_pass;
  settings_.ap_pass = reset_kept_->ap_pass;
  reset_kept_.reset();
  log("factory reset: the name and the Wi-Fi were kept");
  markDirty();
  saveNow();
}

std::string Controller::applyBoardDefaults(const FactoryDefaults* image) {
  struct Kept {  // whatever path returns: the kept values go back last
    Controller& c;
    ~Kept() { c.restoreKept(); }
  } kept{*this};
  if (image) image_defaults_ = *image;  // the texts are the image's own (string literals): kept for get_defaults
  const bool stored = board_boot_.source == BoardSource::Stored;
  if (stored && board_boot_.has_defaults && board_store_ && (fresh_ || board_boot_.apply_defaults)) {
    std::string rec, json, profile, settings, script;
    bool apply = false;
    uint16_t format = 0;
    if (board_store_->fetch(rec) != Stored::Ok || boardRecordUnpack(rec, json, apply, format) != BoardRecordRead::Ok ||
        !boardDefaults(json, profile, settings, script))
      return "board package: its defaults could not be read";
    std::string().swap(rec);
    std::string().swap(json);
    FactoryDefaults d;
    d.profile = profile.c_str();
    d.settings = settings.c_str();
    d.script = script.c_str();
    std::string refused = applyFactoryDefaults(d, true);
    log("the board package's defaults were applied (" + board_boot_.cfg.id + ")");
    // asked for once (set_board "defaults": "apply"): the record is written again without the request, so the
    // next power-on keeps what was changed since. Not while a new image waits for its self-check (a rollback finds
    // the record as it was; the defaults are then applied once more at the next power-on).
    if (board_boot_.apply_defaults && !(update_target_ && update_target_->state() == BootState::Pending)) {
      std::string again;
      if (board_store_->fetch(rec) == Stored::Ok && boardRecordUnpack(rec, again, apply, format) == BoardRecordRead::Ok &&
          !board_store_->save(boardRecordPack(again, false))) {
        store_errors++;
        log("board package: could not clear its \"apply the defaults\" mark in flash");
      }
      board_boot_.apply_defaults = false;
    }
    return refused;
  }
  // no package of its own: the image's compiled-in defaults (a board support package), on a fresh board only
  if (image && !stored) return applyFactoryDefaults(*image);
  return "";
}

Response Controller::boardOp(Request& r) {
  if (!board_store_) return Response::err(r, ErrorCode::Unsupported, BOARD_UNSUPPORTED);
  if (r.op == "get_board") {
    Response resp = Response::ok(r, "board");
    JsonObject o = resp.result();
    o["mcu"] = mcuFamily(chip_.mcu);
    JsonObject run = o["running"].to<JsonObject>();
    run["id"] = board_boot_.cfg.id;
    run["name"] = board_boot_.cfg.name;
    run["source"] = boardSourceName(board_boot_.source);
    if (!board_boot_.problem.empty()) run["problem"] = board_boot_.problem;
    if (board_stored_ || board_pending_) {
      JsonObject st = o["stored"].to<JsonObject>();
      if (board_stored_) {
        st["id"] = board_stored_id_;
        st["name"] = board_stored_name_;
      }
      st["pending"] = board_pending_;  // a restart applies the store (with nothing stored: the image's default board)
    }
    JsonArray pre = o["presets"].to<JsonArray>();
    for (auto& b : builtins_) {
      JsonObject x = pre.add<JsonObject>();
      x["id"] = b.id;
      x["name"] = b.name;
    }
    if (!r["preset"].isNull()) {
      const BuiltinBoard* b = findBuiltin(builtins_, r["preset"].as<std::string>());
      if (!b) return Response::err(r, ErrorCode::NotFound, "this firmware has no built-in board \"" + r["preset"].as<std::string>() + "\"");
      builtinToJson(*b, chip_, o["package"].to<JsonObject>());
    }
    return resp;
  }
  // set_board: the store only; what runs changes at the next restart
  if (origin_ == Origin::Mqtt) return Response::err(r, ErrorCode::Refused, BOARD_NOT_MQTT);
  if (wiped_) return Response::err(r, ErrorCode::Refused, "a factory reset cleared the flash: the board is restarting");
  if (update_target_ && update_target_->state() == BootState::Pending) return Response::err(r, ErrorCode::Refused, update_text::SELF_CHECK_PENDING);
  const bool apply = std::string(r["defaults"] | "keep") == "apply";
  if (r["forget"] | false) {
    bool ok;
    {
      Unlocked u(*this);
      ok = board_store_->wipe();
    }
    if (!ok) {
      store_errors++;
      return Response::err(r, ErrorCode::Internal, "the board package could not be removed from flash: try again");
    }
    board_stored_ = false;
    board_stored_id_.clear();
    board_stored_name_.clear();
    board_pending_ = board_boot_.source != BoardSource::BuiltIn;
    const BuiltinBoard& def = builtins_.front();
    Response resp = Response::ok(r, "board_set");
    resp.result()["id"] = def.id;
    resp.result()["name"] = def.name;
    resp.result()["restart_needed"] = board_pending_;
    resp.result()["warnings"].to<JsonArray>();
    log("board package forgotten: the next restart runs on the built-in board " + std::string(def.id));
    return resp;
  }
  std::string json, why;
  BoardConfig cfg;
  std::vector<std::string> warnings;
  bool ok;
  {
    Unlocked u(*this);  // the request's own data, the chip's rules and the built-ins (fixed since power-on)
    JsonDocument preset;
    JsonVariantConst pkg = r["package"];
    if (!r["preset"].isNull()) {
      const BuiltinBoard* b = findBuiltin(builtins_, r["preset"].as<std::string>());
      if (b) {
        preset["board_package"] = BOARD_PACKAGE_FORMAT;
        preset["id"] = b->id;
        preset["name"] = b->name;
        preset["base"] = b->id;
      }
      pkg = preset.as<JsonVariantConst>();
    }
    if (!r["preset"].isNull() && preset.isNull()) {
      std::string list;
      for (auto& x : builtins_) list += (list.empty() ? "" : ", ") + std::string(x.id);
      why = "this firmware has no built-in board \"" + r["preset"].as<std::string>() + "\" (it has: " + list + ")";
      ok = false;
    } else if (measureJson(pkg) > BOARD_PACKAGE_MAX_BYTES) {
      why = "the board package is " + std::to_string(measureJson(pkg)) + " bytes; at most " + std::to_string(BOARD_PACKAGE_MAX_BYTES / 1024) + " KB";
      ok = false;
    } else {
      ok = checkBoardPackage(pkg, chip_, builtins_, cfg, warnings, why);
      if (ok) serializeJson(pkg, json);
    }
  }
  if (!ok) return Response::err(r, ErrorCode::Invalid, "board package: " + why);
  std::string rec;
  {
    Unlocked u(*this);
    rec = boardRecordPack(json, apply);
    ok = rec.size() <= board_store_->capacity() && board_store_->save(rec);
  }
  if (!ok) {
    if (rec.size() > board_store_->capacity())
      return Response::err(r, ErrorCode::Internal, "board package: it takes " + std::to_string(rec.size()) + " bytes packed, the board keeps at most " +
                                                       std::to_string(board_store_->capacity()) + ": shorten its defaults (send the script with set_script instead)");
    store_errors++;
    return Response::err(r, ErrorCode::Internal, "board package: it could not be stored in flash; the board keeps the one it had");
  }
  board_stored_ = true;
  board_stored_id_ = cfg.id;
  board_stored_name_ = cfg.name;
  board_pending_ = true;
  log("board package stored: " + cfg.id + " (" + cfg.name + "); a restart applies it");
  Response resp = Response::ok(r, "board_set");
  resp.result()["id"] = cfg.id;
  resp.result()["name"] = cfg.name;
  resp.result()["restart_needed"] = true;
  JsonArray w = resp.result()["warnings"].to<JsonArray>();
  for (auto& x : warnings) w.add(x);
  return resp;
}

// ---------------------------------------------------------------- get_defaults / apply_defaults

bool Controller::runningDefaults(std::string& source, std::string& profile, std::string& settings, std::string& script,
                                 std::string& script_name, std::string& why) {
  profile.clear();
  settings.clear();
  script.clear();
  script_name.clear();
  if (board_boot_.source == BoardSource::Stored) {
    // the store holds the running package until a set_board replaces it (then it holds the next one)
    if (board_pending_) {
      why = "a new board package waits for a restart: restart the board first";
      return false;
    }
    std::string rec, json;
    bool apply = false;
    uint16_t format = 0;
    if (!board_store_ || board_store_->fetch(rec) != Stored::Ok || boardRecordUnpack(rec, json, apply, format) != BoardRecordRead::Ok ||
        !boardDefaults(json, profile, settings, script)) {
      why = "the board package's defaults could not be read";
      return false;
    }
    script_name = boardScriptName(json);
    source = "board_package";
    return true;
  }
  const FactoryDefaults& d = image_defaults_;
  if (d.profile) profile = d.profile;
  if (d.settings) settings = d.settings;
  if (d.script) script = d.script;
  if (d.script_name) script_name = d.script_name;
  source = profile.empty() && settings.empty() && script.empty() ? "none" : "firmware";
  return true;
}

Response Controller::defaultsOp(Request& r) {
  std::string source, profile, settings, script, script_name, why;
  bool ok;
  {
    Unlocked u(*this);  // the board package's record and the image's texts: nothing changes them meanwhile
    ok = runningDefaults(source, profile, settings, script, script_name, why);
  }
  if (!ok) return Response::err(r, ErrorCode::Refused, why);
  if (r.op == "get_defaults") {
    Response resp = Response::ok(r, "defaults");
    JsonObject o = resp.result();
    o["source"] = source;
    o["board"] = board_boot_.cfg.id;
    if (!profile.empty()) {
      JsonDocument p;
      if (!deserializeJson(p, profile)) o["profile"] = p.as<JsonVariantConst>();
    }
    if (!settings.empty()) {
      // "key=value" lines (the image's own) or the package's settings, as an object of texts
      JsonObject so = o["settings"].to<JsonObject>();
      size_t at = 0;
      while (at < settings.size()) {
        size_t nl = settings.find('\n', at);
        if (nl == std::string::npos) nl = settings.size();
        std::string line = settings.substr(at, nl - at);
        at = nl + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string t) {
          while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
          while (!t.empty() && isspace((unsigned char)t.front())) t.erase(t.begin());
          return t;
        };
        const std::string k = trim(line.substr(0, eq));
        if (!k.empty()) so[k] = trim(line.substr(eq + 1));
      }
    }
    if (!script.empty()) o["script"] = script;
    if (!script_name.empty()) o["script_name"] = script_name;
    return resp;
  }
  // apply_defaults: now, without a restart; the parts asked for (default: all three), in the usual order
  if (update_target_ && update_target_->state() == BootState::Pending) return Response::err(r, ErrorCode::Refused, update_text::SELF_CHECK_PENDING);
  bool want_profile = true, want_settings = true, want_script = true;
  if (!r["only"].isNull()) {
    want_profile = want_settings = want_script = false;
    for (JsonVariantConst x : r["only"].as<JsonArrayConst>()) {
      const std::string w = x | "";
      want_profile = want_profile || w == "profile";
      want_settings = want_settings || w == "settings";
      want_script = want_script || w == "script";
    }
  }
  FactoryDefaults d;
  JsonDocument applied_doc;
  JsonArray applied = applied_doc.to<JsonArray>();
  if (want_profile && !profile.empty()) { d.profile = profile.c_str(); applied.add("profile"); }
  if (want_settings && !settings.empty()) { d.settings = settings.c_str(); applied.add("settings"); }
  if (want_script && !script.empty()) { d.script = script.c_str(); applied.add("script"); }
  if (applied.size() == 0)
    return Response::err(r, ErrorCode::NotFound, std::string("this board has no default ") +
                                                     (r["only"].isNull() ? "settings, profile or script" : "for that") + " to apply");
  // applied as the board's own requests: this request already passed the page lock
  const Origin was = origin_;
  const uint32_t was_session = session_;
  setOrigin(Origin::Serial);
  const std::string refused = applyFactoryDefaults(d, true);
  setOrigin(was, was_session);
  log("defaults applied (" + source + ")");
  Response resp = Response::ok(r, "defaults_applied");
  resp.result()["source"] = source;
  resp.result()["applied"] = applied;
  JsonArray rf = resp.result()["refused"].to<JsonArray>();
  size_t at = 0;
  while (at < refused.size()) {
    size_t nl = refused.find('\n', at);
    if (nl == std::string::npos) nl = refused.size();
    rf.add(refused.substr(at, nl - at));
    at = nl + 1;
  }
  return resp;
}

}  // namespace pt
