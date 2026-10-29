// The controller's show scripts (SHOW-SCRIPT.md, firmware/show-script-golden/README.md): storing, checking,
// running, the protocol ops and the DMX port. The checker and the runner themselves are script.cpp.
#include "device.h"
#include <ArduinoJson.h>
#include "script_record.h"

namespace pt {


ScriptBoard scriptBoardFor(const Settings& s) {
  ScriptBoard b;
  b.dmx_on = s.dmx_s4;
  for (int i = 0; i < 4; i++) b.roles[i] = (uint8_t)s.roles[i];
  b.buttons_are_triggers = true;  // USER1-3 are always triggers (the checker keeps the flag for the shared golden set)
  if (s.effect_on) {
    b.effect_inputs = 0x03;  // TRIG1, TRIG2
    b.effect_channels = (uint8_t)(0x03 | (s.fx.out3_enabled ? 0x04 : 0) | (s.out4_ready_lamp ? 0x08 : 0));
  } else {
    b.ready_lamp = s.out4_ready_lamp;  // OUT4 shows whether the script's remote effect is ready
  }
  // the profile's key aliases of the settings a script line may name for a time or a count
  for (auto& a : s.profile.aliases)
    if (a.kind == AliasEntry::Kind::Key)
      for (auto k : SCRIPT_SETTINGS)
        if (a.target == k) b.setting_aliases.push_back({a.alias, a.target});
  // SS-N: the wiring labels and the profile's terminal aliases (Settings::names is in the checker's order)
  static_assert(sizeof(s.names) / sizeof(s.names[0]) == SCRIPT_NAMED, "one name per nameable terminal");
  for (int k = 0; k < SCRIPT_NAMED; k++) {
    if (!s.names[k].empty()) b.names.push_back({(uint8_t)k, s.names[k]});
    for (auto& a : s.profile.aliases)
      if (a.kind == AliasEntry::Kind::Terminal && a.target == NAME_IDS[k]) b.names.push_back({(uint8_t)k, a.alias});
  }
  return b;
}

std::string scriptSubject(const std::string& m) {
  size_t a = m.find(": ");
  a = a == std::string::npos ? 0 : a + 2;
  static const char NO[] = "there is no ";
  if (m.compare(a, sizeof NO - 1, NO) == 0) {
    a += sizeof NO - 1;
    size_t b = m.find(" on this board", a);
    return m.substr(a, b == std::string::npos ? std::string::npos : b - a);
  }
  if (a < m.size() && m[a] == '"') {
    size_t b = m.find('"', a + 1);
    return b == std::string::npos ? m.substr(a + 1) : m.substr(a + 1, b - a - 1);
  }
  size_t b = m.find(' ', a);
  if (b != std::string::npos && m.compare(b, 2, " (") == 0) {
    size_t c = m.find(')', b);
    if (c != std::string::npos) b = c + 1;
  }
  return m.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

ScriptCheck Controller::checkScript(const std::string& text, const SoundList& sounds, ScriptProgram& table) const {
  ScriptBoard b = scriptBoardFor(settings_);
  patternBoard(b);  // the stored LED patterns' buttons (a press of one none of them has)
  // the table is the caller's; the text and sounds are the caller's or the stored script, which nothing changes
  // meanwhile (only the step runs, and it cannot change the script: window_lock.h). The step keeps running
  Unlocked u(*this);
  return scriptCompile(text, sounds, b, table);
}

SoundConfig Controller::soundConfig() const {
  SoundConfig c = settings_.sound();
  if (script_prog_) c.ambient = false;  // the script owns the player
  return c;
}

PT_STEP_CODE ScriptInputs Controller::scriptInputs() const {
  ScriptInputs in;
  for (int k = 0; k < INPUT_COUNT; k++) {
    in.closed[k] = has_closed_[k];
    in.since[k] = has_closed_[k] ? closed_since_[k] : opened_since_[k];
    // a test press that does not start things (remote start off) still reaches the effects that start remotely
    // without it (remote_free_effects): they see it joined with the pin
    if (!testStarts()) {
      in.test[k] = testHeld(k);
      in.test_since[k] = in.test[k] ? test_on_since_[k] : test_off_since_[k];
    }
  }
  return in;
}

void Controller::scriptInstall(std::unique_ptr<ScriptProgram> prog) {
  script_gen_++;
  for (int c = 0; c < CHANNEL_COUNT; c++) engine_.setScript(ALL_CHANNELS[c], false, 0);
  if (prog && prog->effects) {
    script_prog_ = std::move(prog);
    if (!script_run_) script_run_.reset(new ScriptRunner());
    script_run_->setTimes(settings_.trig_debounce_ms, settings_.trig_release_ms, boot_ms_ + SCRIPT_BOOT_LOCKOUT_MS);
    script_run_->load(script_prog_.get(), now_);
    scriptValues();  // after load: the runner no longer points at the table just freed
    for (int c = 0; c < CHANNEL_COUNT; c++)
      if (script_prog_->channels & (1u << c)) engine_.setScript(ALL_CHANNELS[c], true, 0);
  } else {
    script_prog_.reset();
    script_run_.reset();
  }
  patternScriptSlots();  // its pattern names to the stored patterns
  for (auto& c : sound_.scriptLoop(0, now_)) df_.command(c);  // a new table sets its own loop
  for (auto& c : sound_.setConfig(soundConfig(), now_)) df_.command(c);
  for (auto& n : script_runs_seen_) n = 0;  // a loaded table starts its effects' run counts at 0
  for (auto& n : script_remote_seen_) n = 0;
}

void Controller::setScriptStore(Storage* store) {
  script_store_ = store;
  if (!scriptLoad()) records_ok_ = false;
}

// Power-on (and after a failed save): load the stored script and check it again with the settings. False when
// a record is there but cannot be read (a new image then fails its self-check).
bool Controller::scriptLoad() {
  script_text_.clear();
  script_sounds_.clear();
  script_error_.clear();
  script_rewrite_ = false;
  script_newer_ = 0;
  script_unreadable_ = false;
  std::string rec;
  Stored got = script_store_ ? script_store_->fetch(rec) : Stored::None;
  if (got == Stored::None) { scriptInstall(nullptr); return true; }
  if (got == Stored::Invalid) {
    // both copies damaged (or the file system could not be opened): kept, as a record that cannot be read
    log("the stored show script could not be read: it is kept (factory reset clears it)");
    script_unreadable_ = true;
    scriptInstall(nullptr);
    return false;
  }
  std::unique_ptr<ScriptProgram> prog(new ScriptProgram());
  ScriptRecordRead r = scriptRecordDecode(rec, *prog);
  std::string().swap(rec);
  if (r.kind == ScriptRecordKind::Newer) {
    log("the stored script is from a newer firmware (record format " + std::to_string(r.version) + "): it does not run here and is kept");
    script_newer_ = r.version ? (int)r.version : 1;
    scriptInstall(nullptr);
    return false;
  }
  if (r.kind == ScriptRecordKind::Unreadable) {
    log("the stored show script could not be read: it is kept (factory reset clears it)");
    script_unreadable_ = true;
    scriptInstall(nullptr);
    return false;
  }
  std::string why;
  SoundList sounds;
  if (!soundListParse(r.sounds_json, sounds, why)) sounds.clear();
  if (r.text.empty()) { scriptInstall(nullptr); return true; }
  script_text_.swap(r.text);
  script_sounds_ = sounds;
  // the stored table, when it was checked against this firmware, these settings and this sound list
  if (r.has_table && r.key == scriptKey(script_sounds_)) {
    scriptInstall(std::move(prog));
    return true;
  }
  // otherwise the text is checked again (and the record written again with the new table)
  script_checks_at_boot++;
  ScriptCheck c = checkScript(script_text_, script_sounds_, *prog);
  if (!c.ok()) {
    // a firmware update can change the rules: the script is kept (the page shows it) but does not run
    script_error_ = c.errors[0].text;
    log("the stored show script does not pass the check and does not run: " + script_error_);
    scriptInstall(nullptr);
    return true;
  }
  scriptInstall(std::move(prog));
  script_rewrite_ = true;
  return true;
}

// A newer or unreadable script record is never overwritten or removed, and no record is written while a new
// image waits for its self-check (FLASH-MAP.md "Stored records").
std::string Controller::scriptHeldText() const {
  if (wiped_) return "a factory reset cleared the flash: the board is restarting";
  // a new image writes no record before its self-check: a rollback must find the records as they were
  if (update_target_ && update_target_->state() == BootState::Pending)
    return update_text::SELF_CHECK_PENDING;
  if (script_newer_)
    return "the stored show script is from a newer firmware (record format " + std::to_string(script_newer_) +
           ") and is kept: factory reset first, then set the script";
  if (script_unreadable_) return "the stored show script could not be read and is kept: factory reset first, then set the script";
  return "";
}

bool Controller::scriptSameTable(const ScriptProgram& t) const {
  return !script_prog_ || scriptTableEncode(t) == scriptTableEncode(*script_prog_);
}

uint32_t Controller::scriptKey(const SoundList& sounds) const {
  return scriptTableKey(scriptBoardFor(settings_), soundListJson(sounds), identity_.fw_version + "/" + identity_.fw_build);
}

// The stored record (script_record.h).
std::string Controller::scriptRecord(const std::string& text, const SoundList& sounds, const ScriptProgram* table) const {
  return scriptRecordEncode(text, soundListJson(sounds), table, scriptKey(sounds));
}

// A record read at power-on that must be written again (format 1, or a table checked again): once writing is
// allowed (not while a new image waits for its self-check: FLASH-MAP.md "Stored records").
void Controller::scriptRewrite() {
  if (!script_rewrite_ || !script_store_ || window_open_ || wiped_) return;  // window: from the step, during another write
  if (update_target_ && update_target_->state() == BootState::Pending) return;
  script_rewrite_ = false;
  if (script_text_.empty()) return;
  bool ok = false;
  {
    Unlocked u(*this);  // built from what only requests change and written without holding up the step
    std::string rec = scriptRecord(script_text_, script_sounds_, script_prog_.get());
    ok = !rec.empty() && rec.size() <= script_store_->capacity() && script_store_->save(rec);
  }
  if (!ok) {
    store_errors++;
    log("the show script could not be written again in the current record format");
  }
}

// Write the script record (an empty one when `empty`). The old script makes room first (its text, sounds
// and table) and lets go of its outputs; its runner is kept, detached from the freed table. The flash is written
// with the lock given back, so the step (and the 1 ms samples) keep running meanwhile, without a script.
// Only the step runs meanwhile: every other task that takes the lock waits until the record is written
// (window_lock.h), and a change asked from the step is refused (Controller::window_open_). A power cut keeps the old record
// (Storage::save is power-safe); a cut between the script and the settings save of put_backup is caught by the
// boot re-check (the table's key). A failed save loads the old script back from its store and says why.
bool Controller::scriptWrite(const std::string& text, const SoundList& sounds, const ScriptProgram* table, bool empty,
                             std::string& why) {
  script_rewrite_ = false;
  why = scriptHeldText();
  if (!why.empty()) return false;  // the stored record stays as it is (callers refuse before this)
  for (int c = 0; c < CHANNEL_COUNT; c++) engine_.setScript(ALL_CHANNELS[c], false, 0);
  if (script_run_) script_run_->load(nullptr, now_);  // never left pointing at the table freed next
  script_prog_.reset();
  script_gen_++;
  std::string().swap(script_text_);
  SoundList().swap(script_sounds_);
  if (!script_store_) return true;
  if (empty) {
    bool gone = false;
    {
      Unlocked u(*this);  // removing the record is a flash write too
      gone = script_store_->wipe();
    }
    if (gone) return true;
    store_errors++;
    (void)scriptLoad();
    why = "removing the script from flash failed";
    return false;
  }
  std::string rec = scriptRecord(text, sounds, table);
  if (rec.empty() || rec.size() > script_store_->capacity()) {
    rec.clear();
    (void)scriptLoad();
    why = "the script and its sound list do not fit the script store";
    return false;
  }
  bool ok = false;
  {
    Unlocked u(*this);  // the record is this call's own copy: written without holding up the step
    ok = script_store_->save(rec);
    std::string().swap(rec);
  }
  if (!ok) {
    store_errors++;
    (void)scriptLoad();
    why = "saving the script to flash failed";
    return false;
  }
  return true;
}

ScriptCheck Controller::saveScript(const std::string& text, const SoundList& sounds, std::string* store_error) {
  std::unique_ptr<ScriptProgram> prog(new ScriptProgram());
  ScriptCheck c = checkScript(text, sounds, *prog);
  if (!c.ok()) return c;
  bool empty = prog->effects == 0;
  std::string why;
  if (!scriptWrite(text, sounds, prog.get(), empty, why)) {
    if (store_error) *store_error = why;
    return c;
  }
  script_text_ = empty ? std::string() : text;
  script_sounds_ = empty ? SoundList() : sounds;
  script_error_.clear();
  scriptInstall(empty ? nullptr : std::move(prog));
  events_.push("script_changed");
  log(empty ? std::string("show script removed") : "show script saved: " + std::to_string(script_prog_->effects) + " effects");
  return c;
}

PT_STEP_CODE void Controller::scriptValues() {
  if (!script_run_) return;
  const Settings& st = settings_;
  const uint32_t v[SCRIPT_SETTING_COUNT] = {st.fx.out1_on_ms, st.fx.out2_on_ms, st.fx.out3_on_ms, st.fx.recover_time_ms,
                                            st.fx.cooldown_ms, st.trigger_out_pulse_ms, st.role_pulse_ms, st.fx.runs_per_cooldown};
  script_run_->setSettings(v);
  script_run_->setPaused(engine_.isPaused());
  script_run_->setRemote(st.fx.remote_trigger_enable, scriptFreeEffects());
  for (int k = 0; k < 2; k++) script_run_->setReadyRule(k, !st.trig_wait_return[k]);
}

PT_STEP_CODE bool Controller::scriptEffectRunning() const {
  if (!script_prog_ || !script_run_) return false;
  for (int e = 0; e < script_prog_->effects; e++)
    if (script_run_->inRun(e)) return true;  // an effect with no wait for going round never stops: not a run
  return false;
}

PT_STEP_CODE void Controller::scriptTick(Millis now) {
  if (!booted_) {
    booted_ = true;
    boot_ms_ = now;
    if (script_run_) script_run_->setTimes(settings_.trig_debounce_ms, settings_.trig_release_ms, boot_ms_ + SCRIPT_BOOT_LOCKOUT_MS);
  }
  bool want_dmx = dmx_ && settings_.dmx_s4 && script_prog_ && script_prog_->dmx;
  if (want_dmx != dmx_on_) {
    dmx_on_ = want_dmx;
    if (dmx_) { if (dmx_on_) dmx_->start(); else dmx_->stop(); }
    dmx_at_ = now;
  }
  if (!script_run_ || !script_prog_) return;  // no table: a new script is being written (scriptWrite)
  scriptValues();
  std::vector<SoundCommand> snd;
  if (!engine_.isPaused()) script_run_->tick(now, scriptInputs(), snd);  // nothing runs while paused
  if (script_run_->tellsWaiting()) link_work_.store(true, std::memory_order_relaxed);  // for the network loop (linkTake)
  // an effect that left its first `wait for` started a run
  for (int e = 0; e < script_prog_->effects && e < SCRIPT_MAX_EFFECTS; e++)
    while (script_runs_seen_[e] < script_run_->runs(e)) {
      script_runs_seen_[e]++;
      std::string detail;
      const ScriptEffect& fe = script_prog_->fx[e];
      for (int i = 0; i < fe.count; i++) {
        const ScriptStep& w = script_prog_->step[fe.first + i];
        if (w.op != ScriptStep::Op::Wait) continue;
        if (w.target < INPUT_COUNT) detail = name(ALL_INPUTS[w.target]);
        break;
      }
      // a remote start (web, MQTT, serial, another board's tell) is an `api` run, as for a show (PROTOCOL.md)
      const bool remote = script_remote_seen_[e] < script_run_->remoteRuns(e);
      if (remote) script_remote_seen_[e]++;
      countRun(now, remote ? "api" : "input", "script", std::string(script_prog_->fx[e].name), detail);
    }
  for (int c = 0; c < CHANNEL_COUNT; c++)
    if (script_prog_->channels & (1u << c)) engine_.setScript(ALL_CHANNELS[c], true, script_run_->level(c));
  for (auto& c : snd) {
    if (c.kind == SoundCommand::Kind::LoopFolder || c.kind == SoundCommand::Kind::LoopMp3 || c.kind == SoundCommand::Kind::LoopIndex) {
      // audio_loop: the background loop (0: off), resumed by itself after a sound played over it (SS-R7)
      for (auto& x : sound_.scriptLoop(c.a, now, soundLoopMode(c))) df_.command(x);
      continue;
    }
    if (c.kind == SoundCommand::Kind::Volume) {
      // audio_volume: the player's volume now, and for the script's sounds from here on (the ambient and trigger
      // sounds keep their settings)
      sound_.script_volume = c.a;
      df_.command(c);
      continue;
    }
    if (c.kind == SoundCommand::Kind::Stop)
      for (auto& x : sound_.scriptLoop(0, now)) (void)x;  // audio_stop: the loop goes too
    // audio_single's volume: the script's audio_volume once it set one, else trigger_volume
    if (c.startsPlayback()) df_.command(SoundCommand::volume(sound_.script_volume >= 0 ? (uint8_t)sound_.script_volume : settings_.trigger_volume));
    sound_.observeShow(c, now);
    df_.command(c);
  }
  if (dmx_on_ && now - dmx_at_ >= DMX_FRAME_MS) {
    // on the 10 ms tick: frames 20 or 30 ms apart, 25 ms on average (40 a second)
    dmx_at_ += DMX_FRAME_MS;
    if (now - dmx_at_ >= DMX_FRAME_MS) dmx_at_ = now;
    dmx_->send(script_run_->dmx(), DMX_CHANNELS);
    dmx_frames++;
  }
}

bool Controller::isFactoryExample(const Show& s) {
  Show ex;
  std::string e;
  if (!showFromString(FACTORY_EXAMPLE_SHOW, ex, e)) return false;
  return showToString(s) == showToString(ex);
}

static bool scriptArgs(const Request& r, std::string& text, SoundList& sounds, std::string& why) {
  text = r["text"] | "";
  std::string sj;
  if (!r["sounds"].isNull()) serializeJson(r["sounds"], sj);
  return soundListParse(sj, sounds, why);
}

Response Controller::scriptOp(Request& r) {
  if (r.op == "get_script") {
    Response resp = Response::ok(r, "script");
    JsonObject o = resp.result();
    o["text"] = script_text_;
    JsonDocument s;
    deserializeJson(s, soundListJson(script_sounds_));
    o["sounds"] = s.as<JsonObjectConst>();
    std::unique_ptr<ScriptProgram> table(new ScriptProgram());  // 3.4 KB: on the heap, not the request's stack
    ScriptCheck c = checkScript(script_text_, script_sounds_, *table);
    scriptReplyJson(c, table->effects, o["check"].to<JsonObject>());
    return resp;
  }
  std::string text, why;
  SoundList sounds;
  if (!scriptArgs(r, text, sounds, why)) return Response::err(r, ErrorCode::BadRequest, why);
  // "bytes": the text's length in UTF-8 as the sender counted it. A link that lost bytes (an overrun serial
  // buffer) can still deliver valid JSON with a shorter text: refused, never checked or stored
  if (!r["bytes"].isNull() && r["bytes"].as<uint64_t>() != text.size())
    return Response::err(r, ErrorCode::BadRequest, "the script arrived with " + std::to_string(text.size()) + " of " +
                                                       std::to_string(r["bytes"].as<uint64_t>()) + " bytes: nothing was stored; send it again");
  Response resp = Response::ok(r, "script_check");
  if (r.op == "check_script") {
    std::unique_ptr<ScriptProgram> table(new ScriptProgram());
    ScriptCheck c = checkScript(text, sounds, *table);
    scriptReplyJson(c, table->effects, resp.result());
    return resp;
  }
  std::string held = scriptHeldText();
  if (!held.empty()) return Response::err(r, ErrorCode::Refused, held);
  std::string store_error;
  ScriptCheck c = saveScript(text, sounds, &store_error);
  if (!store_error.empty()) return Response::err(r, ErrorCode::Internal, store_error);
  scriptReplyJson(c, script_prog_ ? script_prog_->effects : 0, resp.result());
  return resp;
}

}  // namespace pt
