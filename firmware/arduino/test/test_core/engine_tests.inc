// Engine tests: the same cases as proptroller-core/src/engine_tests.rs.

static const char* EDOOR = R"({
  "id": 1, "name": "Front door",
  "triggers": [{"on": "input", "input": "TRIG1"}],
  "cooldown_ms": 5000,
  "tracks": [
    {"target": "OUT1",  "cues": [{"at": 0, "do": "on"}, {"at": 3000, "do": "off"}]},
    {"target": "SOUND", "cues": [{"at": 0, "do": "play", "folder": 1, "track": 7}]},
    {"target": "CC1",   "cues": [{"at": 0, "do": "pulse", "ms": 200}]},
    {"target": "PIXEL", "cues": [{"at": 1000, "do": "fill", "color": "#102030"}]}
  ],
  "length_ms": 4000
})";

typedef std::vector<std::pair<Millis, Frame>> Frames;

static void engineWith(Engine& e, std::initializer_list<const char*> shows) {
  for (auto s : shows) TEST_ASSERT_TRUE(e.setShow(parse(s)).ok());
}
static Frames runE(Engine& e, Millis from, Millis to) {
  Frames f;
  for (Millis t = from; t < to; t += 10) f.push_back({t, e.tick(t)});
  return f;
}
static const Frame& at(const Frames& f, Millis t) {
  for (auto& x : f)
    if (x.first == t) return x.second;
  TEST_FAIL_MESSAGE("no frame at that time");
  return f[0].second;
}
static Level levelAt(const Frames& f, Millis t, Channel c) { return at(f, t).levels[index(c)]; }
static std::vector<Notice> notices(const Frames& f) {
  std::vector<Notice> n;
  for (auto& x : f) n.insert(n.end(), x.second.notices.begin(), x.second.notices.end());
  return n;
}
static bool hasNotice(const std::vector<Notice>& v, const Notice& n) {
  for (auto& x : v)
    if (x == n) return true;
  return false;
}
static Cause cause(Cause::By by, Input i = Input::Trig1) { Cause c; c.by = by; c.input = i; return c; }
static bool allZero(const Frame& f) {
  for (auto l : f.levels)
    if (l) return false;
  return true;
}
static const SoundCommand STOP = SoundCommand::simple(SoundCommand::Kind::Stop);

void trigger_runs_timeline_then_cooldown() {
  Engine e;
  engineWith(e, {EDOOR});
  Frames f = runE(e, 0, 1000);
  for (auto& x : f) TEST_ASSERT_TRUE(allZero(x.second));
  (void)e.pressed(1000, Input::Trig1);
  f = runE(e, 1000, 6000);
  TEST_ASSERT_TRUE(cmdsEq(f[0].second.sound, {SoundCommand::playFolder(1, 7)}));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1000, Channel::Out1));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1190, Channel::Cc1));
  TEST_ASSERT_EQUAL(0, levelAt(f, 1200, Channel::Cc1));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 3990, Channel::Out1));
  TEST_ASSERT_EQUAL(0, levelAt(f, 4000, Channel::Out1));
  const Frame& p = at(f, 2500);
  TEST_ASSERT_TRUE(p.has_pixel && p.pixel.kind == Effect::Kind::Fill && p.pixel.color == Rgb(0x10, 0x20, 0x30) && p.pixel_start == 2000);
  auto n = notices(f);
  TEST_ASSERT_TRUE(n[0] == Notice::started(1, cause(Cause::By::ByInput)));
  TEST_ASSERT_TRUE(n[1] == Notice::ended(1, EndReason::Finished));
  TEST_ASSERT_TRUE(hasNotice(at(f, 5000).notices, Notice::ended(1, EndReason::Finished)));
  ShowStatus st;
  TEST_ASSERT_TRUE(e.status(1, 6000, st) && st.kind == ShowStatus::Kind::Cooldown && st.remaining_ms == 4000);
  (void)e.pressed(6000, Input::Trig1);
  f = runE(e, 6000, 6100);
  n = notices(f);
  TEST_ASSERT_TRUE(n.size() == 1 && n[0] == Notice::ignored(1, Ignored::Cooldown));
  TEST_ASSERT_EQUAL(0, levelAt(f, 6050, Channel::Out1));
  runE(e, 6100, 10000);
  f = runE(e, 10000, 10100);
  TEST_ASSERT_EQUAL(0, levelAt(f, 10050, Channel::Out1));
  TEST_ASSERT_TRUE(e.status(1, 10100, st) && st.kind == ShowStatus::Kind::Idle);
  (void)e.pressed(10100, Input::Trig1);
  f = runE(e, 10100, 10200);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 10150, Channel::Out1));
}

void press_while_running_is_ignored_and_retrigger_policies() {
  Engine e;
  engineWith(e, {EDOOR});
  (void)e.pressed(0, Input::Trig1);
  runE(e, 0, 500);
  (void)e.pressed(500, Input::Trig1);
  Frames f = runE(e, 500, 600);
  auto n = notices(f);
  TEST_ASSERT_TRUE(n.size() == 1 && n[0] == Notice::ignored(1, Ignored::Running));
  ShowStatus st;
  TEST_ASSERT_TRUE(e.status(1, 600, st) && st.kind == ShowStatus::Kind::Running && st.elapsed_ms == 600 && st.length_ms == 4000);

  Show s = parse(EDOOR);
  s.retrigger = Retrigger::Restart;
  s.cooldown_ms = 0;
  Engine e2;
  TEST_ASSERT_TRUE(e2.setShow(s).ok());
  (void)e2.pressed(0, Input::Trig1);
  runE(e2, 0, 3500);
  (void)e2.pressed(3500, Input::Trig1);
  f = runE(e2, 3500, 6000);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 5000, Channel::Out1));
  TEST_ASSERT_TRUE(hasNotice(notices(f), Notice::ended(1, EndReason::Restarted)));
  TEST_ASSERT_TRUE(cmdsEq(f[0].second.sound, {STOP, SoundCommand::playFolder(1, 7)}));

  s.retrigger = Retrigger::Stop;
  TEST_ASSERT_TRUE(e2.setShow(s).ok());
  (void)e2.pressed(7000, Input::Trig1);
  runE(e2, 7000, 7100);
  (void)e2.pressed(7100, Input::Trig1);
  f = runE(e2, 7100, 7200);
  TEST_ASSERT_EQUAL(0, levelAt(f, 7150, Channel::Out1));
}

void priority_arbitration_and_untouched_tracks() {
  const char* ambient = R"({"id": 9, "name": "Ambient", "priority": 0, "looping": true, "triggers": [{"on": "boot"}],
      "tracks": [{"target": "OUT1", "cues": [{"at": 0, "do": "level", "level": 300}]},
                 {"target": "OUT2", "cues": [{"at": 0, "do": "on"}]}], "length_ms": 1000})";
  const char* scene = R"({"id": 1, "name": "Scene", "triggers": [{"on": "input", "input": "TRIG1"}],
      "tracks": [{"target": "OUT1", "cues": [{"at": 0, "do": "on"}, {"at": 1000, "do": "off"}]},
                 {"target": "OUT2", "cues": [{"at": 500, "do": "off"}]}], "length_ms": 2000})";
  Engine e;
  engineWith(e, {ambient, scene});
  Frames f = runE(e, 0, 1000);
  TEST_ASSERT_EQUAL(300, levelAt(f, 500, Channel::Out1));
  (void)e.pressed(1000, Input::Trig1);
  f = runE(e, 1000, 4000);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1100, Channel::Out1));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1100, Channel::Out2));
  TEST_ASSERT_EQUAL(0, levelAt(f, 1600, Channel::Out2));
  TEST_ASSERT_EQUAL(0, levelAt(f, 2500, Channel::Out1));
  TEST_ASSERT_EQUAL(300, levelAt(f, 3100, Channel::Out1));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 3100, Channel::Out2));
  TEST_ASSERT_TRUE(e.isRunning(9));
}

void soft_start_and_watchdog() {
  EngineConfig cfg;
  cfg.outputs[1].soft_start_ms = 300;
  cfg.outputs[4].soft_start_ms = 300;
  cfg.outputs[0].max_on_ms = 2000;
  Engine e(cfg);
  TEST_ASSERT_TRUE(e.setShow(parse(R"({"id": 2, "name": "Fan", "tracks": [
        {"target": "OUT2", "cues": [{"at": 0, "do": "on"}, {"at": 3000, "do": "off"}]},
        {"target": "RLY1", "cues": [{"at": 0, "do": "on"}, {"at": 3000, "do": "off"}]},
        {"target": "OUT1", "cues": [{"at": 0, "do": "on"}, {"at": 3000, "do": "off"}]}]})")).ok());
  TEST_ASSERT_TRUE(e.run(0, 2) == Ignored::None);
  Frames f = runE(e, 0, 3500);
  TEST_ASSERT_EQUAL(500, levelAt(f, 150, Channel::Out2));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 300, Channel::Out2));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 0, Channel::Rly1));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1990, Channel::Out1));
  TEST_ASSERT_EQUAL(0, levelAt(f, 2000, Channel::Out1));
  Notice w;
  w.kind = Notice::Kind::Watchdog;
  w.channel = Channel::Out1;
  TEST_ASSERT_TRUE(hasNotice(notices(f), w));
}

void interlock_keeps_the_first_output() {
  EngineConfig cfg;
  cfg.interlocks.push_back({Channel::Rly1, Channel::Rly2});
  Engine e(cfg);
  TEST_ASSERT_TRUE(e.setShow(parse(R"({"id": 3, "name": "Motor", "tracks": [
        {"target": "RLY1", "cues": [{"at": 0, "do": "on"}, {"at": 1000, "do": "off"}]},
        {"target": "RLY2", "cues": [{"at": 500, "do": "on"}, {"at": 2000, "do": "off"}]}]})")).ok());
  (void)e.run(0, 3);
  Frames f = runE(e, 0, 2500);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 700, Channel::Rly1));
  TEST_ASSERT_EQUAL(0, levelAt(f, 700, Channel::Rly2));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1500, Channel::Rly2));
  int k = 0;
  for (auto& n : notices(f)) k += n.kind == Notice::Kind::Interlock;
  TEST_ASSERT_EQUAL(1, k);
}

void pause_input_stops_everything() {
  EngineConfig cfg;
  cfg.has_pause_input = true;
  cfg.pause_input = Input::Trig2;
  Engine e(cfg);
  TEST_ASSERT_TRUE(e.setShow(parse(EDOOR)).ok());
  (void)e.pressed(0, Input::Trig1);
  runE(e, 0, 500);
  (void)e.pressed(500, Input::Trig2);
  e.setManual(Channel::Out4, true, FULL);
  Frames f = runE(e, 500, 1000);
  for (auto& x : f) TEST_ASSERT_TRUE(allZero(x.second));
  TEST_ASSERT_TRUE(cmdsEq(f[0].second.sound, {STOP, STOP}));
  TEST_ASSERT_TRUE(hasNotice(notices(f), Notice::ended(1, EndReason::Paused)));
  TEST_ASSERT_TRUE(e.run(1000, 1) == Ignored::Paused);
  (void)e.released(1000, Input::Trig2);
  TEST_ASSERT_FALSE(e.isPaused());
  f = runE(e, 1000, 1100);
  // releasing the pause turns nothing back on, also what was set during it (PROTOCOL.md)
  TEST_ASSERT_EQUAL(0, levelAt(f, 1050, Channel::Out4));
  e.setManual(Channel::Out4, true, FULL);
  f = runE(e, 1100, 1200);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1150, Channel::Out4));
}

static std::vector<std::pair<uint16_t, Cause>> starts(const Frames& f) {
  std::vector<std::pair<uint16_t, Cause>> v;
  for (auto& n : notices(f))
    if (n.kind == Notice::Kind::Started) v.push_back({n.show, n.cause});
  return v;
}

void timers_boot_after_and_cues() {
  const char* a = R"({"id": 1, "name": "A", "triggers": [{"on": "boot", "delay_ms": 1000}],
       "tracks": [{"target": "NET", "cues": [{"at": 100, "do": "emit", "cue": "go"}]}], "length_ms": 200})";
  const char* b = R"({"id": 2, "name": "B", "triggers": [{"on": "cue", "name": "go"}],
       "tracks": [{"target": "OUT1", "cues": [{"at": 0, "do": "pulse", "ms": 100}]}]})";
  const char* c = R"({"id": 3, "name": "C", "triggers": [{"on": "after", "show": 1}],
       "tracks": [{"target": "OUT2", "cues": [{"at": 0, "do": "pulse", "ms": 100}]}]})";
  const char* d = R"({"id": 4, "name": "D", "triggers": [{"on": "every", "period_ms": 500}],
       "tracks": [{"target": "OUT3", "cues": [{"at": 0, "do": "pulse", "ms": 50}]}]})";
  Engine e;
  engineWith(e, {a, b, c, d});
  Frames f = runE(e, 0, 2000);
  auto s = starts(f);
  TEST_ASSERT_TRUE(s[0].first == 4 && s[0].second.by == Cause::By::ByTimer);
  bool boot = false, go = false, after = false;
  int timers = 0;
  for (auto& x : s) {
    boot |= x.first == 1 && x.second.by == Cause::By::ByBoot;
    go |= x.first == 2 && x.second.by == Cause::By::ByCue && x.second.name == "go";
    after |= x.first == 3 && x.second.by == Cause::By::ByAfter && x.second.show == 1;
    timers += x.first == 4;
  }
  TEST_ASSERT_TRUE(boot && go && after);
  TEST_ASSERT_EQUAL(3, timers);
  std::vector<std::string> out;
  for (auto& x : f) out.insert(out.end(), x.second.cues_out.begin(), x.second.cues_out.end());
  TEST_ASSERT_TRUE(out.size() == 1 && out[0] == "go");
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1110, Channel::Out1));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1210, Channel::Out2));
  e.cue(3000, "go");
  TEST_ASSERT_TRUE(e.isRunning(2));
}

void looping_until_stopped_and_stop_sound() {
  const char* s = R"({"id": 5, "name": "Loop", "looping": true,
       "tracks": [{"target": "OUT1", "cues": [{"at": 0, "do": "on"}, {"at": 500, "do": "off"}]},
                  {"target": "SOUND", "cues": [{"at": 0, "do": "play", "folder": 2, "track": 1}]}], "length_ms": 1000})";
  Engine e;
  engineWith(e, {s});
  (void)e.run(0, 5);
  Frames f = runE(e, 0, 3000);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 2100, Channel::Out1));
  TEST_ASSERT_EQUAL(0, levelAt(f, 2600, Channel::Out1));
  int k = 0;
  for (auto& x : f) k += !x.second.sound.empty();
  TEST_ASSERT_EQUAL(3, k);
  e.stop(3000, 5);
  f = runE(e, 3000, 3100);
  TEST_ASSERT_TRUE(cmdsEq(f[0].second.sound, {STOP}));
  TEST_ASSERT_FALSE(e.isRunning(5));
}

void manual_and_stop_all() {
  Engine e;
  engineWith(e, {EDOOR});
  e.setManual(Channel::Rly2, true, FULL);
  (void)e.run(0, 1);
  Frames f = runE(e, 0, 100);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 50, Channel::Rly2));
  e.stopAll(100);
  f = runE(e, 100, 200);
  TEST_ASSERT_TRUE(allZero(f[0].second));
  TEST_ASSERT_TRUE(cmdsEq(f[0].second.sound, {STOP}));
}

void editing_and_limits() {
  Engine e;
  engineWith(e, {EDOOR});
  (void)e.run(0, 1);
  runE(e, 0, 100);
  Show s = parse(EDOOR);
  s.name = "Renamed";
  TEST_ASSERT_TRUE(e.setShow(s).ok());
  TEST_ASSERT_FALSE(e.isRunning(1));
  Frames f = runE(e, 100, 200);
  TEST_ASSERT_TRUE(notices(f).empty() || allZero(f[0].second));
  TEST_ASSERT_EQUAL_STRING("Renamed", e.show(1)->name.c_str());
  Show bad = parse(EDOOR);
  bad.tracks[0].cues[0].action = act(Action::Kind::Play);
  bad.tracks[0].cues[0].action.folder = 1;
  bad.tracks[0].cues[0].action.track = 1;
  TEST_ASSERT_FALSE(e.setShow(bad).ok());
  for (uint16_t id = 2; id <= 16; id++) {
    Show x;
    x.id = id;
    x.name = "x";
    Track t;
    t.target = Target::of(Channel::Out1);
    x.tracks.push_back(t);
    TEST_ASSERT_TRUE(e.setShow(x).ok());
  }
  Show many;
  many.id = 17;
  many.name = "too many";
  TEST_ASSERT_FALSE(e.setShow(many).ok());
  TEST_ASSERT_TRUE(e.deleteShow(16));
  TEST_ASSERT_FALSE(e.deleteShow(16));
  TEST_ASSERT_TRUE(e.run(0, 99) == Ignored::Unknown);
  Show d = parse(EDOOR);
  d.id = 20;
  d.enabled = false;
  TEST_ASSERT_TRUE(e.setShow(d).ok());
  TEST_ASSERT_TRUE(e.run(0, 20) == Ignored::Disabled);
  ShowStatus st;
  TEST_ASSERT_TRUE(e.status(20, 0, st) && st.kind == ShowStatus::Kind::Disabled);
}

void ten_ms_resolution() {
  Engine e;
  engineWith(e, {R"({"id": 1, "name": "Fine", "tracks": [{"target": "OUT1", "cues": [{"at": 10, "do": "on"}, {"at": 20, "do": "off"}, {"at": 30, "do": "on"}]}], "length_ms": 40})"});
  (void)e.run(1000, 1);
  Frames f = runE(e, 1000, 1050);
  const Level want[] = {0, FULL, 0, FULL, 0};
  for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL(want[i], f[i].second.levels[0]);
}

void pixels_follow_priority_and_manual() {
  Engine e;
  engineWith(e, {EDOOR});
  TEST_ASSERT_FALSE(e.tick(0).has_pixel);
  Effect r;
  r.kind = Effect::Kind::Rainbow;
  r.period_ms = 1000;
  e.setManualPixel(&r);
  Frame f = e.tick(10);
  TEST_ASSERT_TRUE(f.has_pixel && f.pixel == r && f.pixel_start == 10);
  e.setManualPixel(nullptr);
  (void)e.run(20, 1);
  TEST_ASSERT_FALSE(e.tick(500).has_pixel);
  f = e.tick(1100);
  TEST_ASSERT_TRUE(f.has_pixel && f.pixel.kind == Effect::Kind::Fill && f.pixel_start == 1020);
}

void press_outcome_drivers_and_blocks() {
  EngineConfig cfg;
  cfg.interlocks.push_back({Channel::Rly1, Channel::Rly2});
  cfg.outputs[1].soft_start_ms = 300;
  Engine e(cfg);
  std::vector<uint16_t> st;
  TEST_ASSERT_TRUE(e.press(0, Input::Trig2, st) == Ignored::None && st.empty());
  TEST_ASSERT_TRUE(e.setShow(parse(EDOOR)).ok());
  TEST_ASSERT_TRUE(e.setShow(parse(R"({"id": 2, "name": "Both", "triggers": [{"on":"input","input":"TRIG2"}], "tracks": [
        {"target": "RLY1", "cues": [{"at": 0, "do": "on"}]}, {"target": "RLY2", "cues": [{"at": 0, "do": "on"}]},
        {"target": "OUT2", "cues": [{"at": 0, "do": "on"}]}], "length_ms": 1000})")).ok());
  TEST_ASSERT_TRUE(e.press(0, Input::Trig1, st) == Ignored::None && st.size() == 1 && st[0] == 1);
  TEST_ASSERT_TRUE(e.press(10, Input::Trig1, st) == Ignored::Running);
  TEST_ASSERT_TRUE(e.press(10, Input::Trig2, st) == Ignored::None && st.size() == 1 && st[0] == 2);
  e.setManual(Channel::Cc2, true, FULL);
  e.tick(20);
  e.tick(100);
  const Driver* d = e.drivers();
  TEST_ASSERT_TRUE(d[0].kind == Driver::Kind::ByShow && d[0].show == 1);
  TEST_ASSERT_TRUE(d[4].kind == Driver::Kind::ByShow && d[4].show == 2);
  TEST_ASSERT_TRUE(d[7].kind == Driver::Kind::Manual);
  TEST_ASSERT_TRUE(d[3].kind == Driver::Kind::Idle);
  TEST_ASSERT_TRUE(e.blocked(5) == Block::Interlock);
  TEST_ASSERT_TRUE(e.ramping(Channel::Out2));
  TEST_ASSERT_EQUAL(FULL, e.requested()[1]);
  e.tick(500);
  TEST_ASSERT_FALSE(e.ramping(Channel::Out2));
}

static const char* THREE_PER_3MIN = R"({
  "id": 2, "name": "Three per window",
  "triggers": [{"on": "input", "input": "TRIG1"}],
  "cooldown_ms": 180000, "runs_per_cooldown": 3,
  "tracks": [{"target": "OUT1", "cues": [{"at": 0, "do": "on"}, {"at": 1000, "do": "off"}]}]
})";

static bool pressStarts(Engine& e, Millis t) {
  (void)e.pressed(t, Input::Trig1);
  Frames f = runE(e, t, t + 20);
  for (auto& n : notices(f))
    if (n.kind == Notice::Kind::Started) return true;
  return false;
}

void runs_per_cooldown_three_then_wait() {
  Engine e;
  engineWith(e, {THREE_PER_3MIN});
  runE(e, 0, 100);
  TEST_ASSERT_TRUE(pressStarts(e, 100));
  runE(e, 120, 1200);
  TEST_ASSERT_TRUE(pressStarts(e, 2000));
  runE(e, 2020, 3100);
  TEST_ASSERT_TRUE(pressStarts(e, 4000));
  runE(e, 4020, 5100);
  TEST_ASSERT_FALSE(pressStarts(e, 6000));
  ShowStatus st;
  TEST_ASSERT_TRUE(e.status(2, 6020, st) && st.kind == ShowStatus::Kind::Cooldown && st.remaining_ms == 181100 - 6020);
  TEST_ASSERT_FALSE(pressStarts(e, 181000));
  TEST_ASSERT_TRUE(pressStarts(e, 181100));
}

void runs_per_cooldown_change_applies_to_the_next_run() {
  Engine e;
  engineWith(e, {THREE_PER_3MIN});
  TEST_ASSERT_TRUE(pressStarts(e, 100));
  runE(e, 120, 1200);
  Show s = parse(THREE_PER_3MIN);
  s.runs_per_cooldown = 1;
  TEST_ASSERT_TRUE(e.setShow(s).ok());
  TEST_ASSERT_FALSE(pressStarts(e, 2000));
  s.runs_per_cooldown = 21;
  TEST_ASSERT_FALSE(e.setShow(s).ok());
}


// ---------------------------------------------------------------- on while held, add to a track

static const char* HELD = R"({
  "id": 5, "name": "Link: TRIG1 -> OUT1",
  "triggers": [{"on": "input", "input": "TRIG1", "until_release": true}],
  "tracks": [{"target": "OUT1", "cues": [{"at": 0, "do": "on"}]}, {"target": "RLY1", "cues": [{"at": 0, "do": "on"}]}],
  "length_ms": 2000
})";

void until_release_follows_the_input() {
  Engine e;
  engineWith(e, {HELD});
  Show s = *e.show(5);
  TEST_ASSERT_TRUE(s.triggers[0].until_release);
  TEST_ASSERT_EQUAL(2000, s.length());
  // press: on; release: off at once, with the reason "released"
  (void)e.pressed(1000, Input::Trig1);
  Frames f = runE(e, 1000, 1500);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1490, Channel::Out1));
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 1490, Channel::Rly1));
  (void)e.released(1500, Input::Trig1);
  f = runE(e, 1500, 1600);
  TEST_ASSERT_TRUE(allZero(at(f, 1500)));
  TEST_ASSERT_FALSE(e.isRunning(5));
  TEST_ASSERT_TRUE(hasNotice(notices(f), Notice::ended(5, EndReason::Released)));
  TEST_ASSERT_EQUAL_STRING("released", endReasonName(EndReason::Released));
  // held past its end (the cap): off at 2000 ms, and the late release turns nothing on
  (void)e.pressed(3000, Input::Trig1);
  f = runE(e, 3000, 5500);
  TEST_ASSERT_EQUAL(FULL, levelAt(f, 4990, Channel::Out1));
  TEST_ASSERT_TRUE(allZero(at(f, 5000)));
  TEST_ASSERT_TRUE(hasNotice(notices(f), Notice::ended(5, EndReason::Finished)));
  (void)e.released(5500, Input::Trig1);
  f = runE(e, 5500, 6000);
  for (auto& x : f) TEST_ASSERT_TRUE(allZero(x.second));
  // another input's release does not end it; a run started by the API or a cue is not ended by a release
  (void)e.pressed(6000, Input::Trig1);
  (void)e.released(6100, Input::Trig2);
  TEST_ASSERT_TRUE(e.isRunning(5));
  (void)e.released(6200, Input::Trig1);
  TEST_ASSERT_FALSE(e.isRunning(5));
  TEST_ASSERT_TRUE(e.run(7000, 5) == Ignored::None);
  (void)e.released(7100, Input::Trig1);
  TEST_ASSERT_TRUE(e.isRunning(5));
  f = runE(e, 7100, 9100);
  TEST_ASSERT_FALSE(e.isRunning(5));
  // pause ends it and nothing comes back on at the release
  (void)e.pressed(10000, Input::Trig1);
  e.pause(10100, true);
  TEST_ASSERT_FALSE(e.isRunning(5));
  e.pause(10200, false);
  (void)e.released(10300, Input::Trig1);
  f = runE(e, 10300, 10500);
  for (auto& x : f) TEST_ASSERT_TRUE(allZero(x.second));
}

void until_release_json_and_default_cap() {
  Show s = parse(HELD);
  TEST_ASSERT_EQUAL_STRING(R"({"on":"input","input":"TRIG1","until_release":true})", [&] {
    JsonDocument d; toJson(s.triggers[0], d.to<JsonObject>()); static std::string o; o.clear(); serializeJson(d, o); return o.c_str(); }());
  // left out when false
  ShowTrigger t;
  JsonDocument d; toJson(t, d.to<JsonObject>()); std::string o; serializeJson(d, o);
  TEST_ASSERT_EQUAL_STRING(R"({"on":"input","input":"TRIG1"})", o.c_str());
  // no length_ms: at most UNTIL_RELEASE_MAX_MS held
  s.has_length = false;
  TEST_ASSERT_EQUAL(UNTIL_RELEASE_MAX_MS, s.length());
  // only on an input trigger
  Show bad;
  std::string err;
  TEST_ASSERT_FALSE(showFromString(R"({"id":2,"name":"x","triggers":[{"on":"release","input":"TRIG1","until_release":true}]})", bad, err));
  TEST_ASSERT_FALSE(showFromString(R"({"id":2,"name":"x","triggers":[{"on":"input","input":"TRIG1","until_release":1}]})", bad, err));
}

static void run_engine_tests() {
  RUN_TEST(until_release_follows_the_input);
  RUN_TEST(until_release_json_and_default_cap);
  RUN_TEST(trigger_runs_timeline_then_cooldown);
  RUN_TEST(press_while_running_is_ignored_and_retrigger_policies);
  RUN_TEST(priority_arbitration_and_untouched_tracks);
  RUN_TEST(soft_start_and_watchdog);
  RUN_TEST(interlock_keeps_the_first_output);
  RUN_TEST(pause_input_stops_everything);
  RUN_TEST(timers_boot_after_and_cues);
  RUN_TEST(looping_until_stopped_and_stop_sound);
  RUN_TEST(manual_and_stop_all);
  RUN_TEST(editing_and_limits);
  RUN_TEST(ten_ms_resolution);
  RUN_TEST(pixels_follow_priority_and_manual);
  RUN_TEST(press_outcome_drivers_and_blocks);
  RUN_TEST(runs_per_cooldown_three_then_wait);
  RUN_TEST(runs_per_cooldown_change_applies_to_the_next_run);
}
