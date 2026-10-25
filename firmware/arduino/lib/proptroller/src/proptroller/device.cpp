#include "device.h"
#include <algorithm>
#include <ctype.h>
#include "password.h"
#include "script_record.h"
#include "storage_ab.h"
#include <stdio.h>
#include <string.h>

namespace pt {

const char* const EXAMPLE_SHOW = R"({
  "id": 1, "name": "Example show: door knock",
  "triggers": [{"on": "input", "input": "TRIG1"}],
  "cooldown_ms": 3000,
  "tracks": [
    {"target": "OUT1", "cues": [{"at": 0, "do": "pulse", "ms": 2000}]},
    {"target": "PIXEL", "cues": [{"at": 0, "do": "flash", "color": "#ffffff", "period_ms": 200}, {"at": 2000, "do": "off"}]}
  ]
})";
// the example earlier firmware stored on every fresh board (removed at power-on when untouched)
const char* const FACTORY_EXAMPLE_SHOW = R"({
  "id": 1, "name": "Example: door knock",
  "triggers": [{"on": "input", "input": "TRIG1"}],
  "cooldown_ms": 3000,
  "tracks": [
    {"target": "OUT1", "cues": [{"at": 0, "do": "pulse", "ms": 2000}]},
    {"target": "PIXEL", "cues": [{"at": 0, "do": "flash", "color": "#ffffff", "period_ms": 200}, {"at": 2000, "do": "off"}]}
  ]
})";

static const Millis SAVE_DELAY_MS = 2000, SAVE_MAX_DELAY_MS = 60000;
// flash wear: at most one settings write per SAVE_MIN_GAP_MS (a change is still saved at the latest
// SAVE_MAX_DELAY_MS after it), and a record that did not change is never written again
static const Millis SAVE_MIN_GAP_MS = 30000;
static const Millis PIN_WAIT_MS = 1000, PIN_WAIT_MAX_MS = 30000;  // after a wrong page password
static const size_t PIN_MIN_LEN = 6;
// the stored shows are limited by bytes as well as by number (MAX_SHOWS)
static const char* const PAUSED_TEXT = "everything is paused: release the pause input or press Resume";
static const char* const STORAGE_FULL =
    "storage full: the shows and settings would take more than the board's 12 KB (up to 16 shows, fewer when they are long). Delete a show or remove steps";
static const Millis PIXEL_PERIOD_MS = 20;

DfConfig dfConfig(const Settings& s, bool boot) {
  DfConfig c;
  c.use_ack = s.df_ack;
  c.checksum = s.df_checksum;
  if (!boot) c.init_delay_ms = 0;
  return c;
}

Controller::Controller(BoardPins& pins, ByteStream& uart, PixelOut& pixels, Storage* store, const Identity& id)
    : board_(pins), uart_(uart), pixels_(pixels), store_(store), identity_(id) {
  // outputs are off (Board's constructor); now settings and shows
  std::vector<Show> shows;
  std::string rec;
  Stored got = store_ ? store_->fetch(rec) : Stored::None;
  fresh_ = got == Stored::None;
  if (got == Stored::Invalid) {
    // both copies damaged, or the file system could not be opened (and holds data): kept, nothing is written
    // until a factory reset
    records_ok_ = false;
    store_unreadable_ = true;
    log("the stored settings could not be read: running on the defaults and keeping that record (factory reset clears it)");
  } else if (got == Stored::Ok) {
    // read with migrations from every older format; a newer one is kept and never overwritten (FLASH-MAP.md
    // "Stored records"); a record that is there but cannot be read fails a new image's self-check
    JsonDocument d;
    uint32_t v = 0;
    RecordKind k = readRecord(rec, STORED_VERSION, STORED_MIGRATIONS, STORED_MIGRATION_COUNT, d, v);
    stored_crc_ = crc32((const uint8_t*)rec.data(), rec.size());
    stored_crc_known_ = true;
    std::string().swap(rec);
    if (k == RecordKind::Current || k == RecordKind::Migrated) {
      store_migrated_ = k == RecordKind::Migrated;
      settings_.fromJson(d["settings"]);
      // a factory reset that kept the Wi-Fi: a fresh board (its defaults apply), then the kept values go back
      if (d["reset"] | false) {
        fresh_ = true;
        reset_kept_.reset(new Settings(settings_));
      }
      for (JsonVariantConst sv : d["shows"].as<JsonArrayConst>()) {
        Show s;
        std::string e;
        if (fromJson(sv, s, e)) shows.push_back(s);
      }
    } else {
      records_ok_ = false;
      if (k == RecordKind::Newer) {
        store_newer_ = (int)v;
        log("the stored settings are from a newer firmware (record format " + std::to_string(v) +
            "): running on the defaults and keeping that record unchanged");
      } else {
        store_unreadable_ = true;
        log("the stored settings could not be read: running on the defaults and keeping that record (factory reset clears it)");
      }
    }
  }
  // a fresh board has no shows. Earlier firmware stored the example show on every fresh board: an untouched copy
  // (byte for byte) is removed here, saved at the next save; an edited one is the user's own and stays.
  for (size_t i = 0; i < shows.size(); i++)
    if (isFactoryExample(shows[i])) {
      shows.erase(shows.begin() + i);
      markDirty();
      log("the factory example show was removed: a board starts with no shows (Shows \xe2\x86\x92 Add an example show)");
      break;
    }
  df_ = DfPlayer(dfConfig(settings_, true));
  sound_ = SoundDirector(settings_.sound());
  applySettings();
  for (auto& s : shows) {
    ShowError e = engine_.setShow(s);
    if (!e.ok()) log("stored show rejected: " + e.message());
  }
}

// The inputs as the controller sees them: active = pressed. TRIG1 / TRIG2 set to sensor_high are inverted here
// (XOR with the board support package's own polarity, applied where the pins are read).
PT_STEP_CODE void Controller::readInputs(bool raw[INPUT_COUNT]) {
  board_.readInputs(raw);
  for (int k = 0; k < 2; k++) raw[k] = raw[k] != settings_.trig_sensor_high[k];
  // a test press is a press like a real one (OR'd with the pin) when a remote start is allowed
  if (testStarts())
    for (int k = 0; k < INPUT_COUNT; k++) raw[k] = raw[k] || testHeld(k);
}

void Controller::testRelease(int k, Millis now) {
  if (!test_until_[k]) return;
  test_until_[k] = 0;
  test_session_[k] = 0;
  test_off_since_[k] = now;
}

// a test press whose time is up lets go by itself (polled every sample: never a wait)
void Controller::expireTests(Millis now) {
  for (int k = 0; k < INPUT_COUNT; k++)
    if (test_until_[k] && now >= test_until_[k]) {
      testRelease(k, now);
      log(std::string("test press on ") + name(ALL_INPUTS[k]) + " let go: its time was up");
    }
}

void Controller::applySettings() {
  const Settings& s = settings_;
  // a changed input type re-arms that trigger as at power-on: it must be seen released before anything starts
  for (int k = 0; k < 2; k++) {
    if (s.trig_sensor_high[k] == sensor_high_[k]) continue;
    sensor_high_[k] = s.trig_sensor_high[k];
    trig_[k] = Trigger();
    has_closed_[k] = true;
    has_opened_[k] = false;
    closed_since_[k] = now_;
    if (k == 0 && fx_) { fx_->released = false; fx_->confirming = false; }
    if (script_run_) script_run_->load(script_prog_.get(), now_);  // each effect back to its first wait for
  }
  engine_.setConfig(s.engine());
  for (auto& t : trig_) t.setTimes(s.trig_debounce_ms, s.trig_release_ms);
  // the effect's confirm and release times are the board's trigger times
  settings_.fx.confirm_ms = s.trig_debounce_ms;
  settings_.fx.release_ms = s.trig_release_ms;
  for (auto& c : sound_.setConfig(soundConfig(), now_)) df_.command(c);
  if (script_run_) script_run_->setTimes(s.trig_debounce_ms, s.trig_release_ms, boot_ms_ + SCRIPT_BOOT_LOCKOUT_MS);
  effectSync();
}

void Controller::accepted(Millis now, const std::vector<uint16_t>& started) {
  pulseRole(now, OutputRole::TriggerRepeat);
  bool has_sound = false;
  for (uint16_t id : started) {
    const Show* s = engine_.show(id);
    const Track* t = s ? s->track(Target::sound()) : nullptr;
    if (t && !t->cues.empty()) has_sound = true;
  }
  if (!has_sound) {
    for (auto& c : sound_.trigger()) df_.command(c);
    pulseRole(now, OutputRole::SoundTrigger);
  }
}

// STOP and pause end every source the engine does not run itself: each script effect goes back to its first
// wait for, the effect's cycle ends and the role pulses are cleared. Nothing comes back on by itself.
void Controller::stopOwned(Millis now) {
  if (script_run_) script_run_->load(script_prog_.get(), now);
  if (fx_) fx_->stop();
  for (auto& r : role_on_) r = false;
}

void Controller::pulseRole(Millis now, OutputRole role, uint32_t ms) {
  if (engine_.isPaused()) return;
  for (auto c : ROLE_CHANNELS)
    if (settings_.role(c) == role) {
      role_on_[index(c)] = true;
      role_until_[index(c)] = now + (ms ? ms : settings_.role_pulse_ms);
    }
}

// Role pulses and the effect's outputs go into the engine as owned levels (as a script's), so one pass applies
// the interlock, the maximum on-times and pause to every source; a manual test still wins.
PT_STEP_CODE void Controller::ownedLevels(Millis now) {
  bool o[3] = {false, false, false};
  if (fx_) fx_->outputs(o[0], o[1], o[2]);
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    Channel c = ALL_CHANNELS[i];
    if (effectOwns(c)) {
      bool on = c == Channel::Out4 ? fx_->ready(now) && fx_->released : o[i];
      engine_.setScript(c, true, on ? FULL : 0);
    } else if (c == Channel::Out4 && !fx_ && settings_.out4_ready_lamp && script_run_ && script_prog_) {
      // the ready lamp of the script's first effect: on while a press would start it
      engine_.setScript(c, true, script_run_->armed(0, now) ? FULL : 0);
    } else if (settings_.role(c) != OutputRole::Shows) {
      engine_.setScript(c, true, role_on_[i] && now < role_until_[i] ? FULL : 0);
    } else if (!(script_prog_ && (script_prog_->channels & (1u << i)))) {
      engine_.setScript(c, false, 0);
    }
  }
}

std::string Controller::checkReserved(const Show& show, const Settings& from) const {
  auto owned = [&](Channel c) {
    return from.effect_on && (c == Channel::Out1 || c == Channel::Out2 || (c == Channel::Out3 && from.fx.out3_enabled) ||
                              (c == Channel::Out4 && from.out4_ready_lamp));
  };
  for (auto& t : show.tracks) {
    if (!t.target.isChannel()) continue;
    OutputRole r = from.role(t.target.channel);
    if (r != OutputRole::Shows && !t.cues.empty())
      return std::string(name(t.target.channel)) + " has the role '" + roleLabel(r) +
             "' and cannot be used in shows (Settings → Output roles)";
    if (owned(t.target.channel) && !t.cues.empty())
      return std::string(name(t.target.channel)) + " is driven by the pulse-and-recover effect and cannot be used in shows (Settings \xe2\x86\x92 Effect)";
  }
  if (from.effect_on)
    for (auto& t : show.triggers)
      if (t.on == ShowTrigger::Kind::OnInput && (t.input == Input::Trig1 || t.input == Input::Trig2))
        return std::string(name(t.input)) + " belongs to the pulse-and-recover effect and cannot trigger shows (Settings \xe2\x86\x92 Effect)";
  return "";
}

ShowError Controller::addCodeShow(const Show& s) {
  ShowError e = engine_.setShow(s);
  if (e.ok()) {
    bool known = false;
    for (auto id : code_shows_) known |= id == s.id;
    if (!known) code_shows_.push_back(s.id);
  }
  return e;
}

// ---------------------------------------------------------------- tick

PT_STEP_CODE void Controller::sampleTriggers(Millis now) {
  expireTests(now);
  bool raw[INPUT_COUNT];
  readInputs(raw);
  sampleTriggersWith(now, raw);
}

PT_STEP_CODE void Controller::sampleTriggersWith(Millis now, const bool raw[INPUT_COUNT]) {
  if (now > sample_now_) sample_now_ = now;
  for (int k = 0; k < INPUT_COUNT; k++) {
    if (raw[k]) {
      has_opened_[k] = false;
      if (!has_closed_[k]) { has_closed_[k] = true; closed_since_[k] = now; }
    } else {
      has_closed_[k] = false;
      if (!has_opened_[k]) { has_opened_[k] = true; opened_since_[k] = now; }
    }
  }
  for (int k = 0; k < 2; k++) {
    TriggerEvent ev = trig_[k].update(raw[k], now);
    if (ev != TriggerEvent::None) trig_events_.push_back({(uint8_t)k, ev});
    // the effect's TRIG2 (recover) and a script's recover effect hear the press on the sample it happens (both run
    // every 1 ms)
    bool pause_role = k == 1 && settings_.trig2 == Trig2Role::Pause;
    if (ev == TriggerEvent::Pressed && !updating() && fx_ && k == 1) (void)doRecover();
    (void)pause_role;
  }
  if (updating()) return;  // nothing starts while a firmware update runs
  effectSample(now, raw);
  if (script_run_ && script_prog_) {
    scriptValues();  // a setting changed since the tick applies to the line that reads it on this sample
    script_run_->sample(now, scriptInputs());
  }
}

// the state's copies for the network (stateJson): every 50 ms while asked for, at most 0.1 s old when used
static const uint8_t STATE_COPY_TICKS = 5;
static const Millis STATE_COPY_MAX_AGE_MS = 100;
static const Millis STATE_WANT_MS = 3000;

PT_STEP_CODE void Controller::publishState(Millis now) {
  uint32_t until = snap_wanted_until_.load(std::memory_order_relaxed);
  if (!until || (int32_t)(until - (uint32_t)now) <= 0) return;
  if (++snap_tick_ < STATE_COPY_TICKS) return;
  snap_tick_ = 0;
  captureState(snap_buf_[snap_back_]);
  uint8_t old = snap_mid_.exchange((uint8_t)(snap_back_ | 0x80), std::memory_order_acq_rel);
  snap_back_ = old & 0x7F;
}

const Controller::StateSnap* Controller::takeState(Millis now) {
  snap_wanted_until_.store((uint32_t)(now + STATE_WANT_MS), std::memory_order_relaxed);
  if (snap_mid_.load(std::memory_order_acquire) & 0x80) {
    uint8_t old = snap_mid_.exchange(snap_front_, std::memory_order_acq_rel);
    snap_front_ = old & 0x7F;
  }
  const StateSnap& f = snap_buf_[snap_front_];
  return f.valid && now >= f.now && now - f.now <= STATE_COPY_MAX_AGE_MS ? &f : nullptr;
}

PT_STEP_CODE void Controller::tick(Millis now) {
  StepScope in_step(this);
  profMark(-1);
  now_ = now;
  updateTick(now);
  if (updating()) {
    // update mode: outputs off, inputs ignored, nothing else runs
    trig_events_.clear();
    Level off[CHANNEL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
    board_.apply(now, off);
    board_.setStatusLed(settings_.status_led && Pattern(Pattern::Kind::FastBlink).level(now));
    return;
  }
  expireTests(now);
  bool raw[INPUT_COUNT];
  readInputs(raw);

  // triggers: their 1 ms samples since the last tick, then this one
  sampleTriggersWith(now, raw);
  profMark(0);
  std::vector<std::pair<uint8_t, TriggerEvent>> evs;
  evs.swap(trig_events_);
  for (auto& te : evs) {
    int k = te.first;
    Input input = ALL_INPUTS[k];
    bool pause_role = k == 1 && settings_.trig2 == Trig2Role::Pause;
    TriggerEvent ev = te.second;
    if (ev == TriggerEvent::Pressed) presses[k]++;
    // a project name for the input: the event says it ("REFILL (TRIG2) pressed", SM-AL0)
    if (ev == TriggerEvent::Pressed && terminalDisplay(name(input)) != name(input)) log(terminalDisplay(name(input)) + " pressed");
    if (fx_) {
      // the effect owns TRIG1 (sampled raw) and TRIG2 (recover); a pause role is ignored while it is on
      // the safety hold counts from TRIG1's debounced press (a press held at power-on too)
      if (k == 0 && (ev == TriggerEvent::Pressed || ev == TriggerEvent::IgnoredNotArmed)) { trig1_held_ = true; trig1_since_ = now; }
      if (k == 0 && ev == TriggerEvent::Released) trig1_held_ = false;
      if (k == 0 && ev == TriggerEvent::Pressed && fx_->refusal(now) == EffectRefused::CooldownFull) {
        log("trigger ignored: " + refusedText(EffectRefused::CooldownFull));
      } else if (k == 1 && ev == TriggerEvent::Pressed) {
        // recover: on its sample (sampleTriggersWith)
      } else if (k == 1 && ev == TriggerEvent::Released) {
        fx_->recoverRelease();
      }
      continue;
    }
    if (ev == TriggerEvent::Pressed && pause_role) {
      (void)engine_.pressed(now, input);
    } else if (ev == TriggerEvent::Pressed) {
      std::vector<uint16_t> started;
      if (engine_.press(now, input, started) == Ignored::None) {
        // a press that only a script waits for: no default trigger sound, no CC role pulses
        if (!(scriptWaitsFor(k) && started.empty())) accepted(now, started);
      }
    } else if (ev == TriggerEvent::IgnoredNotArmed && pause_role) {
      (void)engine_.pressed(now, input);  // a pause switch held at power-on must still pause
    } else if (ev == TriggerEvent::Released) {
      (void)engine_.released(now, input);
    }
  }
  // buttons
  for (int k = 0; k < 3; k++) {
    Input input = ALL_INPUTS[2 + k];
    ButtonEvent ev = buttons_[k].update(raw[2 + k], now);
    if (ev.kind == ButtonEventKind::None) continue;
    if (ev.kind == ButtonEventKind::Pressed) presses[2 + k]++;
    // USER1-3 are plain triggers, like TRIG1 / TRIG2
    if (ev.kind == ButtonEventKind::Pressed) {
      std::vector<uint16_t> started;
      if (engine_.press(now, input, started) == Ignored::None && !started.empty()) accepted(now, started);
    } else if (ev.kind == ButtonEventKind::Click || ev.kind == ButtonEventKind::Released) {
      (void)engine_.released(now, input);
    }
  }

  // TRIG1 held 2 s releases the safety (SM-B4); that hold never starts a run
  if (fx_ && fx_->safety_on && trig1_held_ && now - trig1_since_ >= EFFECT_SAFETY_HOLD_MS) {
    EffectEvent e = fx_->safetyOff();
    if (e.kind != EffectEvent::Kind::None) effectEvents(now, {e});
  }
  profMark(1);
  scriptTick(now);
  profMark(2);
  for (int i = 0; i < CHANNEL_COUNT; i++)
    if (manual_until_[i] && now >= manual_until_[i]) { engine_.setManual(ALL_CHANNELS[i], false, 0); manual_until_[i] = 0; }
  ownedLevels(now);
  Frame frame = engine_.tick(now);
  scriptPixels(frame);
  profMark(3);
  board_.apply(now, frame.levels);
  profMark(4);
  bool show_sound_started = false;
  for (auto& c : frame.sound) {
    sound_.observeShow(c, now);
    show_sound_started |= c.startsPlayback();
    df_.command(c);
  }
  if (show_sound_started) pulseRole(now, OutputRole::SoundTrigger);
  for (auto& c : sound_.tick(now)) df_.command(c);
  std::vector<DfEvent> dfe;
  df_.poll(uart_, now, dfe);
  for (auto& e : dfe) {
    for (auto& c : sound_.onEvent(e, now)) df_.command(c);
    char b[64];
    if (e.kind == DfEvent::Kind::Online) log("sound player online");
    else if (e.kind == DfEvent::Kind::Error) { (void)snprintf(b, sizeof b, "sound player error %u", e.value); log(b); }
    else if (e.kind == DfEvent::Kind::Timeout) { (void)snprintf(b, sizeof b, "sound player did not answer command 0x%02x", e.value); log(b); }
  }
  profMark(5);
  publishPixels(frame);
  patternPresses(now);  // after the strip's picture: a press goes to the pattern it shows
  profMark(6);
  for (auto& n : frame.notices) {
    if (n.kind == Notice::Kind::PausedChanged && n.paused) {
      stopOwned(now);
      for (auto& c : sound_.suspend()) df_.command(c);
    }
    if (n.kind == Notice::Kind::PausedChanged && !n.paused) sound_.resume(now);
    noticeOut(n);
  }
  // the shows' fresh starts since the last step (inputs, requests, cues, timers): each one a run
  for (auto& f : engine_.takeFreshRuns()) {
    const Show* sh = engine_.show(f.show);
    std::string detail;
    if (f.cause.by == Cause::By::ByInput || f.cause.by == Cause::By::ByRelease) detail = name(f.cause.input);
    else if (f.cause.by == Cause::By::ByCue) detail = f.cause.name;
    countRun(now, f.cause.byName(), "show", sh ? sh->name : std::string(), detail, f.show);
  }
  cues_out_.insert(cues_out_.end(), frame.cues_out.begin(), frame.cues_out.end());
  board_.setStatusLed(settings_.status_led && pattern().level(now));
  // a flash write stalls both cores: saved when nothing runs, or at the latest SAVE_MAX_DELAY_MS after the change
  // (by background(), outside the step)
  bool quiet = quietNow();
  if (dirty_ && now >= save_not_before_ &&
      (now >= dirty_at_ + (quiet ? SAVE_DELAY_MS : SAVE_MAX_DELAY_MS) || now >= dirty_since_ + SAVE_MAX_DELAY_MS))
    save_due_ = true;
  publishState(now);
  profMark(7);
  prof_n_++;
}

// the profile marks: part k took the time since the last mark (-1: the tick starts)
void Controller::profMark(int part) {
#if defined(PROPTROLLER_STEP_PROFILE)
  if (!us_clock_) return;
  uint32_t t = us_clock_();
  if (part >= 0) {
    uint32_t d = t - prof_at_;
    prof_sum_[part] += d;
    if (d > prof_max_[part]) prof_max_[part] = d;
  }
  prof_at_ = t;
#else
  (void)part;
#endif
}

std::string Controller::profileReport() {
  if (!prof_n_) return "";
  // inputs and their samples, trigger events and buttons, script tick, engine and owned levels, pin writes, sound,
  // pixels, notices / runs / status lamp / save
  static const char* const PART[PROFILE_PARTS] = {"in", "trig", "script", "engine", "pins", "sound", "pixels", "rest"};
  std::string o = "; step parts avg/max us:";
  for (int k = 0; k < PROFILE_PARTS; k++) {
    o += " " + std::string(PART[k]) + " " + std::to_string(prof_sum_[k] / prof_n_) + "/" + std::to_string(prof_max_[k]);
    prof_sum_[k] = 0;
    prof_max_[k] = 0;
  }
  prof_n_ = 0;
  o += "; state copy max " + std::to_string(prof_capture_max_) + " us";
  prof_capture_max_ = 0;
  return o;
}


void Controller::background() {
  if (self_check_due_) selfCheckVerdict();
  if (save_due_) {
    save_due_ = false;
    if (dirty_) saveNow();
  }
  scriptRewrite();
  patternBackground();
  // the lifetime run count: throttled (runs.h), only when nothing runs unless it has waited too long
  if (runs_.saveDue(now_, quietNow())) runsSaveNow();
}

PT_STEP_CODE bool Controller::quietNow() const { return !engine_.anyRunning() && !(fx_ && fx_->busy()) && !scriptEffectRunning(); }

// ---------------------------------------------------------------- run counters

void Controller::setRunStore(Storage* store) {
  run_store_ = store;
  if (!store) return;
  std::string rec;
  Stored got = store->fetch(rec);
  uint64_t n = 0;
  if (got == Stored::Ok && RunCounter::parse(rec, n)) runs_.loaded(n);
  else if (got != Stored::None) log("the lifetime run count could not be read: it starts again from 0");
}

void Controller::runsSaveNow() {
  if (!runs_.unsaved()) return;
  // no store, or a file system that could not be opened (nothing is written there until a factory reset): RAM only
  if (!run_store_ || !run_store_->usable()) { runs_.saved(now_, true); return; }
  if (window_open_) return;  // another request is writing: the loop tries again
  std::string rec = runs_.record();
  bool ok = false;
  {
    Unlocked u(*this);  // a few bytes, written without holding up the step
    ok = run_store_->save(rec);
  }
  runs_.saved(now_, ok);
  if (!ok) store_errors++;
}

void Controller::countRun(Millis now, const char* source, const char* kind, const std::string& nm, const std::string& detail, int show) {
  runs_.count(now);
  JsonDocument d;
  d["source"] = source;  // input, release, api, cue, timer, boot, after
  d["kind"] = kind;      // show, script, effect
  if (show >= 0) d["show"] = show;
  if (!nm.empty()) d["name"] = nm;
  if (!detail.empty()) d["detail"] = detail;
  d["runs_since_power_on"] = runs_.sincePowerOn();
  d["runs_lifetime"] = (unsigned long long)runs_.lifetime();
  d["uptime_ms"] = (unsigned long long)now;
  std::string s;
  serializeJson(d, s);
  events_.push("run", s);
}

void Controller::runStatus(std::vector<ParamValue>& out, bool aliased) const {
  auto add = [&](const char* k, const Value& v) { out.push_back(ParamValue{aliased ? aliasFor(AliasEntry::Kind::Status, k) : std::string(k), v}); };
  add("runs_since_power_on", Value::integer(runs_.sincePowerOn()));
  add("runs_lifetime", Value::integer((int64_t)runs_.lifetime()));
  add("last_run_uptime_ms", Value::integer(runs_.hasRun() ? (int64_t)runs_.lastRunAt() : -1));
}

// A show script's pixels line: the strip's effect over the shows' until another pixels line (a pause's off and a
// manual pixel test still win, as they do for the outputs). Its speed is pixel_speed now.
PT_STEP_CODE void Controller::scriptPixels(Frame& f) const {
  const ScriptStep* st = nullptr;
  Millis since = 0;
  if (engine_.manualPixel() && !engine_.isPaused()) { patternPixels(f); return; }  // a pixel test of a stored pattern
  if (engine_.isPaused() || engine_.manualPixel() || !script_run_ || !script_prog_ || !script_run_->pixels(st, since)) return;
  f.has_pixel = true;
  f.pixel = pixelEffect((Effect::Kind)st->target, Rgb((uint8_t)(st->ms >> 16), (uint8_t)(st->ms >> 8), (uint8_t)st->ms), settings_.pixel_speed);
  f.pixel_start = since;
  patternPixels(f);  // pixels = pattern <name>: the stored pattern
}

// same picture: the effect and its look; the start counts only for an animation (a still colour looks the same
// whenever it started)
static bool samePicture(bool has_a, const Effect& a, Millis start_a, bool has_b, const Effect& b, Millis start_b) {
  if (has_a != has_b) return false;
  return !has_a || (a == b && (!a.animated() || start_a == start_b));
}

// The step's side: what the strip should show now, handed over only when it changed (never a render, never a wait).
PT_STEP_CODE void Controller::publishPixels(const Frame& f) {
  PixelRequest r;
  r.has = f.has_pixel;
  if (r.has) { r.effect = f.pixel; r.start = f.pixel_start; }
  r.brightness = settings_.pixel_brightness;
  r.count = settings_.pixel_count;
  patternPlay(r);
  if (pixel_published_ && r.brightness == pixel_req_.brightness && r.count == pixel_req_.count &&
      samePicture(r.has, r.effect, r.start, pixel_req_.has, pixel_req_.effect, pixel_req_.start))
    return;
  pixel_req_ = r;
  pixel_published_ = true;
  uint32_t q = pixel_seq_.load(std::memory_order_relaxed);
  pixel_seq_.store(q + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  pixel_mail_ = r;
  std::atomic_thread_fence(std::memory_order_release);
  pixel_seq_.store(q + 2, std::memory_order_release);
}

void Controller::pixelService(Millis now) {
  // the step's latest request (a copy the step did not touch meanwhile; else the next call takes it)
  PixelRequest f;
  uint32_t a = pixel_seq_.load(std::memory_order_acquire);
  if (a == 0 || (a & 1)) return;
  f = pixel_mail_;
  std::atomic_thread_fence(std::memory_order_acquire);
  if (pixel_seq_.load(std::memory_order_relaxed) != a) return;
  if (f.count != pixel_buf_.size()) { pixel_buf_.assign(f.count, Rgb()); pixel_have_last_ = false; }
  if (pixel_buf_.empty()) return;
  bool animated = f.has && f.effect.animated();
  if (pixel_have_last_ && f.brightness == pixel_last_.brightness &&
      samePicture(f.has, f.effect, f.start, pixel_last_.has, pixel_last_.effect, pixel_last_.start) &&
      (!animated || now - pixel_last_at_ < PIXEL_PERIOD_MS))
    return;
  // the last frame is still on the wire: never wait for it; this frame (or a newer one) goes on a later call
  if (pixels_.busy()) { pixel_skipped++; return; }
  int pattern = patternRender(f, now);  // a stored pattern (device_pattern.cpp): its interpreter fills the buffer
  if (pattern == 0) return;
  if (pattern < 0) {
    if (f.has) render(f.effect, now > f.start ? now - f.start : 0, pixel_buf_.data(), pixel_buf_.size());
    else for (auto& p : pixel_buf_) p = Rgb();
  }
  // the brightness cap: a plain scale of every channel (the firmware does not estimate current, SM-S3)
  if (f.brightness < PIXEL_BRIGHTNESS_MAX) {
    uint8_t scale = (uint8_t)((uint32_t)f.brightness * 255 / PIXEL_BRIGHTNESS_MAX);
    for (auto& p : pixel_buf_) p = p.scale(scale);
  }
  if (!pixels_.write(pixel_buf_.data(), pixel_buf_.size())) pixel_errors++;
  pixel_have_last_ = true;
  pixel_last_ = f;
  pixel_last_at_ = now;
}

PT_STEP_CODE Pattern Controller::pattern() const {
  if (fx_) return fx_->pattern(now_);
  if (engine_.isPaused()) return Pattern(Pattern::Kind::Heartbeat);
  for (auto& t : trig_)
    if (t.waitingForRelease() && t.isActive()) return Pattern(Pattern::Kind::Flicker);
  if (engine_.anyRunning()) return Pattern(Pattern::Kind::FastBlink);
  for (size_t i = 0; i < engine_.showCount(); i++) {
    ShowStatus st;
    if (engine_.status(engine_.showAt(i).id, now_, st) && st.kind == ShowStatus::Kind::Cooldown) return Pattern(Pattern::Kind::SlowBlink);
  }
  return Pattern(Pattern::Kind::Steady);
}

// ---------------------------------------------------------------- storage

void Controller::storedDoc(JsonDocument& d, const Show* replace) const {
  d["version"] = STORED_VERSION;
  settings_.toJson(d["settings"].to<JsonObject>());
  JsonArray a = d["shows"].to<JsonArray>();
  bool replaced = false;
  for (size_t i = 0; i < engine_.showCount(); i++) {
    const Show& s = engine_.showAt(i);
    bool code = false;
    for (auto id : code_shows_) code |= id == s.id;
    if (replace && replace->id == s.id) { toJson(*replace, a.add<JsonObject>()); replaced = true; continue; }
    if (!code) toJson(s, a.add<JsonObject>());
  }
  if (replace && !replaced) toJson(*replace, a.add<JsonObject>());
}

std::string Controller::storedJson(const Show* replace) const {
  JsonDocument d;
  storedDoc(d, replace);
  std::string out;
  serializeJson(d, out);
  return out;
}

size_t Controller::storedSize(const Show* replace) const {
  JsonDocument d;
  storedDoc(d, replace);
  return measureJson(d);
}

// The settings record must not be written now: it is from a newer firmware, or it was converted from an older
// format and this image has not yet passed its self-check (a rollback must find it readable), or a factory reset
// has just cleared it and the board is restarting.
bool Controller::storeHeld() const {
  return wiped_ || store_newer_ > 0 || store_unreadable_ || (store_migrated_ && update_target_ && update_target_->state() == BootState::Pending);
}

void Controller::saveNow() {
  // from the step while another task writes the flash (a window): later, from the loop
  if (storeHeld() || window_open_) {
    // kept in RAM, pending
    if (!dirty_) markDirty();
    return;
  }
  dirty_ = false;
  if (!store_) return;
  bool ok = false, same = false;
  uint32_t crc = 0;
  {
    // the record (the settings and the shows, only requests change them) built and written without holding up
    // the step: only it runs meanwhile
    Unlocked u(*this);
    std::string rec = storedJson();
    crc = crc32((const uint8_t*)rec.data(), rec.size());
    same = stored_crc_known_ && crc == stored_crc_;
    if (!same) ok = store_->save(rec);
  }
  if (same) return;  // flash wear: already stored
  if (!ok) {
    // kept in RAM and tried again later, 2 s doubling to 60 s
    store_errors++;
    save_retry_ms_ = save_retry_ms_ ? std::min<Millis>(save_retry_ms_ * 2, SAVE_MAX_DELAY_MS) : SAVE_DELAY_MS;
    dirty_ = true;
    dirty_at_ = dirty_since_ = now_ + save_retry_ms_;
    stored_crc_known_ = false;
    log("saving to flash failed: trying again in " + std::to_string(save_retry_ms_ / 1000) + " s");
  } else {
    save_retry_ms_ = 0;
    stored_crc_ = crc;
    stored_crc_known_ = true;
    save_not_before_ = now_ + SAVE_MIN_GAP_MS;
  }
}

// ---------------------------------------------------------------- info / state

Info Controller::info() const {
  Info i;
  i.id = identity_.id;
  i.name = settings_.name;
  i.fw = PROPTROLLER_FW_NAME;
  i.fw_version = identity_.fw_version;
  i.fw_impl = PROPTROLLER_FW_IMPL;
  i.fw_build = identity_.fw_build;
  i.mcu = identity_.mcu;
  i.board = identity_.board;
  if (update_target_) {
    i.has_update = true;
    i.update_slot = update_target_->slot();
    i.update_max_bytes = update_target_->maxBytes();
    i.update_pending = update_target_->state() == BootState::Pending;
    i.update_rolled_back = update_target_->rolledBack();
    i.update_install_max_s = update_target_->installMaxS();
  }
  i.capabilities = {"shows", "outputs", "sound", "pixels", "params"};
  if (locked()) i.capabilities.push_back("pin");
  if (fx_) i.capabilities.push_back("effect");
  i.capabilities.push_back("script");
  i.capabilities.push_back("backup");
  i.capabilities.push_back("test_input");
  if (update_target_) i.capabilities.push_back("update");
  if (wifi_scan_.available()) i.capabilities.push_back("wifi_scan");
  if (board_store_) i.capabilities.push_back("board_package");
  if (pat_) i.capabilities.push_back("patterns");
  i.capabilities.push_back("defaults");
  for (auto& c : identity_.extra_caps) i.capabilities.push_back(c);
  ActionDesc save;
  save.key = "save";
  save.label = "Save now";
  i.actions.push_back(save);
  if (script_run_ && script_prog_) {
    // every effect of the stored script can be started by its name (SS-R10), and a waiting start cancelled
    // a profile's control alias with that name and a label names the button ("Go"); a labelled control alias of
    // cancel names Cancel ("Cancel the waiting start")
    for (int e = 0; e < script_prog_->effects; e++) {
      ActionDesc a;
      a.key = script_prog_->fx[e].name;
      for (auto& ch : a.key) if (ch == ' ') ch = '_';
      a.label = std::string("Start ") + script_prog_->fx[e].name;
      for (auto& al : settings_.profile.aliases)
        if (al.kind == AliasEntry::Kind::Control && al.alias == a.key && !al.label.empty()) a.label = al.label;
      a.hold = settings_.start_hold;
      i.actions.push_back(a);
    }
    if (!fx_) {
      ActionDesc a;
      a.key = "cancel";
      a.label = "Cancel waiting start";
      for (auto& al : settings_.profile.aliases)
        if (al.kind == AliasEntry::Kind::Control && al.target == a.key) a.label = al.label.empty() ? al.alias + " (" + a.label + ")" : al.label;
      i.actions.push_back(a);
    }
  }
  if (fx_) {
    const char* const keys[3][2] = {{"trigger", "Trigger"}, {"recover", "Recover"}, {"cancel", "Cancel queued run"}};
    for (auto& k : keys) {
      ActionDesc a;
      a.key = k[0];
      a.label = k[1];
      // a held recover runs while its button is held: the page sends down true / false (effectAction)
      a.momentary = a.key == "recover" && settings_.fx.recover_mode == RecoverMode::Hold;
      // a control alias names the button, the board's name beside it: "Go (Trigger)" (SM-AL0)
      for (auto& al : settings_.profile.aliases)
        if (al.kind == AliasEntry::Kind::Control && al.target == a.key) a.label = (al.label.empty() ? al.alias : al.label) + " (" + a.label + ")";
      i.actions.push_back(a);
    }
  }
  i.stop_button = settings_.stop_button;
  i.terminals = terminals();
  // terminal aliases: displays show the alias with the board name beside it (SM-AL0)
  for (auto& t : i.terminals)
    for (auto& a : settings_.profile.aliases)
      if (a.kind == AliasEntry::Kind::Terminal && a.target == t.id) {
        t.alias = a.alias;
        if (t.label.empty()) t.label = a.label;  // the profile's label when the wiring has none
      }
  return i;
}

uint16_t Controller::freeShowId() const {
  uint16_t id = 1;
  while (engine_.show(id) && id < 0xFFFF) id++;
  return id;
}

std::vector<Terminal> Controller::terminals() const {
  const Settings& s = settings_;
  std::vector<Terminal> v;
  // only the terminals this board has (a board profile may leave some out)
  for (int i = 0; i < INPUT_COUNT; i++) {
    if (!board_.has(ALL_INPUTS[i])) continue;
    Terminal t(name(ALL_INPUTS[i]), Terminal::Kind::InputT, s.names[8 + i]);
    if (ALL_INPUTS[i] == Input::Trig2 && s.trig2 == Trig2Role::Pause) t.setRole("pause");
    t.shows = !t.has_role;
    v.push_back(t);
  }
  for (auto c : ALL_CHANNELS) {
    if (!board_.has(c)) continue;
    Terminal t(name(c), Terminal::Kind::OutputT, s.names[index(c)]);
    OutputRole r = s.role(c);
    if (r != OutputRole::Shows) t.setRole(roleName(r));
    t.shows = r == OutputRole::Shows;
    t.disabled = r == OutputRole::None;
    v.push_back(t);
  }
  if (fx_) {
    // the terminals the pulse-and-recover effect owns
    for (auto& t : v) {
      const char* role = nullptr;
      if (t.id == "TRIG1") role = "trigger";
      else if (t.id == "TRIG2") role = "recover";
      else if (t.id == "OUT1") role = "pulse";
      else if (t.id == "OUT2") role = "recovery";
      else if (t.id == "OUT3" && s.fx.out3_enabled) role = "after-pulse";
      else if (t.id == "OUT4" && s.out4_ready_lamp) role = "ready lamp";
      else if (t.role == "trigger_repeat") role = "trigger out";
      else if (t.role == "sound_trigger") role = "sound out";
      if (role) { t.setRole(role); t.shows = false; }
    }
  }
  if (board_.hasSound()) v.push_back(Terminal("SOUND", Terminal::Kind::SoundT, "DFPlayer, SPK / LINE OUT"));
  if (board_.hasPixels()) {
    char b[32];
    (void)snprintf(b, sizeof b, "%u pixels", (unsigned)s.pixel_count);
    Terminal px("PIXEL", Terminal::Kind::PixelT, b);
    px.shows = true;
    px.disabled = s.pixel_count == 0;
    v.push_back(px);
  }
  return v;
}

// The state in two halves: captureState copies what the step changes (levels, triggers, show timers, the effect,
// the script's lines, the sound player) into a StateSnap, fast and under the lock; renderState writes the JSON from
// it and from what only requests change (names, aliases, settings), which needs no lock against the step. So a
// page or the serial port asking for the state holds the lock only for the copy (stateJson renders in a window).
PT_STEP_CODE void Controller::captureState(StateSnap& s) const {
  s.valid = true;
  s.now = now_;
  s.board = board_.report();
  s.store_errors = store_errors;
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    Channel c = ALL_CHANNELS[i];
    StateSnap::Out& o = s.out[i];
    Level ml = 0;
    o.manual = engine_.manual(c, ml);
    o.requested = engine_.requested()[i];
    o.ramping = engine_.ramping(c);
    o.driver = engine_.drivers()[i];
    o.owned = effectOwns(c);
    o.blocked = engine_.blocked(i);
  }
  for (int i = 0; i < INPUT_COUNT; i++) {
    StateSnap::In& x = s.in[i];
    if (i < 2) {
      x.active = trig_[i].isActive();
      x.waiting = trig_[i].waitingForRelease() && trig_[i].isActive();
    } else {
      x.active = buttons_[i - 2].isPressed();
      x.waiting = false;
    }
    if (i == 0 && fx_) x.waiting = fx_->raw_closed && !fx_->released;  // the effect decides TRIG1
    x.test = testHeld(i);
    x.test_starts = testStarts();
    x.armed = i < 2 && (i == 0 && fx_ ? fx_->released : trig_[i].isReady());
  }
  s.shows.resize(engine_.showCount());
  for (size_t k = 0; k < engine_.showCount(); k++) {
    StateSnap::ShowSnap& x = s.shows[k];
    x.id = engine_.showAt(k).id;
    x.status = ShowStatus();
    engine_.status(x.id, s.now, x.status);
    x.left = x.window = 0;
    x.has_runs = engine_.runsLeft(x.id, s.now, x.left, x.window);
  }
  s.paused = engine_.isPaused();
  s.runs_since = runs_.sincePowerOn();
  s.runs_lifetime = runs_.lifetime();
  s.has_run = runs_.hasRun();
  s.last_run = runs_.hasRun() ? runs_.lastRunAt() : 0;
  s.sound_playing = df_.isPlaying();
  s.sound_online = df_.isOnline();
  s.has_track = df_.current(s.track);
  s.playing = sound_.playing().kind;
  s.ambient_missing = sound_.ambientMissing();
  s.pattern = pattern();
  statusNums(s.status);
  s.has_pixel = pixel_published_ && pixel_req_.count && pixel_req_.has;
  if (s.has_pixel) s.pixel = pixel_req_.effect;
  s.has_fx = fx_ != nullptr;
  if (fx_) {
    s.fx_state = fx_->stateName(s.now);
    s.fx_cooldown_left = fx_->cooldownRemaining(s.now);
  }
  s.script_effects = 0;
  if (script_prog_ && script_run_) {
    s.script_effects = script_prog_->effects;
    for (int e = 0; e < script_prog_->effects && e < SCRIPT_MAX_EFFECTS; e++) {
      s.script_running[e] = script_run_->running(e);
      s.script_line[e] = script_run_->line(e);
    }
  }
  s.upd_active = upd_.active;
  s.upd_restarting = upd_.restart_at_set;
  s.upd_done = upd_.done;
  s.upd_total = upd_.total;
}

void Controller::renderState(const StateSnap& s, JsonObject o) const {
  const BoardReport& snap = s.board;
  Millis now = s.now;
  o["v"] = PROTOCOL_VERSION;
  o["uptime_ms"] = (unsigned long long)now;
  // the flash: only when something is wrong
  if (store_newer_ || store_unreadable_)
    o["store"] = std::string("the stored settings ") + (store_newer_ ? "are from a newer firmware" : "could not be read") +
                 " and are kept: changes are not saved (a factory reset starts over)";
  else if (script_newer_ || script_unreadable_)
    o["store"] = std::string("the stored show script ") + (script_newer_ ? "is from a newer firmware" : "could not be read") +
                 " and is kept: the script cannot be changed (a factory reset starts over)";
  if (s.store_errors) o["store_errors"] = s.store_errors;
  // the stored board package could not be used: no pins are driven (BOARD-PACKAGES.md)
  if (board_boot_.source == BoardSource::None && !board_boot_.problem.empty()) o["board_problem"] = board_boot_.problem;
  JsonArray outs = o["outputs"].to<JsonArray>();
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    const StateSnap::Out& so = s.out[i];
    JsonObject x = outs.add<JsonObject>();
    Channel c = ALL_CHANNELS[i];
    OutputRole role = settings_.role(c);
    x["channel"] = name(c);
    x["level"] = snap.levels[i];
    x["manual"] = so.manual;
    x["requested"] = so.requested;
    x["ramping"] = so.ramping;
    if (so.manual) x["driver"] = "manual";
    else if (role != OutputRole::Shows) x["driver"] = roleLabel(role);
    else {
      const Driver& d = so.driver;
      if (d.kind == Driver::Kind::ByShow) {
        const Show* sh = engine_.show(d.show);
        if (sh) x["driver"] = sh->name;
        else { char b[16]; (void)snprintf(b, sizeof b, "show %u", d.show); x["driver"] = b; }
      } else if (d.kind == Driver::Kind::Manual) x["driver"] = "manual";
      else if (d.kind == Driver::Kind::Script) x["driver"] = "script";
    }
    if (so.owned) x["driver"] = "effect";
    switch (so.blocked) {
      case Block::Interlock: x["blocked"] = "interlock"; break;
      case Block::Watchdog: x["blocked"] = "watchdog"; break;
      case Block::Paused: x["blocked"] = "paused"; break;
      case Block::None:
      default:
        if (role == OutputRole::None) x["blocked"] = "not used";
    }
    for (auto& a : settings_.profile.aliases)
      if (a.kind == AliasEntry::Kind::Terminal && a.target == name(c)) x["alias"] = a.alias;
  }
  JsonArray ins = o["inputs"].to<JsonArray>();
  for (int i = 0; i < INPUT_COUNT; i++) {
    const StateSnap::In& si = s.in[i];
    JsonObject x = ins.add<JsonObject>();
    x["input"] = name(ALL_INPUTS[i]);
    x["active"] = si.active;
    x["waiting"] = si.waiting;
    x["raw"] = snap.inputs[i];
    // a test press held now (test_input), and whether it starts things (remote start on) or only shows
    if (si.test) { x["test"] = true; x["test_starts"] = si.test_starts; }
    if (i < 2) x["armed"] = si.armed;
    x["held"] = false;
    for (auto& a : settings_.profile.aliases)
      if (a.kind == AliasEntry::Kind::Terminal && a.target == name(ALL_INPUTS[i])) x["alias"] = a.alias;
  }
  JsonArray shows = o["shows"].to<JsonArray>();
  for (size_t k = 0; k < s.shows.size() && k < engine_.showCount(); k++) {
    const Show& sh = engine_.showAt(k);
    const StateSnap::ShowSnap& ss = s.shows[k];
    JsonObject x = shows.add<JsonObject>();
    x["id"] = sh.id;
    x["name"] = sh.name;
    showStatusToJson(ss.status, x);
    if (ss.has_runs) {
      x["runs_left"] = ss.left;
      x["window_ms"] = ss.window;
    }
  }
  o["paused"] = s.paused;
  // the run counters (runs.h): the time since the last run is uptime_ms - last_run_uptime_ms
  o["runs_since_power_on"] = s.runs_since;
  o["runs_lifetime"] = (unsigned long long)s.runs_lifetime;
  if (s.has_run) o["last_run_uptime_ms"] = (unsigned long long)s.last_run;
  o["sound_playing"] = s.sound_playing;
  o["sound_online"] = s.sound_online;
  patternToJson(s.pattern, o, "pattern");
  JsonArray status = o["status"].to<JsonArray>();
  std::vector<ParamValue> lines;
  statusLines(s.status, lines, true);
  for (auto& pv : lines) {
    JsonObject x = status.add<JsonObject>();
    x["key"] = pv.key;
    pv.value.toJson(x, "value");
    // a profile's label for it, and "ms" for a time left (shown as m:ss), also under an alias
    const AliasEntry* al = aliasNamed(pv.key);
    std::string base = al && al->kind == AliasEntry::Kind::Status ? al->target : pv.key;
    if (al && al->kind == AliasEntry::Kind::Status && !al->label.empty()) x["label"] = al->label;
    if (base.size() > 13 && base.compare(base.size() - 13, 13, "_remaining_ms") == 0) x["unit"] = "ms";
  }
  // the run counters' labels from the profile ("Presses tonight")
  for (const char* k : RUN_STATUS)
    for (auto& a : settings_.profile.aliases)
      if (a.kind == AliasEntry::Kind::Status && a.target == k && !a.label.empty()) o["run_labels"][k] = a.label;
  net_.toJson(o["net"].to<JsonObject>());
  JsonObject so = o["sound"].to<JsonObject>();
  so["playing"] = s.sound_playing;
  if (s.has_track) {
    const SoundCommand& cur = s.track;
    char b[32];
    switch (cur.kind) {
      case SoundCommand::Kind::PlayMp3: (void)snprintf(b, sizeof b, "/mp3/%04u", cur.a); break;
      case SoundCommand::Kind::LoopMp3: (void)snprintf(b, sizeof b, "/mp3/%04u (loop)", cur.a); break;
      case SoundCommand::Kind::PlayFolder: (void)snprintf(b, sizeof b, "/%02u/%03u", cur.a, cur.b); break;
      case SoundCommand::Kind::LoopFolder: (void)snprintf(b, sizeof b, "/%02u/ (loop)", cur.a); break;
      case SoundCommand::Kind::PlayIndex: case SoundCommand::Kind::LoopIndex: (void)snprintf(b, sizeof b, "card file #%u", cur.a); break;
      case SoundCommand::Kind::Next: case SoundCommand::Kind::Pause: case SoundCommand::Kind::Reset: case SoundCommand::Kind::Resume: case SoundCommand::Kind::SelectSd: case SoundCommand::Kind::Stop: case SoundCommand::Kind::Volume:
      default: b[0] = 0;
    }
    so["track"] = b;
  }
  switch (s.playing) {
    case Playing::Kind::Ambient: so["source"] = "ambient"; break;
    case Playing::Kind::Trigger: so["source"] = "trigger"; break;
    case Playing::Kind::Show: so["source"] = "show"; break;
    case Playing::Kind::Loop: so["source"] = "loop"; break;
    case Playing::Kind::Idle:
    default: break;
  }
  so["online"] = s.sound_online;
  so["ambient_missing"] = s.ambient_missing;
  if (s.has_pixel) o["pixel"] = s.pixel.describe();
  if (s.has_fx) {
    // the state panel: the effect's state machine and its cooldown timer
    JsonObject m = o["machine"].to<JsonObject>();
    m["state"] = s.fx_state;
    JsonArray st = m["states"].to<JsonArray>();
    for (auto n : EFFECT_STATES) st.add(n);
    JsonArray tm = m["timers"].to<JsonArray>();
    if (s.fx_cooldown_left > 0) {
      JsonObject t = tm.add<JsonObject>();
      t["label"] = "cooldown";
      t["ms"] = s.fx_cooldown_left;
      t["total_ms"] = settings_.fx.cooldown_ms;
    }
  }
  // the script, then a firmware update in progress
  if (!script_text_.empty()) {
    JsonObject sc = o["script"].to<JsonObject>();
    JsonArray fx = sc["effects"].to<JsonArray>();
    if (script_prog_)
      for (int e = 0; e < s.script_effects && e < script_prog_->effects; e++) {
        JsonObject x = fx.add<JsonObject>();
        x["name"] = std::string(script_prog_->fx[e].name);
        x["running"] = s.script_running[e];
        x["line"] = s.script_line[e];
      }
    if (!script_error_.empty()) sc["error"] = script_error_;
  }
  if (s.upd_active) {
    JsonObject u = o["update"].to<JsonObject>();
    u["phase"] = "writing";
    u["done"] = s.upd_done;
    u["total"] = s.upd_total;
  } else if (s.upd_restarting) {
    JsonObject u = o["update"].to<JsonObject>();
    u["phase"] = "restarting";
    u["done"] = 0;
    u["total"] = 0;
  }
}

void Controller::stateToJson(JsonObject o) const {
  std::unique_ptr<StateSnap> s(new StateSnap());  // on the heap: the boards' loop stacks are small
  captureState(*s);
  renderState(*s, o);
}

std::string Controller::infoJson() {
  std::string s;
  Unlocked u(*this);  // only what requests change: written without holding up the step
  JsonDocument d;
  info().toJson(d.to<JsonObject>());
  serializeJson(d, s);
  return s;
}

// The state for a page, the serial port or MQTT: on a board the step's latest copy (stateJson holds the lock only
// to take it), else (the first time, or the host's controller without a lock) a copy made here.
const Controller::StateSnap* Controller::stateSnapshot() {
  const StateSnap* snap = lock_release_ && !in_tick_ ? takeState(now_) : nullptr;
  if (snap) return snap;
  if (!state_snap_) state_snap_.reset(new StateSnap());
#if defined(PROPTROLLER_STEP_PROFILE)
  uint32_t t0 = us_clock_ ? us_clock_() : 0;
#endif
  captureState(*state_snap_);  // under the lock
#if defined(PROPTROLLER_STEP_PROFILE)
  if (us_clock_) { uint32_t d = us_clock_() - t0; if (d > prof_capture_max_) prof_capture_max_ = d; }
#endif
  return state_snap_.get();
}

std::string Controller::stateJson() {
  const StateSnap* snap = stateSnapshot();
  std::string s;
  {
    // the JSON in a window: the step runs meanwhile, every other task waits; renderState reads the snapshot and
    // what only requests change
    Unlocked u(*this);
    JsonDocument d;
    renderState(*snap, d.to<JsonObject>());
    serializeJson(d, s);
  }
  return s;
}

std::string Controller::call(const char* body, size_t len) {
  // a big body (a backup, a script) needs up to 4 times its size and 48 KB more while it is parsed and checked
  // (requestHeapNeed, checked by test_heap_peak): refused before anything is allocated when the heap cannot give that
  if (len > 2048) {
    std::string short_of;
    if (heap_probe_) {
      size_t need = requestHeapNeed(len), have = heap_probe_();
      if (have < need) short_of = std::to_string(have / 1024) + " KB free, about " + std::to_string(need / 1024) + " KB needed";
    }
    if (short_of.empty() && block_probe_) {
      size_t need = requestBlockNeed(len), have = block_probe_();
      if (have < need)
        short_of = "the largest free block is " + std::to_string(have / 1024) + " KB, about " + std::to_string(need / 1024) + " KB needed in one piece";
    }
    if (!short_of.empty())
      return errorFor(body, len, ErrorCode::Refused, "not enough memory for a request this big (" + short_of + "): restart the board and try again").json();
  }
  Request r;
  Response* err = nullptr;
  std::string generic, why;
  bool parsed = false;
  // the aliases, copied (small): the parse reads only this request's own data. None: the body is parsed as it
  // is, never copied first
  std::unique_ptr<Profile> aliases;
  if (!settings_.profile.aliases.empty()) aliases.reset(new Profile(settings_.profile));
  bool hook = hasRequestHook();
  {
    Unlocked u(*this);  // the body is this request's own: parsed without holding up the step
    if (aliases) {
      if (!aliasIn(*aliases, body, len, generic, why)) return errorFor(body, len, ErrorCode::BadRequest, why).json();
      body = generic.data();
      len = generic.size();
    }
    parsed = parseRequest(body, len, r, err, hook);
    std::string().swap(generic);  // the document has its own copy
  }
  if (!parsed) {
    std::string s = err->json();
    delete err;
    return s;
  }
  // what reads only what requests change (the shows, the profile, the settings, the script's text, a script to
  // check): answered in a window, so only the step runs meanwhile and nothing it changes is read
  static const char* const READ_ONLY[] = {"list_shows", "get_show", "get_profile", "get_script", "info", "check_script", "get_backup", "get_params"};
  bool read_only = false;
  for (auto op : READ_ONLY) read_only |= r.op == op;
  auto answerIt = [&]() -> Response {
    if (!read_only) return handle(r);
    Unlocked u(*this);
    return handle(r);
  };
  Response resp = answerIt();
  // the answer is this request's own document (a get_params or get_backup answer is 10-20 KB of text): written
  // out, and the request's and the answer's documents freed, without holding up the step
  std::string answer;
  {
    Unlocked u(*this);
    // an answer the heap cannot hold (a script check with many messages: hardware 2026-10-23, a 15 KB answer
    // restarted an ESP32) is refused in plain words instead of running out of memory while it is written
    size_t out = resp.jsonSize();
    std::string short_of;
    if (out > ANSWER_CHECK_BYTES) {
      if (heap_probe_ && heap_probe_() < out + ANSWER_HEAP_SPARE)
        short_of = std::to_string(heap_probe_() / 1024) + " KB free, about " + std::to_string((out + ANSWER_HEAP_SPARE) / 1024) + " KB needed";
      else if (block_probe_ && block_probe_() < out + ANSWER_BLOCK_SPARE)
        short_of = "the largest free block is " + std::to_string(block_probe_() / 1024) + " KB, about " + std::to_string((out + ANSWER_BLOCK_SPARE) / 1024) + " KB needed in one piece";
    }
    if (!short_of.empty()) {
      resp.drop();  // first: the refusal needs the memory
      answer = Response::err(r, ErrorCode::Refused, "the answer is too big for the memory left (" + short_of + "): restart the board and try again").json();
    } else {
      answer = resp.json();
      resp.drop();
    }
    r.doc.clear();
    r.doc.shrinkToFit();
  }
  return answer;
}

NetConfig Controller::netConfig() {
  NetConfig n;
  n.sta_ssid = settings_.wifi_ssid;
  n.sta_pass = settings_.wifi_pass;
  const std::string& id = identity_.id;
  n.ap_ssid = "Proptroller-" + (id.size() >= 4 ? id.substr(id.size() - 4) : id);
  n.ap_pass = settings_.ap_pass;
  n.hostname = hostLabel(settings_.name, id);
  n.hostname_taken = mdnsLabel(n.hostname, id, true);
  return n;
}

std::vector<std::string> Controller::takeCuesOut() {
  std::vector<std::string> v;
  v.swap(cues_out_);
  return v;
}

// ---------------------------------------------------------------- API

bool Controller::handleExtra(Request& r, Response& out) {
  if (request_hook_) return request_hook_(*this, r, out, request_hook_user_);
  return false;
}

// Why a show did not start, in plain words with what to do.
const char* ignoredText(Ignored why) {
  switch (why) {
    case Ignored::Running: return "already running: wait for it to end, or set \"If triggered again\" to restart";
    case Ignored::Cooldown: return "resting after its last run (its cooldown): try again when the cooldown is over";
    case Ignored::Disabled: return "switched off: enable it in the show editor";
    case Ignored::Paused: return PAUSED_TEXT;
    case Ignored::None: case Ignored::Unknown:
    default: return "there is no show with that number";
  }
}

static Response refused(const Request& r, Ignored why) {
  return Response::err(r, why == Ignored::Unknown ? ErrorCode::NotFound : ErrorCode::Refused, ignoredText(why));
}

bool Controller::pinOk(const Request& r) const { return r.has_pin && passwordOk(settings_.pin_salt, settings_.pin_hash, r.pin); }

// The page password, throttled: after a wrong one the next try waits 1 s, doubling to 30 s; "" when it is right.
std::string Controller::pinRefused(bool has, const std::string& pin) {
  static const char* const LOCKED = "the page is locked: enter the page password (Settings \xe2\x86\x92 Device)";
  if (!has) return LOCKED;
  Millis now = fxNow();
  if (now < pin_wait_until_)
    return "wrong page password: wait " + std::to_string((pin_wait_until_ - now + 999) / 1000) + " s before the next try";
  if (passwordOk(settings_.pin_salt, settings_.pin_hash, pin)) { pin_wait_ms_ = 0; return ""; }
  pin_wait_ms_ = pin_wait_ms_ ? std::min<Millis>(pin_wait_ms_ * 2, PIN_WAIT_MAX_MS) : PIN_WAIT_MS;
  pin_wait_until_ = now + pin_wait_ms_;
  return LOCKED;
}

Response Controller::handle(Request& r) {
  Millis now = now_;
  const std::string& op = r.op;
  // page lock (SM-P2): checked before anything changes; the USB serial port is local access
  // stopping is never locked: STOP, stop, pause on, releasing or switching off an output
  bool safe = op == "stop_all" || op == "stop" || (op == "pause" && r["on"].as<bool>()) ||
              (op == "set_output" && (r["level"].isNull() || r["level"].as<int>() == 0)) ||
              (op == "test_input" && !r["active"].as<bool>());
  if (r.mutates() && !safe && locked() && origin_ != Origin::Serial) {
    std::string why = pinRefused(r.has_pin, r.pin);
    if (!why.empty()) return Response::err(r, ErrorCode::Pin, why);
  }
  if (r.mutates() && updating()) return Response::err(r, ErrorCode::Refused, update_text::IN_UPDATE_MODE);
  // a change from the step (a sketch's task) while another task's request has the lock given back (Unlocked):
  // nothing may change the settings, the profile or the script under it (stopping still works). Other tasks never
  // get here meanwhile: the lock makes them wait (window_lock.h)
  if (r.mutates() && !safe && window_open_) return Response::err(r, ErrorCode::Refused, BUSY_TEXT);
  {
    Response extra = Response::done(r);
    if (handleExtra(r, extra)) return extra;
    if (r.custom) return unknownOp(r);
  }
  if (op == "custom") return Response::err(r, ErrorCode::NotFound, std::string("no code on this board answers \"") + (r["name"] | "") + "\"");
  if (op == "info") {
    Response resp = Response::ok(r, "info");
    // only what requests change (names, settings, the script, the update slot): written in a window
    Unlocked u(*this);
    info().toJson(resp.result());
    return resp;
  }
  if (op == "state") {
    Response resp = Response::ok(r, "state");
    const StateSnap* snap = stateSnapshot();
    Unlocked u(*this);  // the JSON from the copy, as stateJson
    renderState(*snap, resp.result());
    return resp;
  }
  if (op == "list_shows") {
    Response resp = Response::ok(r, "shows");
    JsonArray a = resp.result()["shows"].to<JsonArray>();
    for (size_t i = 0; i < engine_.showCount(); i++) {
      const Show& s = engine_.showAt(i);
      JsonObject x = a.add<JsonObject>();
      x["id"] = s.id;
      x["name"] = s.name;
      x["enabled"] = s.enabled;
      x["length_ms"] = s.length();
      x["tracks"] = (unsigned)s.tracks.size();
    }
    return resp;
  }
  if (op == "get_show") {
    const Show* s = engine_.show(r["show"].as<uint16_t>());
    if (!s) return Response::err(r, ErrorCode::NotFound, "no such show");
    Response resp = Response::ok(r, "show");
    toJson(*s, resp.result()["data"].to<JsonObject>());
    return resp;
  }
  if (op == "put_show") {
    Show data;
    std::string e;
    (void)fromJson(r["data"], data, e);
    // "new": a new show, the board picks its id (the answer carries the stored show)
    bool fresh = r["new"] | false;
    if (fresh) {
      if (engine_.showCount() >= MAX_SHOWS) return Response::err(r, ErrorCode::Internal, STORAGE_FULL);
      data.id = freeShowId();
    }
    ShowError se = validate(data);
    if (!se.ok()) {
      // the board says what is wrong and what to change, naming the output and the time
      Response bad = Response::invalid(r, se);
      bad.doc()["error"]["message"] = explain(se, data);
      return bad;
    }
    std::string m = checkReserved(data, settings_);
    if (!m.empty()) return Response::err(r, ErrorCode::Invalid, m);
    if (store_ && storedSize(&data) > store_->capacity()) return Response::err(r, ErrorCode::Internal, STORAGE_FULL);
    se = engine_.setShow(data);
    if (!se.ok()) return Response::invalid(r, se);
    for (size_t i = 0; i < code_shows_.size(); i++)
      if (code_shows_[i] == data.id) { code_shows_.erase(code_shows_.begin() + i); break; }
    markDirty();
    events_.push("shows_changed");
    if (fresh) {
      Response resp = Response::ok(r, "show");
      toJson(data, resp.result()["data"].to<JsonObject>());
      return resp;
    }
    return Response::done(r);
  }
  if (op == "add_example") {
    // the example show, on request only (a fresh board has none): id 1, or the lowest free id
    Show ex;
    std::string e;
    (void)showFromString(EXAMPLE_SHOW, ex, e);
    ex.id = freeShowId();
    if (engine_.showCount() >= MAX_SHOWS) return Response::err(r, ErrorCode::Internal, STORAGE_FULL);
    std::string m = checkReserved(ex, settings_);
    if (!m.empty()) return Response::err(r, ErrorCode::Invalid, "the example show cannot be added: " + m);
    if (store_ && storedJson(&ex).size() > store_->capacity()) return Response::err(r, ErrorCode::Internal, STORAGE_FULL);
    ShowError se = engine_.setShow(ex);
    if (!se.ok()) return Response::invalid(r, se);
    markDirty();
    events_.push("shows_changed");
    Response resp = Response::ok(r, "show");
    toJson(ex, resp.result()["data"].to<JsonObject>());
    return resp;
  }
  if (op == "delete_show") {
    if (!engine_.deleteShow(r["show"].as<uint16_t>())) return Response::err(r, ErrorCode::NotFound, "no such show");
    markDirty();
    events_.push("shows_changed");
    return Response::done(r);
  }
  if (op == "run") {
    Ignored w = engine_.run(now, r["show"].as<uint16_t>());
    return w == Ignored::None ? Response::done(r) : refused(r, w);
  }
  if (op == "stop") { engine_.stop(now, r["show"].as<uint16_t>()); return Response::done(r); }
  if (op == "stop_all") {
    engine_.stopAll(now);
    stopOwned(now);
    engine_.sound(SoundCommand::simple(SoundCommand::Kind::Stop));
    return Response::done(r);
  }
  if (op == "pause") {
    engine_.pause(now, r["on"].as<bool>());
    if (engine_.isPaused()) stopOwned(now);
    return Response::done(r);
  }
  if (op == "cue") { engine_.cue(now, r["name"].as<std::string>()); return Response::done(r); }
  if (op == "set_output") {
    Channel ch = Channel::Out1;
    (void)reqChannel(r, "channel", ch);
    if (!board_.has(ch)) return Response::err(r, ErrorCode::NotFound, std::string(name(ch)) + " is not on this board");
    if (effectOwns(ch)) return Response::err(r, ErrorCode::Refused, std::string(name(ch)) + " is driven by the pulse-and-recover effect (Settings \xe2\x86\x92 Effect)");
    bool has = !r["level"].isNull();
    uint64_t l = has ? r["level"].as<uint64_t>() : 0;
    if (has && (l > 1000 || (l > 0 && l < 1000 && !isPwm(ch))))
      return Response::err(r, ErrorCode::BadRequest, "level 0-1000; relays and contacts are on/off");
    uint32_t ms = r["ms"] | 0u;
    // a pause is a show stop (PROTOCOL.md): nothing goes on while it lasts, and nothing set meanwhile comes on at
    // its release
    if (l > 0 && engine_.isPaused()) return Response::err(r, ErrorCode::Refused, PAUSED_TEXT);
    if (l > 0 && !ms && (origin_ == Origin::Http || origin_ == Origin::Mqtt))
      return Response::err(r, ErrorCode::Refused, "from plain HTTP or MQTT an output goes on only as a pulse: add \"ms\" (at most 600000); to hold it on, use the phone page");
    engine_.setManual(ch, has, (Level)l);
    // a pulse ends by itself; what a phone page holds ends with that page (sessionClosed)
    manual_until_[index(ch)] = has && ms ? fxNow() + ms : 0;
    manual_session_[index(ch)] = has && origin_ == Origin::Session ? session_ : 0;
    return Response::done(r);
  }
  if (op == "pixel") {
    if (r["action"].isNull()) { engine_.setManualPixel(nullptr); return Response::done(r); }
    Effect eff;
    if (!strcmp(r["action"]["do"] | "", "pattern")) {
      // a stored LED pattern as the pixel test
      std::string name = r["action"]["name"] | "";
      if (!pat_) return Response::err(r, ErrorCode::Unsupported, "this board has no pattern player");
      if (!patternEffect(name, eff)) return Response::err(r, ErrorCode::NotFound, "there is no pattern called " + name + " on this board");
    } else {
      Action a;
      std::string e;
      (void)actionFromJson(r["action"], a, e);
      if (!a.effect(eff)) return Response::err(r, ErrorCode::BadRequest, "not a pixel action");
    }
    if (engine_.isPaused()) return Response::err(r, ErrorCode::Refused, PAUSED_TEXT);
    engine_.setManualPixel(&eff);
    pixel_session_ = origin_ == Origin::Session ? session_ : 0;
    return Response::done(r);
  }
  if (op == "sound") {
    SoundCommand c;
    (void)reqSound(r, c);
    // loop_mp3: /mp3/NNNN over and over (the director plays it again each time it ends); stop ends it too
    if (c.kind == SoundCommand::Kind::LoopMp3) {
      for (auto& x : sound_.scriptLoop(c.a, now_, SoundMode::Mp3)) df_.command(x);
      return Response::done(r);
    }
    if (c.kind == SoundCommand::Kind::Stop)
      for (auto& x : sound_.scriptLoop(0, now_)) (void)x;
    engine_.sound(c);
    return Response::done(r);
  }
  if (op == "get_params") {
    Response resp = Response::ok(r, "params");
    // built with the lock given back: only the step runs meanwhile, and it changes no setting or alias (window_lock.h)
    Unlocked u(*this);
    JsonArray p = resp.result()["params"].to<JsonArray>();
    // with a profile, an aliased setting is listed under its alias, label and unit (SM-AL2), its range in the
    // alias's units (SM-AL3)
    for (auto& d : Settings::params()) {
      std::string alias = aliasFor(AliasEntry::Kind::Key, d.key);
      if (alias == d.key) { d.toJson(p.add<JsonObject>()); continue; }
      const AliasEntry* a = aliasNamed(alias);
      ParamDesc x = d;
      x.key = alias;
      if (!a->label.empty()) x.label = a->label;
      if (!a->unit.empty()) x.unit = a->unit;
      if (!a->group.empty()) x.group = a->group;
      if (a->scale) { x.min = aliasMin(d.min, a->scale); x.max = aliasMax(d.max, a->scale); }
      x.toJson(p.add<JsonObject>());
    }
    JsonArray v = resp.result()["values"].to<JsonArray>();
    for (auto& pv : settings_.values()) {
      JsonObject x = v.add<JsonObject>();
      std::string alias = aliasFor(AliasEntry::Kind::Key, pv.key);
      x["key"] = alias;
      const AliasEntry* a = alias == pv.key ? nullptr : aliasNamed(alias);
      Value val = pv.value;
      if (a && a->invert && val.type == Value::Type::Bool) val.b = !val.b;
      if (a && a->scale && val.type == Value::Type::Int) val.i /= a->scale;
      val.toJson(x, "value");
      // a secret: never its value, only whether one is set (the page shows "set" / "not set")
      const std::string& k = pv.key;
      if (k == "pin") x["set"] = !settings_.pin_hash.empty();
      else if (k == "wifi_pass") x["set"] = !settings_.wifi_pass.empty();
      else if (k == "ap_pass") x["set"] = !settings_.ap_pass.empty();
    }
    return resp;
  }
  if (op == "set_params") {
    std::vector<ParamValue> values;
    for (JsonVariantConst pv : r["values"].as<JsonArrayConst>()) {
      ParamValue x;
      x.key = pv["key"].as<std::string>();
      (void)Value::fromJson(pv["value"], x.value);
      values.push_back(x);
    }
    return setParams(r, values);
  }
  if (op == "get_script" || op == "check_script" || op == "set_script") return scriptOp(r);
  if (op == "list_patterns" || op == "put_pattern" || op == "delete_pattern") return patternOp(r);
  if (op == "get_backup" || op == "put_backup") return backupOp(r);
  if (op == "get_board" || op == "set_board") return boardOp(r);
  if (op == "get_defaults" || op == "apply_defaults") return defaultsOp(r);
  if (op == "wifi_scan") {
    if (!wifi_scan_.available()) return Response::err(r, ErrorCode::Unsupported, "this board has no Wi-Fi to look for networks with");
    // the radio is left alone while a firmware update comes in
    if (updating()) return Response::err(r, ErrorCode::Refused, update_text::IN_UPDATE_MODE);
    Millis retry = wifi_scan_.ask(now, r["rescan"] | false);
    Response resp = Response::ok(r, "wifi_scan");
    wifi_scan_.toJson(resp.result(), now, retry);
    return resp;
  }
  if (op == "get_profile") {
    Response resp = Response::ok(r, "profile");
    settings_.profile.toJson(resp.result());  // no topic_prefix when none is set
    return resp;
  }
  if (op == "set_profile") {
    Profile p;
    std::string why;
    if (!parseProfile(r["data"], p, why)) return Response::err(r, ErrorCode::BadRequest, why);
    // a stored script that uses a terminal alias keeps it (SS-N): renaming or removing it is refused
    if (!script_text_.empty()) {
      std::unique_ptr<Settings> next(new Settings(settings_));
      next->profile = p;
      std::unique_ptr<ScriptProgram> t(new ScriptProgram());
      ScriptBoard nb = scriptBoardFor(*next);
      ScriptCheck c;
      {
        Unlocked u(*this);
        c = scriptCompile(script_text_, script_sounds_, nb, *t);
      }
      if (!c.ok())
        return Response::err(r, ErrorCode::Invalid, "profile: the stored script uses " + scriptSubject(c.errors[0].text) + ". Change the script first.");
      if (!scriptSameTable(*t))
        return Response::err(r, ErrorCode::Invalid, "profile: the stored script would then switch another terminal. Change the script first.");
    }
    std::swap(settings_.profile, p);  // loading or removing aliases changes no value (SM-AL5)
    // a profile that would not fit the settings record is refused, as a show (one that does not make it bigger
    // always passes)
    if (store_) {
      size_t after = storedSize();
      if (after > store_->capacity()) {
        std::swap(settings_.profile, p);
        if (after > storedSize()) return Response::err(r, ErrorCode::Internal, STORAGE_FULL);
        std::swap(settings_.profile, p);
      }
    }
    markDirty();
    events_.push("params_changed");
    return Response::done(r);
  }
  if (op == "action") {
    std::string key = r["key"].as<std::string>();
    // a show-script effect's name, a control, or a profile's control alias (an effect's name wins, SS-R10)
    std::string c = resolveControl(key);
    if (c == "trigger" || c == "recover" || c == "cancel" || c.compare(0, 7, "script:") == 0) return effectAction(r, c);
    if (r["key"].as<std::string>() == "save") {
      if (dirty_) saveNow();  // flash wear: nothing to save, nothing written
      return Response::done(r);
    }
    return Response::err(r, ErrorCode::NotFound, "no such action");
  }
  if (op == "reboot") {
    if (dirty_) saveNow();
    patternBackground(true);  // patterns not yet written go to flash first
    runsSaveNow();
    reboot_ = true;
    return Response::done(r);
  }
  if (op == "factory_reset") {
    // everything: settings (with the Wi-Fi), shows and the show script; each wipe is power-safe (a cut part way
    // never brings an older record back), and a wipe that fails is said, without a restart. keep_wifi: the settings
    // record is replaced (one power-safe write) by the firmware's defaults with the name, the Wi-Fi network and its
    // password and the setup hotspot's password kept, marked as reset: the next power-on starts fresh (the
    // board's defaults) and puts those back, so the board stays on its network under its name
    bool a;
    if ((r["keep_wifi"] | false) && store_) {
      std::unique_ptr<Settings> kept(new Settings());
      kept->name = settings_.name;
      kept->wifi_ssid = settings_.wifi_ssid;
      kept->wifi_pass = settings_.wifi_pass;
      kept->ap_pass = settings_.ap_pass;
      JsonDocument d;
      d["version"] = STORED_VERSION;
      kept->toJson(d["settings"].to<JsonObject>());
      d["shows"].to<JsonArray>();
      d["reset"] = true;
      std::string rec;
      serializeJson(d, rec);
      a = store_->save(rec);
    } else {
      a = !store_ || store_->wipe();
    }
    if (a) stored_crc_known_ = false;  // the flash no longer holds the record in RAM
    bool b = (!script_store_ || script_store_->wipe()) && patternsWipe();
    // the board package is kept (the board keeps its pins), unless asked to forget it
    if (a && b && (r["forget_board"] | false) && board_store_ && !board_store_->wipe()) {
      store_errors++;
      log("factory reset: the board package could not be cleared from flash");
      return Response::err(r, ErrorCode::Internal, "factory reset failed: the board package could not be cleared from flash. Try again; nothing was restarted");
    }
    if (!a || !b) {
      store_errors++;
      log("factory reset: the flash could not be cleared");
      return Response::err(r, ErrorCode::Internal, std::string("factory reset failed: the ") + (a ? "show script" : "settings") +
                                                       " could not be cleared from flash. Try again; nothing was restarted");
    }
    runsSaveNow();  // the lifetime run count is kept (its own record, not wiped)
    // until the restart nothing is written back (a change in the meantime would bring settings back)
    wiped_ = true;
    dirty_ = false;
    reboot_ = true;
    return Response::done(r);
  }
  if (op == "reset_runs") {
    // the lifetime run count back to 0 (page lock: a change like any other); runs since power-on stay
    runs_.resetLifetime(now_);
    log("lifetime runs reset to 0");
    return Response::done(r);
  }
  if (op == "test_input") {
    // a test press: held until let go (active false), "ms" (at most TEST_PRESS_MAX_MS) or the page closing
    Input in = Input::Trig1;
    (void)reqInput(r, "input", in);
    int k = (int)in;
    if (!r["active"].as<bool>()) { testRelease(k, fxNow()); return Response::done(r); }
    uint32_t ms = r["ms"] | 0u;
    if (!ms && (origin_ == Origin::Http || origin_ == Origin::Mqtt))
      return Response::err(r, ErrorCode::Refused, "from plain HTTP or MQTT a test press needs \"ms\" (at most 30000); to hold it, use the phone page");
    if (!ms || ms > TEST_PRESS_MAX_MS) ms = TEST_PRESS_MAX_MS;
    Millis t = fxNow();
    if (!test_until_[k]) test_on_since_[k] = t;
    test_until_[k] = t + ms;
    test_session_[k] = origin_ == Origin::Session ? session_ : 0;
    return Response::done(r);
  }
  if (op == "sim_input") return Response::err(r, ErrorCode::Unsupported, "simulator only");
  return Response::err(r, ErrorCode::BadRequest, "unknown op");
}

Response Controller::setParams(const Request& r, const std::vector<ParamValue>& values) {
  // the serial console and MQTT come here directly: nothing changes while a firmware update runs
  if (updating()) return Response::err(r, ErrorCode::Refused, update_text::IN_UPDATE_MODE);
  // called directly by a sketch (Proptroller::setting ...) as well as by handle(): refused from the step while
  // another task's request has the lock given back (see handle())
  if (window_open_) return Response::err(r, ErrorCode::Refused, BUSY_TEXT);
  // the candidate settings on the heap (a Settings is about 1 KB: not on the request's stack)
  std::unique_ptr<Settings> next_box(new Settings(settings_));
  Settings& next = *next_box;
  for (auto& pv : values)
    if (pv.key == "pin") {
      // a fresh salt per board and per change
      next.pin_salt = newSalt(identity_.id + ":" + std::to_string(now_) + ":" + next.pin_salt);
      break;
    }
  for (auto& pv : values) {
    const ParamDesc* d = Settings::find(pv.key);
    if (!d) return Response::err(r, ErrorCode::NotFound, "unknown setting " + pv.key);
    // an empty secret means "keep the current one" (reads return secrets blank); the page password can be cleared
    if (d->isSecret() && pv.value == Value::text("") && pv.key != "pin") continue;
    if (pv.key == "pin" && pv.value.type == Value::Type::Text && !pv.value.s.empty() && pv.value.s.size() < PIN_MIN_LEN)
      return Response::err(r, ErrorCode::BadRequest, "the page password needs at least 6 characters");
    // in the words the page shows it under: a key alias's label and group (SM-AL2)
    ParamDesc shown = *d;
    for (auto& a : settings_.profile.aliases)
      if (a.kind == AliasEntry::Kind::Key && a.target == d->key) {
        if (!a.label.empty()) shown.label = a.label;
        if (!a.group.empty()) shown.group = a.group;
        break;
      }
    std::string why = shown.explain(pv.value);
    if (!why.empty()) return Response::err(r, ErrorCode::BadRequest, why);
    const char* e = next.set(pv.key, pv.value);
    if (e) return Response::err(r, ErrorCode::BadRequest, shown.label + ": " + e);
  }
  // a new network without a password in the same change is an open one: the old network's password is not kept
  // for it (an empty secret otherwise means "keep"; an unchanged network keeps its password)
  if (next.wifi_ssid != settings_.wifi_ssid) {
    bool pass_given = false;
    for (auto& pv : values) pass_given |= pv.key == "wifi_pass" && !(pv.value == Value::text(""));
    if (!pass_given) next.wifi_pass.clear();
  }
  // a trigger from the network: a phone page's enable lasts while the page is open (SM-R2); plain HTTP cannot
  // give it
  if (next.fx.remote_trigger_enable && !settings_.fx.remote_trigger_enable) {
    if (origin_ == Origin::Http)
      return Response::err(r, ErrorCode::BadRequest, "turn the remote trigger on from the phone page, MQTT or the serial console; from a page it stays on only while that page is open");
    fx_session_ = origin_ == Origin::Session ? session_ : 0;
    fx_by_ = origin_ == Origin::Session ? "web:" + std::to_string(session_) : origin_ == Origin::Mqtt ? "mqtt" : "serial";
  } else if (!next.fx.remote_trigger_enable) {
    fx_session_ = 0;
    fx_by_.clear();
  }
  if (next.page_lock && next.pin_hash.empty())
    return Response::err(r, ErrorCode::BadRequest, "set a page password first (Settings \xe2\x86\x92 Device \xe2\x86\x92 Page password)");
  // a stored script must still pass with the new settings (the DMX port, roles, the built-in effect, the
  // terminals' names) and still drive the same terminals (a name moved to another terminal)
  // ("<key>: the stored script uses <x>. Change the script first.")
  if (!script_text_.empty()) {
    std::unique_ptr<ScriptProgram> t(new ScriptProgram());
    ScriptBoard nb = scriptBoardFor(next);
    bool still_ok = false;
    {
      Unlocked u(*this);
      still_ok = scriptCompile(script_text_, script_sounds_, nb, *t).ok();
    }
    if (still_ok) still_ok = scriptSameTable(*t);
    if (!still_ok) {
      std::unique_ptr<Settings> one_box(new Settings(settings_));
      for (auto& pv : values) {
        Settings& one = *one_box;
        one = settings_;
        if (one.set(pv.key, pv.value)) continue;
        ScriptCheck c = scriptCompile(script_text_, script_sounds_, scriptBoardFor(one), *t);
        if (c.ok() && scriptSameTable(*t)) continue;
        if (c.ok()) return Response::err(r, ErrorCode::Invalid, pv.key + ": the stored script would then switch another terminal. Change the script first.");
        return Response::err(r, ErrorCode::Invalid, pv.key + ": the stored script uses " + scriptSubject(c.errors[0].text) + ". Change the script first.");
      }
      return Response::err(r, ErrorCode::Invalid, "the stored script would no longer pass the check or would switch other terminals. Change the script first.");
    }
  }
  for (size_t i = 0; i < engine_.showCount(); i++) {
    const Show& s = engine_.showAt(i);
    char b[24];
    (void)snprintf(b, sizeof b, "show %u ", s.id);
    std::string m = checkReserved(s, next);
    if (!m.empty()) return Response::err(r, ErrorCode::Invalid, std::string(b) + "uses it: " + m);
  }
  // a longer name (or any text) must still fit the settings record (one that does not make it bigger always passes)
  bool text = false;
  for (auto& pv : values) text |= pv.value.type == Value::Type::Text;
  if (store_ && text) {
    std::swap(settings_, next);
    size_t after = storedSize();
    std::swap(settings_, next);
    if (after > store_->capacity() && after > storedSize()) return Response::err(r, ErrorCode::Internal, STORAGE_FULL);
  }
  bool df_changed = next.df_ack != settings_.df_ack || next.df_checksum != settings_.df_checksum;
  // flash wear: the same values again (a retained MQTT config/set redelivered at every reconnect, a value not
  // stored such as remote_trigger_enable) mark nothing to save
  bool stored_changed = !(next == settings_);
  settings_ = next;
  applySettings();
  if (df_changed) df_ = DfPlayer(dfConfig(settings_, false));
  if (stored_changed) markDirty();
  events_.push("params_changed");
  return Response::done(r);
}

// ---------------------------------------------------------------- MQTT (proptroller/<name>/...)
//
// | Topic                   | Direction     | Payload                                                     |
// |-------------------------|---------------|-------------------------------------------------------------|
// | proptroller/discover    | in            | anything: every controller answers on proptroller/announce  |
// | proptroller/announce    | out           | the discovery announce JSON                                 |
// | proptroller/broadcast/getName | in      | anything: every device answers on proptroller/broadcast/name|
// | <p>/online              | out, retained | true; the last will sets false                              |
// | <p>/state               | out, retained | the State JSON, when it changes, at most once a second      |
// | <p>/event               | out           | event JSON (notices, logs, runs)                            |
// | <p>/event/run           | out           | a run's data: source, kind, name, counts, uptime_ms         |
// | <p>/status/<key>        | out, retained | run counters always; the effect's values while it is on     |
// | <p>/rpc                 | in            | a request envelope (as POST /api/v1/rpc, PIN included)      |
// | <p>/rpc/reply           | out           | the response                                                |
// | <p>/config/get/<key>    | in            | anything: the value is published on <p>/config/<key>        |
// | <p>/config/set/<key>    | in            | the value; the new value is published                       |
// | <p>/config/<key>        | out, retained | the value (secrets are never published)                     |
// | <p>/cue                 | in            | a cue name: starts it                                       |
//
// A session keeps the prefix it subscribed with; a rename or a broker change ends it (mqttEpoch).


bool Controller::mqttTarget(std::string& host, uint16_t& port) {
  if (settings_.mqtt_broker.empty()) return false;
  host = settings_.mqtt_broker;
  port = settings_.mqtt_port;
  return true;
}

std::string Controller::mqttPrefixNow() const {
  std::string label = hostLabel(settings_.name, identity_.id);
  // a device never answers on the shared topics
  if (label == "broadcast" || label == "discover" || label == "announce") label += "-" + (identity_.id.size() >= 4 ? identity_.id.substr(identity_.id.size() - 4) : identity_.id);
  return topicRoot() + "/" + label;  // a profile's topic_prefix replaces "proptroller" (SM-AL6)
}
std::string Controller::mqttPrefix() const { return mqtt_.prefix.empty() ? mqttPrefixNow() : mqtt_.prefix; }
MqttMsg Controller::mqttWill() { return MqttMsg(mqttPrefixNow() + "/online", "false", true); }

std::vector<std::string> Controller::mqttSubscriptions() {
  mqtt_.prefix = mqttPrefixNow();
  mqtt_.has_target = mqttTarget(mqtt_.host, mqtt_.port);
  const std::string& p = mqtt_.prefix;
  return {p.substr(0, p.find('/')) + "/broadcast/getName", p + "/rpc", p + "/config/get/+", p + "/config/set/+", p + "/cue", p + "/control/+",
          p + "/status/get/+", p + "/script/set", p + "/script/check"};
}

// Every setting (under its alias when the profile gives one, SM-AL2) and the effect's status values.
static std::vector<MqttMsg> configMsgs(const Settings& s, const std::string& p, const std::vector<ParamValue>& status) {
  std::vector<MqttMsg> v;
  for (auto& pv : s.values()) {
    const ParamDesc* d = Settings::find(pv.key);
    if (d && d->isSecret()) continue;
    std::string key = pv.key;
    Value val = pv.value;
    for (auto& a : s.profile.aliases)
      if (a.kind == AliasEntry::Kind::Key && a.target == pv.key) {
        key = a.alias;
        if (a.invert && val.type == Value::Type::Bool) val.b = !val.b;
        if (a.scale && val.type == Value::Type::Int) val.i /= a.scale;
      }
    v.push_back(MqttMsg((p + "/config/").append(key), val.plain(), true));
  }
  for (auto& st : status) v.push_back(MqttMsg(p + "/status/" + st.key, st.value.plain(), true));
  return v;
}

// configMsgs from a copy of the settings, with the lock given back: every setting is about 100 messages, several
// ms of work that would hold up the step at each MQTT connect
std::vector<MqttMsg> Controller::configMsgsUnlocked(const std::string& p, const std::vector<ParamValue>& status) {
  std::unique_ptr<Settings> s(new Settings(settings_));  // about 1 KB: on the heap, not the network task's stack
  Unlocked u(*this);
  return configMsgs(*s, p, status);
}

std::vector<MqttMsg> Controller::mqttOnConnect() {
  std::string p = mqttPrefix();
  std::vector<MqttMsg> v = {MqttMsg(p + "/online", "true", true)};
  std::vector<ParamValue> status;
  effectStatus(status);
  runStatus(status);
  auto cfg = configMsgsUnlocked(p, status);
  mqtt_.published.clear();
  for (auto& m : cfg) mqtt_.published.push_back({m.topic, m.payload});
  v.insert(v.end(), cfg.begin(), cfg.end());
  std::string st = stateJson();
  mqtt_.last_state = st;
  mqtt_.last_at = now_;
  mqtt_.seq = events_.seq();
  v.push_back(MqttMsg(p + "/state", st, true));
  return v;
}

std::vector<MqttMsg> Controller::mqttOnMessage(const std::string& topic, const std::string& payload) {
  std::string p = mqttPrefix();
  // the only broadcast request: every device answers with its topic name (settings and controls are per device)
  std::string root = p.substr(0, p.find('/'));
  if (topic == root + "/broadcast/getName") return {MqttMsg(root + "/broadcast/name", p.substr(root.size() + 1), false)};
  if (topic.compare(0, p.size() + 1, p + "/") != 0) return {};
  std::string rest = topic.substr(p.size() + 1);
  auto err = [&](const std::string& m) { return std::vector<MqttMsg>{MqttMsg(p + "/error", m, false)}; };
  struct OriginGuard {
    Controller& c;
    Origin o;
    uint32_t s;
    ~OriginGuard() { c.setOrigin(o, s); }
  } guard{*this, origin_, session_};
  setOrigin(Origin::Mqtt);
  if (rest == "rpc") return {MqttMsg(p + "/rpc/reply", call(payload.data(), payload.size()), false)};
  // show scripts: the payload is set_script's {"text", "sounds"}; the check is published on script/result
  if (rest == "script/set" || rest == "script/check") {
    bool set = rest == "script/set";
    if (set && locked()) return err("the page is locked: use rpc with the page password");
    JsonDocument in;
    if (deserializeJson(in, payload) || !in.is<JsonObject>()) return err("script: send {\"text\": \"...\", \"sounds\": {}}");
    in["op"] = set ? "set_script" : "check_script";
    std::string body;
    serializeJson(in, body);
    JsonDocument out;
    deserializeJson(out, call(body.data(), body.size()));
    if (!(out["ok"] | false)) return err(out["error"]["message"] | "script refused");
    std::string res;
    serializeJson(out["result"], res);
    return {MqttMsg(p + "/script/result", res, false)};
  }
  if (rest.compare(0, 8, "control/") == 0) {
    // an alias works as its control; errors name the control
    std::string c = resolveControl(rest.substr(8));
    // locked: MQTT is read-only, but for a trigger, which has its own gate (remote_trigger_enable)
    // a script effect that needs remote_trigger_enable has that gate too; one started without it does not
    bool gated = c == "trigger" || (c.compare(0, 7, "script:") == 0 && !((scriptFreeEffects() >> atoi(c.c_str() + 7)) & 1));
    if (locked() && !gated && !c.empty()) return err("the page is locked: " + rest.substr(8) + " on the web page with the page password");
    if (c.empty()) return err("unknown control");
    Request req;
    Response r = effectAction(req, c);
    if (!r.isOk() && c != "restart") return err((c.compare(0, 7, "script:") == 0 ? rest.substr(8) : c) + ": " + r.errorMessage());
    return {};
  }
  if (rest.compare(0, 11, "status/get/") == 0) {
    std::string k = rest.substr(11);
    Value v;
    if (!statusRead(k, v)) return err("unknown status");
    return {MqttMsg(p + "/status/" + k, v.plain(), true)};
  }
  if (rest == "cue") {
    if (locked()) return err("the page is locked: cues over MQTT need the page lock off");
    std::string n = payload;
    while (!n.empty() && isspace((unsigned char)n.back())) n.pop_back();
    while (!n.empty() && isspace((unsigned char)n.front())) n.erase(n.begin());
    cueIn(n);
    return {};
  }
  if (rest.compare(0, 11, "config/get/") == 0 || rest.compare(0, 11, "config/set/") == 0) {
    bool set = rest[7] == 's';
    std::string name = rest.substr(11);
    // a key alias works as its key (SM-AL1); the answer comes back under the name asked for
    const AliasEntry* a = aliasNamed(name);
    if (set && locked()) return err("the page is locked: use rpc with the page password");
    // a fixed name always answers its value; a value set under it changes nothing
    if (a && a->kind == AliasEntry::Kind::Fixed) return {MqttMsg(p + "/config/" + name, a->target, true)};
    if (a && a->kind != AliasEntry::Kind::Key) a = nullptr;
    std::string key = a ? a->target : name;
    const ParamDesc* d = Settings::find(key);
    if (!d) return err("unknown setting");
    if (set) {
      Value v;
      std::string e;
      if (!d->parsePlain(payload, v, e)) return err(name + ": " + e);
      if (a) {
        if (a->invert && v.type == Value::Type::Bool) v.b = !v.b;
        if (a->scale && v.type == Value::Type::Int) {
          int64_t lo = aliasMin(d->min, a->scale), hi = aliasMax(d->max, a->scale);
          if (v.i < lo || v.i > hi) return err(name + ": allows " + std::to_string((long long)lo) + "-" + std::to_string((long long)hi) + (a->unit.empty() ? "" : " " + a->unit));
          v.i *= a->scale;
        }
      }
      Request req;
      Response resp = setParams(req, withAlso(a, key, v));
      if (!resp.isOk()) return err(name + ": " + resp.errorMessage());
    }
    if (d->isSecret()) return {};
    Value now;
    if (!settings_.get(key, now)) return {};
    if (a && a->invert && now.type == Value::Type::Bool) now.b = !now.b;
    if (a && a->scale && now.type == Value::Type::Int) now.i /= a->scale;
    return {MqttMsg(p + "/config/" + name, now.plain(), true)};
  }
  return {};
}

std::vector<MqttMsg> Controller::mqttPoll() {
  if (!mqtt_.prefix.empty()) {
    std::string host;
    uint16_t port = 0;
    bool has = mqttTarget(host, port);
    if (mqttPrefixNow() != mqtt_.prefix || has != mqtt_.has_target || host != mqtt_.host || port != mqtt_.port) {
      // renamed, a new topic_prefix, or another broker: end this session so the client reconnects; nothing more
      // is published on the old connection
      mqtt_.epoch++;
      mqtt_.prefix.clear();
      return {};
    }
  }
  std::string p = mqttPrefix();
  std::vector<MqttMsg> v;
  std::vector<std::string> evs;
  uint32_t seq = events_.since(mqtt_.seq, evs);
  mqtt_.seq = seq;
  bool params_changed = false;
  for (auto& e : evs) {
    params_changed |= e.find("\"event\":\"params_changed\"") != std::string::npos;
    v.push_back(MqttMsg(p + "/event", e, false));
    // a run also on its own topic, its data alone: a fleet server logs trigger times from it
    size_t at = e.find("\"event\":\"run\",\"data\":");
    if (at != std::string::npos && e.size() >= 2) {
      size_t from = at + strlen("\"event\":\"run\",\"data\":");
      v.push_back(MqttMsg(p + "/event/run", e.substr(from, e.size() - 1 - from), false));
    }
  }
  // the run counters' status values: a run changes them, so they go out at once (three values, compared here)
  {
    std::vector<ParamValue> rs;
    runStatus(rs);
    for (auto& st : rs) {
      MqttMsg m(p + "/status/" + st.key, st.value.plain(), true);
      bool found = false;
      for (auto& pub : mqtt_.published)
        if (pub.first == m.topic) {
          found = true;
          if (pub.second != m.payload) { pub.second = m.payload; v.push_back(m); }
        }
      if (!found) { mqtt_.published.push_back({m.topic, m.payload}); v.push_back(m); }
    }
  }
  // the effect's status values: a change goes out within MQTT_STATUS_MIN_MS; a time left once a second
  if (now_ - mqtt_.status_at >= MQTT_STATUS_MIN_MS) {
    mqtt_.status_at = now_;
    bool time_left = now_ - mqtt_.time_left_at >= MQTT_STATUS_PERIOD_MS;
    if (time_left) mqtt_.time_left_at = now_;
    std::vector<ParamValue> status;
    effectStatus(status);
    for (auto& st : status) {
      const AliasEntry* al = aliasNamed(st.key);
      const std::string& base = al && al->kind == AliasEntry::Kind::Status ? al->target : st.key;
      if (!time_left && base.size() > 13 && base.compare(base.size() - 13, 13, "_remaining_ms") == 0) continue;
      MqttMsg m(p + "/status/" + st.key, st.value.plain(), true);
      bool found = false;
      for (auto& pub : mqtt_.published)
        if (pub.first == m.topic) {
          found = true;
          if (pub.second != m.payload) { pub.second = m.payload; v.push_back(m); }
        }
      if (!found) { mqtt_.published.push_back({m.topic, m.payload}); v.push_back(m); }
    }
  }
  if (params_changed) {
    for (auto& m : configMsgsUnlocked(p, {})) {
      bool found = false;
      for (auto& pub : mqtt_.published)
        if (pub.first == m.topic) {
          found = true;
          if (pub.second != m.payload) { pub.second = m.payload; v.push_back(m); }
        }
      if (!found) { mqtt_.published.push_back({m.topic, m.payload}); v.push_back(m); }
    }
  }
  if (now_ - mqtt_.last_at >= 1000) {
    std::string st = stateJson();
    // uptime changes every second: compare the state without it
    auto strip = [](const std::string& s) {
      size_t a = s.find("\"uptime_ms\":");
      if (a == std::string::npos) return s;
      size_t b = s.find(',', a);
      return s.substr(0, a) + s.substr(b);
    };
    if (strip(st) != strip(mqtt_.last_state)) {
      mqtt_.last_state = st;
      mqtt_.last_at = now_;
      v.push_back(MqttMsg(p + "/state", st, true));
    }
  }
  return v;
}

// ---------------------------------------------------------------- a board support package's defaults

std::string Controller::applyFactoryDefaults(const FactoryDefaults& d, bool force) {
  if (!fresh_ && !force) return "";
  std::string refused;
  auto note = [&](const std::string& what) {
    refused += (refused.empty() ? "" : "\n") + what;
    log("board defaults: " + what);
  };
  // an answer of call(): ok, or the error's message
  auto failed = [](const std::string& answer, std::string& why) {
    JsonDocument a;
    if (deserializeJson(a, answer) || !a["ok"].as<bool>()) {
      why = a["error"]["message"] | "not accepted";
      return true;
    }
    if (a["result"]["ok"].is<bool>() && !a["result"]["ok"].as<bool>()) {  // a script the checker refused
      why = a["result"]["errors"][0]["message"] | "the checker refused it";
      return true;
    }
    return false;
  };
  if (d.profile && *d.profile) {
    std::string body = std::string("{\"op\":\"set_profile\",\"data\":") + d.profile + "}", why;
    if (failed(call(body.data(), body.size()), why)) note("profile: " + why);
  }
  if (d.settings) {
    const char* p = d.settings;
    while (*p) {
      const char* e = p;
      while (*e && *e != '\n') e++;
      std::string line(p, (size_t)(e - p));
      p = *e ? e + 1 : e;
      size_t hash = line.find('#');
      if (hash != std::string::npos) line.resize(hash);
      while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
      while (!line.empty() && isspace((unsigned char)line.front())) line.erase(line.begin());
      if (line.empty()) continue;
      std::string out = consoleLine(line);
      if (out.compare(0, 6, "error:") == 0) note(line + ": " + out.substr(7));
    }
  }
  if (d.script && *d.script) {
    JsonDocument q;
    q["op"] = "set_script";
    q["text"] = d.script;
    std::string body, why;
    serializeJson(q, body);
    if (failed(call(body.data(), body.size()), why)) note("script: " + why);
  }
  return refused;
}

}  // namespace pt
