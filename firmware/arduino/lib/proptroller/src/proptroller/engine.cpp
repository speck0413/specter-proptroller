#include "engine.h"
#include <algorithm>

namespace pt {

static const char* const CAUSE_NAMES[] = {"input", "release", "timer", "boot", "cue", "after", "api"};
PT_ONE_PER(CAUSE_NAMES, Cause::By::ByApi);
const char* Cause::byName() const { return CAUSE_NAMES[idx(by)]; }
bool Cause::operator==(const Cause& o) const {
  if (by != o.by) return false;
  if (by == By::ByInput || by == By::ByRelease) return input == o.input;
  if (by == By::ByCue) return name == o.name;
  if (by == By::ByAfter) return show == o.show;
  return true;
}

const char* endReasonName(EndReason r) {
  static const char* const n[] = {"finished", "stopped", "restarted", "paused", "replaced", "released"};
  return n[(int)r];
}
const char* ignoredName(Ignored i) {
  static const char* const n[] = {"", "running", "cooldown", "disabled", "paused", "unknown"};
  return n[(int)i];
}

bool Notice::operator==(const Notice& o) const {
  if (kind != o.kind) return false;
  switch (kind) {
    case Kind::Started: return show == o.show && cause == o.cause;
    case Kind::Ended: return show == o.show && reason == o.reason;
    case Kind::IgnoredTrigger: return show == o.show && why == o.why;
    case Kind::Watchdog: case Kind::Interlock: return channel == o.channel;
    case Kind::PausedChanged: return paused == o.paused;
  }
  return true;
}

Engine::Rt::Rt(const Show& s) : show(s) {
  for (auto& t : show.tracks) compiled.emplace_back(t.target.isChannel() ? new Compiled(t) : nullptr);
  length = show.length();
  has_next.assign(show.triggers.size(), false);
  next_every.assign(show.triggers.size(), 0);
}

Engine::Engine(const EngineConfig& cfg) : cfg_(cfg) { rebuildShapers(); }

// New limits keep each output's on-time: a settings change never restarts a maximum on-time.
void Engine::rebuildShapers() {
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    const OutputConfig& o = cfg_.outputs[i];
    Shaper fresh(isPwm(ALL_CHANNELS[i]) ? o.soft_start_ms : 0, o.max_on_ms);
    shapers_[i].soft_start_ms = fresh.soft_start_ms;
    shapers_[i].max_on_ms = fresh.max_on_ms;
  }
}

void Engine::setConfig(const EngineConfig& cfg) {
  cfg_ = cfg;
  rebuildShapers();
}

int Engine::indexOf(uint16_t id) const {
  for (size_t i = 0; i < shows_.size(); i++)
    if (shows_[i].show.id == id) return (int)i;
  return -1;
}

const Show* Engine::show(uint16_t id) const {
  int i = indexOf(id);
  return i < 0 ? nullptr : &shows_[i].show;
}

ShowError Engine::setShow(Show s) {
  ShowError e = validate(s);
  if (!e.ok()) return e;
  s.normalize();
  int i = indexOf(s.id);
  if (i >= 0) {
    endRunsOf(s.id, EndReason::Replaced);
    Rt rt(s);
    rt.window = shows_[i].window;
    rt.boot_done = shows_[i].boot_done;
    shows_[i] = std::move(rt);
  } else {
    if (shows_.size() >= MAX_SHOWS) { e.problem = Problem::TooBig; return e; }
    shows_.emplace_back(s);
    std::stable_sort(shows_.begin(), shows_.end(), [](const Rt& a, const Rt& b) { return a.show.id < b.show.id; });
  }
  return e;
}

bool Engine::deleteShow(uint16_t id) {
  endRunsOf(id, EndReason::Replaced);
  size_t n = shows_.size();
  shows_.erase(std::remove_if(shows_.begin(), shows_.end(), [id](const Rt& r) { return r.show.id == id; }), shows_.end());
  return n != shows_.size();
}

bool Engine::isRunning(uint16_t id) const {
  for (auto& r : runs_)
    if (r.show == id) return true;
  return false;
}

bool Engine::status(uint16_t id, Millis now, ShowStatus& out) const {
  int i = indexOf(id);
  if (i < 0) return false;
  const Rt& rt = shows_[i];
  out = ShowStatus();
  for (auto& r : runs_)
    if (r.show == id) {
      out.kind = ShowStatus::Kind::Running;
      out.elapsed_ms = (uint32_t)(now > r.start ? now - r.start : 0);
      out.length_ms = rt.length;
      return true;
    }
  if (!rt.show.enabled) { out.kind = ShowStatus::Kind::Disabled; return true; }
  if (rt.window.isOpen(now) && rt.window.left(now, rt.show.runs_per_cooldown) == 0) {
    out.kind = ShowStatus::Kind::Cooldown;
    out.remaining_ms = rt.window.remainingMs(now);
    return true;
  }
  out.kind = ShowStatus::Kind::Idle;
  return true;
}

bool Engine::runsLeft(uint16_t id, Millis now, uint32_t& left, uint32_t& window_ms) const {
  int i = indexOf(id);
  if (i < 0 || shows_[i].show.runs_per_cooldown <= 1) return false;
  const Rt& rt = shows_[i];
  left = rt.window.left(now, rt.show.runs_per_cooldown);
  window_ms = rt.window.isOpen(now) ? rt.window.remainingMs(now) : 0;
  return true;
}

void Engine::setManual(Channel c, bool on, Level level) {
  has_manual_[index(c)] = on;
  manual_[index(c)] = level;
}

bool Engine::manual(Channel c, Level& level) const {
  if (!has_manual_[index(c)]) return false;
  level = manual_[index(c)];
  return true;
}

void Engine::setManualPixel(const Effect* e) {
  has_manual_pixel_ = e != nullptr;
  if (e) manual_pixel_ = *e;
  manual_pixel_fresh_ = e != nullptr;
}

// ---------------------------------------------------------------- triggers

Ignored Engine::tryStart(int idx, const Cause& c, Millis now) {
  uint16_t id = shows_[idx].show.id;
  Ignored r = startInner(idx, c, now);
  if (r != Ignored::None) notices_.push_back(Notice::ignored(id, r));
  return r;
}

Ignored Engine::startInner(int idx, const Cause& c, Millis now) {
  uint16_t id = shows_[idx].show.id;
  if (paused_) return Ignored::Paused;
  if (!shows_[idx].show.enabled) return Ignored::Disabled;
  if (isRunning(id)) {
    switch (shows_[idx].show.retrigger) {
      case Retrigger::Ignore: return Ignored::Running;
      case Retrigger::Restart:
        endRunsOf(id, EndReason::Restarted);
        startRun(idx, c, now);
        return Ignored::None;
      case Retrigger::Stop:
        endRunsOf(id, EndReason::Stopped);
        return Ignored::None;
    }
  }
  Rt& rt = shows_[idx];
  if (!rt.window.allows(now, rt.show.runs_per_cooldown)) return Ignored::Cooldown;
  // only a fresh start counts; a restart or a loop of the same run does not
  rt.window.countRun(now);
  if (fresh_.size() >= FRESH_RUNS_MAX) fresh_.erase(fresh_.begin());  // nobody takes them (an engine on its own)
  fresh_.push_back(FreshRun{id, c});
  startRun(idx, c, now);
  return Ignored::None;
}

void Engine::startRun(int idx, const Cause& c, Millis now) {
  seq_++;
  const Rt& rt = shows_[idx];
  Run run;
  run.show = rt.show.id;
  run.start = now;
  run.seq = seq_;
  run.cursor.assign(rt.show.tracks.size(), 0);
  if (c.by == Cause::By::ByInput)
    for (auto& t : rt.show.triggers)
      if (t.on == ShowTrigger::Kind::OnInput && t.input == c.input && t.until_release) { run.held = true; run.input = c.input; }
  notices_.push_back(Notice::started(rt.show.id, c));
  runs_.push_back(run);
}

void Engine::endRunsOf(uint16_t id, EndReason reason) {
  Millis now = last_now_;
  size_t i = 0;
  while (i < runs_.size()) {
    if (runs_[i].show == id) {
      Run run = runs_[i];
      runs_.erase(runs_.begin() + i);
      finishRun(run, reason, now);
    } else {
      i++;
    }
  }
}

void Engine::finishRun(const Run& run, EndReason reason, Millis now) {
  int i = indexOf(run.show);
  if (i >= 0) {
    Rt& rt = shows_[i];
    // the cooldown starts when the (first) run ends
    if (reason != EndReason::Restarted && reason != EndReason::Replaced) rt.window.open(now, rt.show.cooldown_ms);
    if (reason != EndReason::Finished && run.played_sound && rt.show.stop_sound)
      out_sound_.push_back(SoundCommand::simple(SoundCommand::Kind::Stop));
  }
  if ((reason == EndReason::Finished || reason == EndReason::Released)) pending_after_.push_back(run.show);
  notices_.push_back(Notice::ended(run.show, reason));
}

Ignored Engine::pressed(Millis now, Input i) {
  bump(now);
  std::vector<uint16_t> s;
  if (cfg_.has_pause_input && cfg_.pause_input == i) { setPaused(true); return Ignored::None; }
  (void)press(now, i, s);
  return Ignored::None;
}

Ignored Engine::released(Millis now, Input i) {
  bump(now);
  if (cfg_.has_pause_input && cfg_.pause_input == i) { setPaused(false); return Ignored::None; }
  // "on while held": the runs this input's press started end now
  size_t k = 0;
  while (k < runs_.size()) {
    if (runs_[k].held && runs_[k].input == i) {
      Run run = runs_[k];
      runs_.erase(runs_.begin() + k);
      finishRun(run, EndReason::Released, now);
    } else {
      k++;
    }
  }
  for (size_t idx = 0; idx < shows_.size(); idx++) {
    bool m = false;
    for (auto& t : shows_[idx].show.triggers) m |= t.on == ShowTrigger::Kind::OnRelease && t.input == i;
    if (m) {
      Cause c;
      c.by = Cause::By::ByRelease;
      c.input = i;
      (void)tryStart((int)idx, c, now);
    }
  }
  return Ignored::None;
}

Ignored Engine::press(Millis now, Input i, std::vector<uint16_t>& started) {
  bump(now);
  started.clear();
  if (cfg_.has_pause_input && cfg_.pause_input == i) { setPaused(true); return Ignored::None; }
  std::vector<int> idxs;
  for (size_t idx = 0; idx < shows_.size(); idx++) {
    bool m = false;
    for (auto& t : shows_[idx].show.triggers) m |= t.on == ShowTrigger::Kind::OnInput && t.input == i;
    if (m) idxs.push_back((int)idx);
  }
  if (idxs.empty()) return paused_ ? Ignored::Paused : Ignored::None;
  Ignored why = Ignored::Unknown;
  for (int idx : idxs) {
    Cause c;
    c.by = Cause::By::ByInput;
    c.input = i;
    Ignored r = tryStart(idx, c, now);
    if (r == Ignored::None) started.push_back(shows_[idx].show.id);
    else why = r;
  }
  return started.empty() ? why : Ignored::None;
}

void Engine::cue(Millis now, const std::string& name) {
  bump(now);
  for (size_t idx = 0; idx < shows_.size(); idx++) {
    bool m = false;
    for (auto& t : shows_[idx].show.triggers) m |= t.on == ShowTrigger::Kind::Cue && t.name == name;
    if (m) {
      Cause c;
      c.by = Cause::By::ByCue;
      c.name = name;
      (void)tryStart((int)idx, c, now);
    }
  }
}

Ignored Engine::run(Millis now, uint16_t id) {
  bump(now);
  int idx = indexOf(id);
  if (idx < 0) return Ignored::Unknown;
  Cause c;
  c.by = Cause::By::ByApi;
  return tryStart(idx, c, now);
}

void Engine::stop(Millis now, uint16_t id) {
  bump(now);
  endRunsOf(id, EndReason::Stopped);
}

void Engine::stopAll(Millis now) {
  bump(now);
  std::vector<uint16_t> ids;
  ids.reserve(runs_.size());
  for (auto& r : runs_) ids.push_back(r.show);
  for (auto id : ids) endRunsOf(id, EndReason::Stopped);
  for (int i = 0; i < CHANNEL_COUNT; i++) has_manual_[i] = false;
  has_manual_pixel_ = false;
}

void Engine::pause(Millis now, bool on) {
  bump(now);
  setPaused(on);
}

void Engine::setPaused(bool p) {
  if (p == paused_) return;
  if (p) {
    std::vector<uint16_t> ids;
    ids.reserve(runs_.size());
    for (auto& r : runs_) ids.push_back(r.show);
    for (auto id : ids) endRunsOf(id, EndReason::Paused);
    // manual tests end too: nothing comes back on when the pause is released
    for (auto& m : has_manual_) m = false;
    has_manual_pixel_ = false;
    out_sound_.push_back(SoundCommand::simple(SoundCommand::Kind::Stop));
  } else {
    // and nothing set during the pause (a sketch's turnOn) comes on at its release
    for (auto& m : has_manual_) m = false;
    has_manual_pixel_ = false;
  }
  paused_ = p;
  Notice n;
  n.kind = Notice::Kind::PausedChanged;
  n.paused = p;
  notices_.push_back(n);
}

// ---------------------------------------------------------------- tick

Frame Engine::tick(Millis now_in) {
  bump(now_in);
  Millis now = last_now_;

  // timers and boot triggers
  for (size_t idx = 0; idx < shows_.size(); idx++) {
    for (size_t ti = 0; ti < shows_[idx].show.triggers.size(); ti++) {
      const ShowTrigger tr = shows_[idx].show.triggers[ti];
      if (tr.on == ShowTrigger::Kind::Every) {
        Rt& rt = shows_[idx];
        if (!rt.has_next[ti]) { rt.has_next[ti] = true; rt.next_every[ti] = now + tr.ms; }
        Millis next = rt.next_every[ti];
        if (now >= next) {
          rt.next_every[ti] = next + tr.ms;
          Cause c;
          c.by = Cause::By::ByTimer;
          (void)tryStart((int)idx, c, now);
        }
      } else if (tr.on == ShowTrigger::Kind::Boot) {
        if (!shows_[idx].boot_done && now >= tr.ms) {
          shows_[idx].boot_done = true;
          Cause c;
          c.by = Cause::By::ByBoot;
          (void)tryStart((int)idx, c, now);
        }
      }
    }
  }
  // cues emitted last tick, and followers of shows that ended
  std::vector<std::string> cues;
  cues.swap(pending_cues_);
  for (auto& n : cues) cue(now, n);
  std::vector<uint16_t> after;
  after.swap(pending_after_);
  for (uint16_t id : after) {
    for (size_t idx = 0; idx < shows_.size(); idx++) {
      bool m = false;
      for (auto& t : shows_[idx].show.triggers) m |= t.on == ShowTrigger::Kind::After && t.show == id;
      if (m) {
        Cause c;
        c.by = Cause::By::ByAfter;
        c.show = id;
        (void)tryStart((int)idx, c, now);
      }
    }
  }

  // edge cues (sound, net) and run ends
  size_t i = 0;
  while (i < runs_.size()) {
    int si = indexOf(runs_[i].show);
    if (si < 0) { runs_.erase(runs_.begin() + i); continue; }
    Run& run = runs_[i];
    Millis elapsed = now > run.start ? now - run.start : 0;
    const Rt& rt = shows_[si];
    bool played = false;
    for (size_t ti = 0; ti < rt.show.tracks.size(); ti++) {
      const Track& tr = rt.show.tracks[ti];
      size_t& cur = run.cursor[ti];
      while (cur < tr.cues.size() && tr.cues[cur].at <= elapsed) {
        const Cue& c = tr.cues[cur];
        if (tr.target.kind == Target::Kind::SOUND) {
          SoundCommand sc;
          if (c.action.sound(sc)) {
            if (sc.startsPlayback()) played = true;
            out_sound_.push_back(sc);
          }
        } else if (tr.target.kind == Target::Kind::NET && c.action.kind == Action::Kind::Emit) {
          out_cues_.push_back(c.action.cue);
          pending_cues_.push_back(c.action.cue);
        }
        cur++;
      }
    }
    if (played) run.played_sound = true;
    uint64_t length = rt.length;
    if (elapsed >= length) {
      if (rt.show.looping) {
        run.start += length > MIN_LOOP_MS ? length : MIN_LOOP_MS;
        for (auto& c : run.cursor) c = 0;
        i++;
      } else {
        Run done = run;
        runs_.erase(runs_.begin() + i);
        finishRun(done, EndReason::Finished, now);
      }
    } else {
      i++;
    }
  }

  // arbitration
  Level req[CHANNEL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
  Driver drivers[CHANNEL_COUNT];
  if (!paused_) {
    for (int ci = 0; ci < CHANNEL_COUNT; ci++) {
      Channel c = ALL_CHANNELS[ci];
      Target tc = Target::of(c);
      bool have = false;
      uint8_t best_pri = 0;
      uint64_t best_seq = 0;
      Level best_l = 0;
      uint16_t best_id = 0;
      for (auto& r : runs_) {
        int si = indexOf(r.show);
        if (si < 0) continue;
        const Rt& rt = shows_[si];
        int ti = -1;
        for (size_t k = 0; k < rt.show.tracks.size(); k++)
          if (rt.show.tracks[k].target == tc) { ti = (int)k; break; }
        if (ti < 0 || !rt.compiled[ti]) continue;
        Level l = 0;
        if (rt.compiled[ti]->levelAt(now > r.start ? now - r.start : 0, l)) {
          bool better = !have || rt.show.priority > best_pri || (rt.show.priority == best_pri && r.seq > best_seq);
          if (better) { have = true; best_pri = rt.show.priority; best_seq = r.seq; best_l = l; best_id = rt.show.id; }
        }
      }
      req[ci] = have ? best_l : 0;
      if (have) { drivers[ci].kind = Driver::Kind::ByShow; drivers[ci].show = best_id; }
      if (has_script_[ci]) {
        req[ci] = script_[ci];
        drivers[ci].kind = Driver::Kind::Script;
      }
      if (has_manual_[ci]) {
        req[ci] = manual_[ci];
        drivers[ci].kind = Driver::Kind::Manual;
      }
    }
  }
  for (int k = 0; k < CHANNEL_COUNT; k++) { drivers_[k] = drivers[k]; last_req_[k] = req[k]; }

  // interlocks: the output that was on first keeps it
  for (auto& pair : cfg_.interlocks) {
    int a = index(pair.first), b = index(pair.second);
    if (req[a] > 0 && req[b] > 0) {
      int loser = (shapers_[b].level() > 0 && shapers_[a].level() == 0) ? a : b;
      req[loser] = 0;
      if (!blocked_[loser]) {
        blocked_[loser] = true;
        Notice n;
        n.kind = Notice::Kind::Interlock;
        n.channel = ALL_CHANNELS[loser];
        notices_.push_back(n);
      }
    } else {
      blocked_[a] = false;
      blocked_[b] = false;
    }
  }

  Frame f;
  // shaping
  for (int k = 0; k < CHANNEL_COUNT; k++) {
    bool was = shapers_[k].tripped();
    f.levels[k] = shapers_[k].update(now, req[k]);
    if (shapers_[k].tripped() && !was) {
      Notice n;
      n.kind = Notice::Kind::Watchdog;
      n.channel = ALL_CHANNELS[k];
      notices_.push_back(n);
    }
  }

  // pixels
  if (paused_) {
    f.has_pixel = true;
    f.pixel = Effect();
    f.pixel_start = now;
  } else if (has_manual_pixel_) {
    // the animation runs from when the test was set (a start of `now` every tick would hold it at its first frame)
    if (manual_pixel_fresh_) { manual_pixel_start_ = now; manual_pixel_fresh_ = false; }
    f.has_pixel = true;
    f.pixel = manual_pixel_;
    f.pixel_start = manual_pixel_start_;
  } else {
    pixelLayer(now, f);
  }
  f.sound.swap(out_sound_);
  f.cues_out.swap(out_cues_);
  f.notices.swap(notices_);
  return f;
}

Block Engine::blocked(int i) const {
  if (paused_) return Block::Paused;
  if (shapers_[i].tripped()) return Block::Watchdog;
  if (blocked_[i] && last_req_[i] > 0) return Block::Interlock;
  return Block::None;
}

bool Engine::ramping(Channel c) const {
  const Shaper& s = shapers_[index(c)];
  return s.level() > 0 && s.level() < last_req_[index(c)];
}

void Engine::pixelLayer(Millis now, Frame& f) const {
  bool have = false;
  uint8_t best_pri = 0;
  uint64_t best_seq = 0;
  for (auto& r : runs_) {
    int si = indexOf(r.show);
    if (si < 0) continue;
    const Rt& rt = shows_[si];
    const Track* tr = rt.show.track(Target::pixel());
    if (!tr) continue;
    Millis elapsed = now > r.start ? now - r.start : 0;
    size_t k = 0;
    while (k < tr->cues.size() && tr->cues[k].at <= elapsed) k++;
    if (k == 0) continue;
    const Cue& cue = tr->cues[k - 1];
    Effect e;
    if (cue.action.effect(e)) {
      bool better = !have || rt.show.priority > best_pri || (rt.show.priority == best_pri && r.seq > best_seq);
      if (better) {
        have = true;
        best_pri = rt.show.priority;
        best_seq = r.seq;
        f.has_pixel = true;
        f.pixel = e;
        f.pixel_start = r.start + cue.at;
      }
    }
  }
}

}  // namespace pt
