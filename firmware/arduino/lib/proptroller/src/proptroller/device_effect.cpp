// The generic controller's pulse-and-recover effect (its own state machine beside the show engine) and the
// profile's aliases (a project's own names for keys, controls, status values and terminals). See
// firmware/STATE-MACHINE.md on the main branch.
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "device.h"

namespace pt {

static const char* const DEFAULT_TOPIC_ROOT = "proptroller";

// ---------------------------------------------------------------- the effect

void Controller::effectSync() {
  if (settings_.effect_on && !fx_) {
    fx_.reset(new EffectMachine(settings_.fx, now_));
  } else if (!settings_.effect_on && fx_) {
    fx_.reset();
    settings_.fx.remote_trigger_enable = false;
    fx_by_.clear();
    fx_session_ = 0;
  }
}

bool Controller::effectOwns(Channel c) const {
  if (!fx_) return false;
  return c == Channel::Out1 || c == Channel::Out2 || (c == Channel::Out3 && settings_.fx.out3_enabled) ||
         (c == Channel::Out4 && settings_.out4_ready_lamp);
}

bool Controller::effectOwns(Input i) const { return fx_ && (i == Input::Trig1 || i == Input::Trig2); }

std::string Controller::effectOwnsShow(const Show& s) const {
  if (!fx_) return "";
  for (auto& t : s.tracks)
    if (t.target.isChannel() && effectOwns(t.target.channel) && !t.cues.empty())
      return terminalDisplay(name(t.target.channel)) + " belongs to the pulse-and-recover effect (Settings \xe2\x86\x92 Effect)";
  for (auto& t : s.triggers)
    if ((t.on == ShowTrigger::Kind::OnInput || t.on == ShowTrigger::Kind::OnRelease) && effectOwns(t.input))
      return terminalDisplay(name(t.input)) + " belongs to the pulse-and-recover effect (Settings \xe2\x86\x92 Effect)";
  return "";
}

void Controller::effectSample(Millis now, const bool raw[INPUT_COUNT]) {
  if (!fx_ || engine_.isPaused()) return;  // nothing runs while paused
  if (recover_lease_ && now >= recover_lease_until_) {
    recover_lease_ = false;
    fx_->recoverRelease();
  }
  effectEvents(now, fx_->tick(now, settings_.fx, raw[0]));
}

void Controller::effectEvents(Millis now, const std::vector<EffectEvent>& evs) {
  for (auto& e : evs) {
    char b[96];
    switch (e.kind) {
      case EffectEvent::Kind::Started:
        // every run, whatever started it, pulses the trigger out and sound out roles and plays the trigger
        // sound (SM-F5)
        pulseRole(now, OutputRole::TriggerRepeat, settings_.trigger_out_pulse_ms);
        pulseRole(now, OutputRole::SoundTrigger, settings_.trigger_out_pulse_ms);
        for (auto& c : sound_.trigger()) df_.command(c);
        (void)snprintf(b, sizeof b, "run #%u (%s)", (unsigned)e.value, e.remote ? "remote" : terminalDisplay("TRIG1").c_str());
        log(b);
        countRun(now, e.remote ? "api" : "input", "effect", "pulse_and_recover", e.remote ? "" : "TRIG1");
        break;
      case EffectEvent::Kind::RecoveryStarted:
        (void)snprintf(b, sizeof b, "recovery %u ms", (unsigned)e.value);
        log(b);
        break;
      case EffectEvent::Kind::RecoverStarted: log("recover"); break;  // a remembered recover starting
      case EffectEvent::Kind::SafetyOff: log("safety off"); break;
      case EffectEvent::Kind::None:
      default: break;
    }
  }
}

static std::string mmss(uint64_t ms) {
  uint64_t s = (ms + 999) / 1000;
  char b[24];
  (void)snprintf(b, sizeof b, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  return b;
}

std::string Controller::refusedText(EffectRefused why) const {
  switch (why) {
    case EffectRefused::CooldownFull:
      return "cooling down, " + mmss(fx_ ? fx_->cooldownRemaining(fxNow()) : 0) + " left (" + std::to_string(settings_.fx.runs_per_cooldown) +
             " runs per cooldown used)";
    case EffectRefused::Boot: return "starting up: the effect is ready 2 s after power-on";
    case EffectRefused::Safety: return "the safety is on: hold TRIG1 2 s, or set auto_arm true";
    case EffectRefused::Busy: return "a run or its recovery is in progress";
    case EffectRefused::Recovering: return "a recover is in progress: wait for it to finish";
    case EffectRefused::RemoteDisabled: return "the remote trigger is not enabled (Settings \xe2\x86\x92 Effect \xe2\x86\x92 Remote trigger; it is off after every restart)";
    case EffectRefused::AlreadyQueued: return "already queued: one run is waiting for the effect to be ready";
    case EffectRefused::None:
    default: return "";
  }
}

Response Controller::effectAction(const Request& r, const std::string& control) {
  // the serial console and MQTT come here directly: no run starts while a firmware update runs
  if (updating()) return Response::err(r, ErrorCode::Refused, update_text::IN_UPDATE_MODE);
  // a web button sends down:true while held and down:false on release: holding it does not repeat (SM-Q2)
  bool has_down = !r["down"].isNull();
  if (has_down && !r["down"].as<bool>()) {
    if (control == "recover") {
      // a held recover from the web ends with the button's release
      recover_lease_ = false;
      if (fx_) fx_->recoverRelease();
    }
    return Response::done(r);
  }
  if (control == "recover" && has_down && fx_ && settings_.fx.recover_mode == RecoverMode::Hold) {
    // the web page repeats down:true while the button is held: a lease keeps the held recover going
    bool running = fx_->recover != EffectMachine::RecoverRun::NoRecover;
    recover_lease_ = true;
    recover_lease_until_ = fxNow() + EFFECT_ACTION_LEASE_MS;
    if (running) return Response::done(r);
  }
  if (control == "restart") {
    if (dirty_) saveNow();
    runsSaveNow();
    reboot_ = true;
    return Response::done(r);
  }
  // a show script's effect, by its name (SS-R10)
  if (control.compare(0, 7, "script:") == 0) return scriptStart(r, atoi(control.c_str() + 7));
  if (control == "cancel" && !fx_ && script_run_ && script_prog_) {
    if (script_run_->cancelRemote()) log("queued start cancelled");
    return Response::done(r);
  }
  if (!fx_) return Response::err(r, ErrorCode::NotFound, "no such action");  // the other actions are the effect's
  if (control == "cancel") {
    if (fx_->cancelRemote()) log("queued run cancelled");
    return Response::done(r);
  }
  std::string why;
  if (control == "trigger") why = doTrigger();
  else if (control == "recover") why = doRecover();
  else return Response::err(r, ErrorCode::NotFound, "no such action");
  if (!why.empty()) return Response::err(r, ErrorCode::Refused, why);
  return Response::done(r);
}

std::string Controller::doTrigger() {
  EffectEvent e;
  EffectRefused why = fx_->remoteTrigger(fxNow(), settings_.fx, e);
  if (why != EffectRefused::None) {
    std::string t = refusedText(why);
    log("remote trigger refused: " + t);
    return t;
  }
  if (e.kind != EffectEvent::Kind::None) effectEvents(fxNow(), {e});
  else log("remote run queued: it starts when the effect is ready");
  return "";
}

std::string Controller::doRecover() {
  bool queued = false;
  EffectRefused why = fx_->recoverPress(fxNow(), settings_.fx, queued);
  if (why == EffectRefused::Recovering) return "";  // a second press stops a timed / held recover
  if (why != EffectRefused::None) {
    std::string t = refusedText(why);
    log("recover refused: " + t);
    return t;
  }
  if (queued) log("recover remembered: it starts when the cycle is done");
  else log("recover");
  return "";
}

void Controller::statusNums(StatusNums& n) const {
  Millis now = now_;
  n = StatusNums();
  n.remote = settings_.fx.remote_trigger_enable;
  if (n.remote) n.by = fx_by_;
  if (fx_) {
    const EffectMachine& m = *fx_;
    n.any = n.fx = true;
    n.run_count = m.run_count;
    n.runs_left = m.runsLeft(now);
    n.cooldown_ms = m.cooldownRemaining(now);
    n.pulsing = m.pulsing();
    n.recovering = m.recovering();
    n.ready = m.ready(now);
    n.queued = m.queued_remote;
    return;
  }
  if (!script_run_ || !script_prog_ || !script_prog_->effects) return;
  n.any = true;
  n.run_count = script_run_->totalRuns();
  n.has_limit = script_run_->hasLimit(0);
  if (n.has_limit) {
    n.runs_left = script_run_->runsLeft(0, now);
    n.cooldown_ms = script_run_->cooldownRemaining(0, now);
  }
  bool pulsing = board_.levels()[index(Channel::Out1)] > 0;
  for (int e = 0; e < script_prog_->effects; e++) pulsing |= script_run_->beforeRecovery(e);
  n.pulsing = pulsing;
  n.recovering = board_.levels()[index(Channel::Out2)] > 0;
  n.ready = script_run_->ready(0, now);
  n.queued = script_run_->anyQueued();
}

void Controller::statusLines(const StatusNums& n, std::vector<ParamValue>& out, bool aliased) const {
  if (!n.any) return;
  auto add = [&](const char* k, const Value& v) { out.push_back(ParamValue{aliased ? aliasFor(AliasEntry::Kind::Status, k) : std::string(k), v}); };
  add("run_count", Value::integer(n.run_count));
  if (n.fx || n.has_limit) {
    add("runs_left", Value::integer(n.runs_left));
    add("cooldown_remaining_ms", Value::integer(n.cooldown_ms));
  }
  add("pulsing", Value::boolean(n.pulsing));
  add("recovering", Value::boolean(n.recovering));
  add("ready", Value::boolean(n.ready));
  add("remote_trigger_enable", Value::boolean(n.remote));
  add("remote_trigger_by", Value::text(n.by));
  add("queued", Value::boolean(n.queued));
}

void Controller::effectStatus(std::vector<ParamValue>& out, bool aliased) const {
  StatusNums n;
  statusNums(n);
  statusLines(n, out, aliased);
}

std::string Controller::callFrom(uint32_t session, const char* body, size_t len) {
  Origin o = origin_;
  uint32_t s = session_;
  setOrigin(Origin::Session, session);
  std::string out = call(body, len);
  setOrigin(o, s);
  return out;
}

void Controller::sessionClosed(uint32_t session) {
  if (!session) return;
  // what a phone page turned on or started ends with it: manual outputs, a pixel test
  for (int i = 0; i < CHANNEL_COUNT; i++)
    if (manual_session_[i] == session) { engine_.setManual(ALL_CHANNELS[i], false, 0); manual_session_[i] = 0; }
  if (pixel_session_ == session) { engine_.setManualPixel(nullptr); pixel_session_ = 0; }
  for (int k = 0; k < INPUT_COUNT; k++)
    if (test_session_[k] == session) testRelease(k, fxNow());
  // an enable given from a phone page ends with that page (SM-R2)
  if (fx_session_ == session && settings_.fx.remote_trigger_enable) {
    settings_.fx.remote_trigger_enable = false;
    fx_by_.clear();
    fx_session_ = 0;
    if (fx_) fx_->cancelRemote();
    if (script_run_) script_run_->cancelRemote();
    log("remote trigger off: the phone page that turned it on is gone");
    events_.push("params_changed");
  }
}

// ---------------------------------------------------------------- a show script's effects started remotely (SS-R10)

// The effect of the stored script with that control name (its name; "_" for a space), or -1.
int Controller::scriptEffect(const std::string& name) const {
  return script_run_ && script_prog_ ? script_run_->effectNamed(name) : -1;
}

// remote_free_effects as a bit per effect of the stored script
PT_STEP_CODE uint8_t Controller::scriptFreeEffects() const {
  uint8_t m = 0;
  if (!script_run_ || !script_prog_) return m;
  const std::string& list = settings_.remote_free_effects;
  // asked on every 1 ms sample: worked out again only when the list or the script changed
  if (free_gen_ == script_gen_ && free_list_ == list) return free_mask_;
  size_t a = 0;
  while (a <= list.size()) {
    size_t b = list.find(',', a);
    if (b == std::string::npos) b = list.size();
    int e = script_run_->effectNamed(list.substr(a, b - a));
    if (e >= 0) m = (uint8_t)(m | (1u << e));
    a = b + 1;
  }
  free_gen_ = script_gen_;
  free_list_ = list;
  free_mask_ = m;
  return m;
}

Response Controller::scriptStart(const Request& r, int e) {
  Millis now = fxNow();
  bool free = (scriptFreeEffects() >> e) & 1;
  ScriptRunner::Answer a = script_run_->remoteStart(e, now, free || settings_.fx.remote_trigger_enable);
  std::string fxname = std::string(script_prog_->fx[e].name);
  switch (a) {
    case ScriptRunner::Answer::Starts: log("remote start: effect \"" + fxname + "\""); return Response::done(r);
    case ScriptRunner::Answer::Queued:
      log("remote start queued: effect \"" + fxname + "\" starts when it can");
      return Response::done(r);
    case ScriptRunner::Answer::AlreadyQueued: return Response::err(r, ErrorCode::Refused, refusedText(EffectRefused::AlreadyQueued));
    case ScriptRunner::Answer::Disabled: return Response::err(r, ErrorCode::Refused, refusedText(EffectRefused::RemoteDisabled));
    case ScriptRunner::Answer::CooldownFull: {
      std::string t = "cooling down, " + mmss(script_run_->cooldownRemaining(e, now)) + " left (effect \"" + fxname + "\" has used its runs)";
      log("remote start refused: " + t);
      return Response::err(r, ErrorCode::Refused, t);
    }
    case ScriptRunner::Answer::NoEffect:
    default: return Response::err(r, ErrorCode::NotFound, "no such action");
  }
}

// The effect's status values from the stored script (the same keys, so a profile's status aliases work):
// run_count counts every effect's runs; ready, runs_left and cooldown_remaining_ms are the first effect's;
// pulsing: OUT1 on, or a run that turns OUT2 on (its recovery) has not reached that line yet (a recover cycle's
// OUT1 interval counts, as the pulse-and-recover effect's firmware once reported it); recovering: OUT2 on (SM-F6);
// queued: a remote start waits (SS-R10).
// ---------------------------------------------------------------- aliases

std::string Controller::aliasFor(AliasEntry::Kind k, const std::string& target) const {
  for (auto& a : settings_.profile.aliases)
    if (a.kind == k && a.target == target) return a.alias;
  return target;
}

const AliasEntry* Controller::aliasNamed(const std::string& alias) const {
  for (auto& a : settings_.profile.aliases)
    if (a.kind != AliasEntry::Kind::Terminal && a.alias == alias) return &a;
  return nullptr;
}

std::string Controller::terminalName(const std::string& n) const {
  std::string u = n;
  for (auto& c : u) c = (char)toupper((unsigned char)c);
  for (auto& a : settings_.profile.aliases)
    if (a.kind == AliasEntry::Kind::Terminal && a.alias == u) return a.target;
  return n;
}

std::string Controller::terminalDisplay(const std::string& board_name) const {
  for (auto& a : settings_.profile.aliases)
    if (a.kind == AliasEntry::Kind::Terminal && a.target == board_name) return a.alias + " (" + board_name + ")";
  return board_name;
}

std::string Controller::topicRoot() const {
  return settings_.profile.topic_prefix.empty() ? std::string(DEFAULT_TOPIC_ROOT) : settings_.profile.topic_prefix;
}

// A value given under a key alias, as the key's value; false with the reason in the alias's units.
static bool aliasValue(const AliasEntry& a, const ParamDesc& d, Value& v, std::string& why) {
  if (a.invert && v.type == Value::Type::Bool) v.b = !v.b;
  if (a.scale && v.type == Value::Type::Int) {
    int64_t lo = aliasMin(d.min, a.scale), hi = aliasMax(d.max, a.scale);
    if (v.i < lo || v.i > hi) {
      std::string u = a.unit.empty() ? "" : " " + a.unit;
      why = a.alias + ": " + (a.label.empty() ? d.label : a.label) + " is set to " + std::to_string((long long)v.i) + u + " but the board allows " +
            std::to_string((long long)lo) + "-" + std::to_string((long long)hi) + u + "; " + (v.i > hi ? "lower" : "raise") + " it in Settings \xe2\x86\x92 " + (a.group.empty() ? d.group : a.group);
      return false;
    }
    v.i *= a.scale;
  }
  return true;
}
// The key's value as shown under an alias.
static Value aliasShown(const AliasEntry& a, Value v) {
  if (a.invert && v.type == Value::Type::Bool) v.b = !v.b;
  if (a.scale && v.type == Value::Type::Int) v.i /= a.scale;
  return v;
}

bool Controller::aliasIn(const Profile& p, const char* body, size_t len, std::string& out, std::string& why) {
  if (p.aliases.empty()) { out.assign(body, len); return true; }
  JsonDocument d;
  if (deserializeJson(d, body, len) || !d.is<JsonObject>()) { out.assign(body, len); return true; }  // the parser reports it
  JsonObject o = d.as<JsonObject>();
  // as terminalName() and aliasNamed(), on `p`
  auto terminalName = [&](const std::string& n) {
    std::string u = n;
    for (auto& c : u) c = (char)toupper((unsigned char)c);
    for (auto& a : p.aliases)
      if (a.kind == AliasEntry::Kind::Terminal && a.alias == u) return a.target;
    return n;
  };
  auto aliasNamed = [&](const std::string& alias) -> const AliasEntry* {
    for (auto& a : p.aliases)
      if (a.kind != AliasEntry::Kind::Terminal && a.alias == alias) return &a;
    return nullptr;
  };
  auto term = [&](JsonVariant v) {
    if (v.is<const char*>()) v.set(terminalName(v.as<std::string>()));
  };
  for (const char* k : {"channel", "input", "target"}) term(o[k]);
  if (o["data"].is<JsonObject>()) {
    JsonArray tracks = o["data"]["tracks"], triggers = o["data"]["triggers"];
    for (JsonVariant t : tracks) term(t["target"]);
    for (JsonVariant t : triggers) term(t["input"]);
  }
  std::string op = o["op"] | "";
  if (op == "set_params") {
    JsonArray values = o["values"];
    std::vector<std::pair<std::string, Value>> more;  // the keys of "also" lists, set with the same value
    for (size_t i = 0; i < values.size();) {
      JsonVariant pv = values[i];
      const AliasEntry* a = aliasNamed(pv["key"] | "");
      if (a && a->kind == AliasEntry::Kind::Fixed) { values.remove(i); continue; }  // accepted, changes nothing
      i++;
      if (!a || a->kind != AliasEntry::Kind::Key) continue;
      const ParamDesc* desc = Settings::find(a->target);
      Value v;
      if (!desc || !Value::fromJson(pv["value"], v)) continue;
      if (!aliasValue(*a, *desc, v, why)) return false;
      pv["key"] = a->target;
      v.toJson(pv.as<JsonObject>(), "value");
      for (auto& k : a->also) more.push_back({k, v});
    }
    for (auto& m : more) {
      JsonObject x = values.add<JsonObject>();
      x["key"] = m.first;
      m.second.toJson(x, "value");
    }
  }
  out.clear();
  serializeJson(d, out);
  return true;
}

void Controller::noticeOut(const Notice& n) { events_.notice(n); }  // board names; aliased presses log separately

// ---------------------------------------------------------------- serial console

std::string Controller::callSerial(const char* body, size_t len) {
  Origin o = origin_;
  uint32_t s = session_;
  setOrigin(Origin::Serial);
  std::string out = call(body, len);
  setOrigin(o, s);
  return out;
}

// A secret on the console: whether it is set, never its value.
static std::string secretShown(const Settings& st, const std::string& key) {
  bool set = key == "pin" ? !st.pin_hash.empty() : key == "wifi_pass" ? !st.wifi_pass.empty() : key == "ap_pass" ? !st.ap_pass.empty() : false;
  return set ? "(set)" : "(empty)";
}

// The console: the answer without its final line end;
// a several-line answer (status) has CR LF between its lines.
static const char* const NL = "\r\n";

// The name of a control: a show-script effect's name first ("script:<index>", SS-R10), then trigger, recover, cancel,
// restart, or a control alias; "" when none.
std::string Controller::resolveControl(const std::string& name) const {
  int e = scriptEffect(name);
  if (e >= 0) return "script:" + std::to_string(e);
  for (auto c : EFFECT_CONTROLS)
    if (name == c) return name;
  const AliasEntry* a = aliasNamed(name);
  return a && a->kind == AliasEntry::Kind::Control ? a->target : std::string();
}

// A value set under `a` (a key alias, or null for the key itself): the key, then the alias's "also" keys.
std::vector<ParamValue> Controller::withAlso(const AliasEntry* a, const std::string& key, const Value& v) {
  std::vector<ParamValue> out{ParamValue{key, v}};
  if (a && a->kind == AliasEntry::Kind::Key)
    for (auto& k : a->also) out.push_back(ParamValue{k, v});
  return out;
}

// A setting's key from its name (the key or a key alias), or "".
std::string Controller::resolveKey(const std::string& name) const {
  if (Settings::find(name)) return name;
  const AliasEntry* a = aliasNamed(name);
  return a && a->kind == AliasEntry::Kind::Key ? a->target : std::string();
}

// A status value by its name (a status key or a status alias); false when there is none (an effect value while
// the effect is off).
bool Controller::statusRead(const std::string& name, Value& out) const {
  std::string key;
  for (auto k : EFFECT_STATUS)
    if (name == k) key = name;
  for (auto k : RUN_STATUS)
    if (name == k) key = name;
  if (key.empty()) {
    const AliasEntry* a = aliasNamed(name);
    if (a && a->kind == AliasEntry::Kind::Status) key = a->target;
  }
  if (key.empty()) return false;
  std::vector<ParamValue> st;
  effectStatus(st, false);
  runStatus(st, false);  // the run counters are there with the effect off too
  for (auto& pv : st)
    if (pv.key == key) { out = pv.value; return true; }
  return false;
}

// A setting by its name, in the alias's terms (inverted, scaled).
bool Controller::getOut(const std::string& name, Value& out) const {
  std::string key = resolveKey(name);
  if (key.empty() || !settings_.get(key, out)) return false;
  const AliasEntry* a = aliasNamed(name);
  if (a && a->kind == AliasEntry::Kind::Key) out = aliasShown(*a, out);
  return true;
}

std::string Controller::consoleLine(const std::string& line_in) {
  std::string line = line_in;
  while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
  while (!line.empty() && isspace((unsigned char)line.front())) line.erase(line.begin());
  if (line.empty()) return "";
  Origin o = origin_;
  setOrigin(Origin::Serial);
  std::string out = consoleAnswer(line);
  setOrigin(o, session_);
  return out;
}

std::string Controller::consoleAnswer(const std::string& line) {
  size_t eq = line.find('=');
  std::string name = line.substr(0, eq), val;
  bool has_val = eq != std::string::npos;
  auto trimmed = [](std::string x) {
    while (!x.empty() && isspace((unsigned char)x.back())) x.pop_back();
    while (!x.empty() && isspace((unsigned char)x.front())) x.erase(x.begin());
    return x;
  };
  name = trimmed(name);
  if (has_val) val = trimmed(line.substr(eq + 1));
  Request req;
  if (!has_val) {
    if (name == "help") return "key, key=value, trigger, recover, cancel, restart, status, settings, mem, help";
    if (name == "mem") return mem_probe_ ? mem_probe_() : "error: this board does not measure its memory";
    if (name == "status") {
      std::vector<ParamValue> st;
      effectStatus(st);
      runStatus(st);
      std::string s;
      for (auto& pv : st) s += (s.empty() ? "" : NL) + pv.key + "=" + pv.value.plain();
      return s;
    }
    if (name == "settings") {
      // as get_params shows them: an aliased setting under its alias, in its units; secrets blank
      JsonDocument d;
      JsonArray arr = d.to<JsonArray>();
      for (auto& pv : settings_.values()) {
        std::string k = aliasFor(AliasEntry::Kind::Key, pv.key);
        const AliasEntry* a = k == pv.key ? nullptr : aliasNamed(k);
        JsonObject x = arr.add<JsonObject>();
        x["key"] = k;
        (a ? aliasShown(*a, pv.value) : pv.value).toJson(x, "value");
      }
      std::string s;
      serializeJson(d, s);
      return s;
    }
    std::string action = resolveControl(name);
    if (!action.empty()) {
      if (action == "restart") {
        effectAction(req, "restart");
        return "restarting";
      }
      Response r = effectAction(req, action);
      return r.isOk() ? "ok" : "error: " + r.errorMessage();
    }
    Value sv;
    if (statusRead(name, sv)) return name + "=" + sv.plain();
  }
  const AliasEntry* fixed = aliasNamed(name);
  if (fixed && fixed->kind == AliasEntry::Kind::Fixed) return name + "=" + fixed->target;  // a set changes nothing
  std::string key = resolveKey(name);
  if (key.empty()) return "error: unknown command or setting \"" + name + "\" (type help)";
  const ParamDesc* d = Settings::find(key);
  if (!d) return "error: unknown setting";
  if (!has_val && d->isSecret()) return name + "=" + secretShown(settings_, key);
  if (!has_val) {
    Value v;
    if (!getOut(name, v)) return "error: unknown setting";
    return name + "=" + v.plain();
  }
  Value v;
  std::string e;
  if (!d->parsePlain(val, v, e)) return "error: " + name + ": " + e;
  const AliasEntry* a = aliasNamed(name);
  if (a && a->kind == AliasEntry::Kind::Key && !aliasValue(*a, *d, v, e)) return "error: " + e;
  Response r = setParams(req, withAlso(a, key, v));
  if (!r.isOk()) return "error: " + r.errorMessage();
  if (d->isSecret()) return name + "=(set)";
  Value now;
  if (!getOut(name, now)) return "ok";
  return name + "=" + now.plain();
}

}  // namespace pt
