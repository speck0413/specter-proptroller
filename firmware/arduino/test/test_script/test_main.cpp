// Show scripts (firmware/SHOW-SCRIPT.md): the checker (SS-W, SS-L, SS-C) and the runner (SS-R), on their own.
// Every message is checked word for word by test_script_golden (firmware/show-script-golden/).
#include <unity.h>
#include "proptroller/script_record.h"
#include <string.h>
#include <vector>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include "proptroller/script.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static ScriptBoard board() {
  ScriptBoard b;
  b.buttons_are_triggers = true;
  return b;
}

static ScriptCheck check(const char* text, ScriptProgram& p, const ScriptBoard& b = board(), const SoundList& s = {}) {
  return scriptCompile(text, s, b, p);
}


static bool sameTable(const ScriptProgram& a, const ScriptProgram& b) {
  if (a.effects != b.effects || a.steps != b.steps) return false;
  for (int e = 0; e < a.effects; e++)
    if (strcmp(a.fx[e].name, b.fx[e].name) || a.fx[e].count != b.fx[e].count) return false;
  for (int i = 0; i < a.steps; i++) {
    const ScriptStep &x = a.step[i], &y = b.step[i];
    if (x.op != y.op || x.target != y.target || x.value != y.value || x.ms != y.ms) return false;
  }
  return true;
}

// ------------------------------------------------------------------ the checker

void first_look_example_checks() {
  ScriptProgram p;
  ScriptCheck c = check(
      "effect porch light:\n  wait for trig1\n  out1 = on\n  audio_single = 3\n  sleep 30\n  fade out1 to 0% over 2\n\n"
      "effect prop:\n  wait for trig2\n  rly1 = on        # presses the prop's button\n  sleep 3\n  rly1 = off\n  sleep 60\n",
      p);
  TEST_ASSERT_TRUE(c.ok());
  TEST_ASSERT_EQUAL(0, c.warnings.size());
  TEST_ASSERT_EQUAL(2, p.effects);
  TEST_ASSERT_EQUAL_STRING("porch light", p.fx[0].name);
  TEST_ASSERT_EQUAL_STRING("prop", p.fx[1].name);
  TEST_ASSERT_EQUAL(5, p.fx[0].count);
  TEST_ASSERT_EQUAL(5, p.fx[1].count);
  TEST_ASSERT_EQUAL(30000, p.step[3].ms);
  TEST_ASSERT_EQUAL(ScriptStep::Op::Fade, p.step[4].op);
  TEST_ASSERT_EQUAL(2000, p.step[4].ms);
}

void case_spacing_and_phone_typing_give_the_same_table() {
  ScriptProgram a, b, c, d;
  TEST_ASSERT_TRUE(check("effect Door:\nwait for trig1\nout1 = on\nsleep 0.4\nout1 = off\n", a).ok());
  TEST_ASSERT_TRUE(check("EFFECT   door:\r\n\tWAIT  FOR  TRIG1\r\n   OUT1:ON\r\nSleep 400ms\r\nout1=off.\r\n", b).ok());
  TEST_ASSERT_TRUE(check("effect door:\n wait for trig1\n out1 =on\n sleep 0.4 s\n out1 = off\n", c).ok());
  // smart quotes in a comment, a non-breaking space, a trailing period
  TEST_ASSERT_TRUE(check("effect door:\nwait for trig1 # \xE2\x80\x9C" "door\xE2\x80\x9D\nout1\xC2\xA0= on\nsleep 0.4.\nout1 = off\n", d).ok());
  TEST_ASSERT_TRUE(sameTable(a, b));
  TEST_ASSERT_TRUE(sameTable(a, c));
  TEST_ASSERT_TRUE(sameTable(a, d));
}

void effect_names() {
  ScriptProgram p;
  TEST_ASSERT_TRUE(check("Effect1:\nwait for trig1\nout1 = on\nout1 = off\neffect  Porch   Light :\nwait for trig2\nout2 = 50%\nout2 = off\n", p).ok());
  TEST_ASSERT_EQUAL_STRING("effect1", p.fx[0].name);
  TEST_ASSERT_EQUAL_STRING("porch light", p.fx[1].name);
}

void times_and_levels() {
  ScriptProgram p;
  TEST_ASSERT_TRUE(check("effect t:\nwait for trig1\nsleep 200 ms\nsleep 3 min\nsleep 1.25\nsleep 2 seconds\nsleep 0.0005\nout1 = 50 %\nout1 = 12.34%\nout1 = off\n", p).ok());
  TEST_ASSERT_EQUAL(200, p.step[1].ms);
  TEST_ASSERT_EQUAL(180000, p.step[2].ms);
  TEST_ASSERT_EQUAL(1250, p.step[3].ms);
  TEST_ASSERT_EQUAL(2000, p.step[4].ms);
  TEST_ASSERT_EQUAL(1, p.step[5].ms);  // rounded to the nearest ms
  TEST_ASSERT_EQUAL(500, p.step[6].value);
  TEST_ASSERT_EQUAL(123, p.step[7].value);
}



void every_error_is_reported_in_line_order() {
  ScriptProgram p;
  ScriptCheck c = check("effect a:\nwait for trig1\nout9 = on\nsleep never\nrly1 = 20%\n", p);
  TEST_ASSERT_EQUAL(3, c.errors.size());
  TEST_ASSERT_EQUAL(3, c.errors[0].line);
  TEST_ASSERT_EQUAL(4, c.errors[1].line);
  TEST_ASSERT_EQUAL(5, c.errors[2].line);
  TEST_ASSERT_EQUAL(0, p.effects);  // a refused script leaves no table
}



void sounds_are_numbers_and_volume_is_a_percentage() {
  SoundList s = {{"Chime.mp3", 3}};  // a stored sound list is kept, never read
  ScriptProgram p;
  TEST_ASSERT_TRUE(check("effect a:\nwait for trig1\naudio_single = 99\naudio_loop = \"7\"\naudio_single = 12\naudio_stop\n", p, board(), s).ok());
  TEST_ASSERT_EQUAL(99, p.step[1].value);
  TEST_ASSERT_EQUAL(7, p.step[2].value);
  TEST_ASSERT_EQUAL(12, p.step[3].value);
  ScriptCheck c = check("effect a:\nwait for trig1\naudio_single = Chime.mp3\naudio_single = 100\naudio_loop = 7_wind\n", p, board(), s);
  TEST_ASSERT_EQUAL(3, c.errors.size());
  TEST_ASSERT_EQUAL_STRING("line 3: \"Chime.mp3\" is not a sound number. Sounds are numbered 1 to 99: Studio's SD card tool puts them on the card for you (sound 5 is 05/001-<name>.mp3).", c.errors[0].text.c_str());
  TEST_ASSERT_EQUAL_STRING("line 4: sound 100 is out of range. Sounds are numbered 1 to 99: Studio's SD card tool puts them on the card for you (sound 5 is 05/001-<name>.mp3).", c.errors[1].text.c_str());
  // audio_volume: 0-100 % on the player's 0-30, halves up; off and 0 are mute; a sound line (may come first)
  TEST_ASSERT_TRUE(check("effect a:\naudio_volume = 60%\nwait for trig1\naudio_volume = 50%\naudio_volume = 1.7%\naudio_volume = off\naudio_volume = 0\naudio_volume = 100 %\n", p).ok());
  uint16_t want[] = {18, 15, 1, 0, 0, 30};
  for (int k = 0; k < 6; k++) {
    const ScriptStep& st = p.step[k == 0 ? 0 : k + 1];
    TEST_ASSERT_TRUE(st.op == ScriptStep::Op::Volume);
    TEST_ASSERT_EQUAL(want[k], st.value);
  }
  c = check("effect a:\nwait for trig1\naudio_volume = loud\naudio_volume = 120%\naudio_volume =\n", p);
  TEST_ASSERT_EQUAL(3, c.errors.size());
  TEST_ASSERT_EQUAL_STRING("line 3: \"loud\" is not a volume. Use a percentage from 0% to 100%, or off.", c.errors[0].text.c_str());
  SoundList back;
  std::string why;
  TEST_ASSERT_TRUE(soundListParse(soundListJson(s), back, why));
  TEST_ASSERT_EQUAL(1, back.size());
}


// ------------------------------------------------------------------ the runner

namespace {
// 1 ms samples, a tick every 10 ms (as the board).
struct Bench {
  ScriptProgram prog;
  ScriptRunner run;
  ScriptInputs in;
  Millis t = 0;
  std::vector<SoundCommand> sound;
  std::vector<std::pair<Millis, std::pair<int, Level>>> changes;  // (time, (channel, level))
  Level last[CHANNEL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
  explicit Bench(const char* text, const ScriptBoard& b = board()) {
    ScriptCheck c = scriptCompile(text, {}, b, prog);
    TEST_ASSERT_TRUE_MESSAGE(c.ok(), c.errors.empty() ? "" : c.errors[0].text.c_str());
    run.setTimes(100, 100, SCRIPT_BOOT_LOCKOUT_MS);
    run.load(&prog, 0);
  }
  void set(int k, bool closed) {
    if (in.closed[k] != closed) { in.closed[k] = closed; in.since[k] = t; }
  }
  void step(Millis ms) {
    for (Millis i = 0; i < ms; i++) {
      t++;
      run.sample(t, in);
      if (t % 10 == 0) {
        run.tick(t, in, sound);
        for (int c = 0; c < CHANNEL_COUNT; c++)
          if (run.level(c) != last[c]) { last[c] = run.level(c); changes.push_back({t, {c, last[c]}}); }
      }
    }
  }
  void press(int k, Millis ms) { set(k, true); step(ms); set(k, false); }
  int ons(int c) const {
    int n = 0;
    for (auto& x : changes) n += x.second.first == c && x.second.second > 0 && x.second.second == FULL;
    return n;
  }
};
}  // namespace

void a_run_and_its_timing() {
  Bench b("effect a:\nwait for trig1\nout1 = on\nsleep 2\nout1 = off\nsleep 1\n");
  b.step(2500);
  b.press(0, 150);
  b.step(4000);
  TEST_ASSERT_EQUAL(2, b.changes.size());
  TEST_ASSERT_EQUAL(2600, b.changes[0].first);  // closed at 2500, 100 ms debounce, the next tick
  TEST_ASSERT_EQUAL(4600, b.changes[1].first);
  TEST_ASSERT_EQUAL(1u, b.run.runs(0));
}

void nothing_passes_in_the_boot_lockout_and_a_held_input_waits_for_release() {
  Bench b("effect a:\nwait for trig1\nout1 = on\nsleep 1\nout1 = off\n");
  b.set(0, true);  // held from power-on
  b.step(5000);
  TEST_ASSERT_EQUAL(0, b.changes.size());
  b.set(0, false);
  b.step(300);
  b.press(0, 150);
  b.step(2000);
  TEST_ASSERT_EQUAL(1, b.ons(0));
  Bench early("effect a:\nwait for trig1\nout1 = on\nsleep 1\nout1 = off\n");
  early.step(500);
  early.press(0, 300);  // closed and opened inside the 2 s lockout
  early.step(3000);
  TEST_ASSERT_EQUAL(0, early.changes.size());
}

void held_through_the_end_never_restarts() {
  Bench b("effect a:\nwait for trig1\nout1 = on\nsleep 1\nout1 = off\n");
  b.step(2500);
  b.press(0, 5000);  // held for 5 s: one run
  b.step(2000);
  TEST_ASSERT_EQUAL(1, b.ons(0));
  b.press(0, 150);  // released, pressed again: a second run
  b.step(2000);
  TEST_ASSERT_EQUAL(2, b.ons(0));
}

void press_release_press_during_a_run_is_one_run() {
  Bench b("effect a:\nwait for trig1\nout1 = on\nsleep 2\nout1 = off\n");
  b.step(2500);
  b.press(0, 150);
  b.step(300);
  b.press(0, 150);  // during the run
  b.step(300);
  b.press(0, 150);
  b.step(3000);
  TEST_ASSERT_EQUAL(1, b.ons(0));
  TEST_ASSERT_EQUAL(1u, b.run.runs(0));
}

void a_short_tap_never_counts() {
  Bench b("effect a:\nwait for trig1\nout1 = on\nsleep 1\nout1 = off\n");
  b.step(2500);
  b.press(0, 60);
  b.step(2000);
  TEST_ASSERT_EQUAL(0, b.changes.size());
}

void two_effects_at_once_and_one_input_starts_both() {
  Bench b("effect a:\nwait for trig1\nout1 = on\nsleep 3\nout1 = off\neffect b:\nwait for trig1\nrly1 = on\nsleep 1\nrly1 = off\n"
          "effect c:\nwait for trig2\nout2 = on\nsleep 1\nout2 = off\n");
  b.step(2500);
  b.press(0, 150);
  b.step(500);
  b.press(1, 150);
  b.step(4000);
  TEST_ASSERT_EQUAL(1, b.ons(0));
  TEST_ASSERT_EQUAL(1, b.ons(4));
  TEST_ASSERT_EQUAL(1, b.ons(1));
  TEST_ASSERT_EQUAL(b.changes[0].first, b.changes[1].first);  // a and b start on the same tick
}

void a_wait_in_the_middle() {
  Bench b("effect a:\nwait for trig1\nout1 = on\nwait for trig1\nout1 = off\n");
  b.step(2500);
  b.press(0, 1000);  // starts; still held at the second wait: it must be released first
  b.step(500);
  TEST_ASSERT_EQUAL(FULL, b.run.level(0));
  b.press(0, 150);
  b.step(200);
  TEST_ASSERT_EQUAL(0, b.run.level(0));
}

void fade_and_flicker_take_their_time() {
  Bench b("effect a:\nwait for trig1\nout1 = 100%\nfade out1 to 0% over 1\nout2 = on\nflicker out1 for 0.5\nout2 = off\n");
  b.step(2500);
  b.press(0, 150);
  b.step(2000);
  Level mid = 0;
  for (auto& x : b.changes)
    if (x.second.first == 0 && x.first == 3100) mid = x.second.second;
  TEST_ASSERT_EQUAL_MESSAGE(500, mid, "halfway through the 1 s fade from 100 %");
  // out2 came on when the fade had finished, and off when the flicker had
  Millis on2 = 0, off2 = 0;
  for (auto& x : b.changes)
    if (x.second.first == 1) (x.second.second ? on2 : off2) = x.first;
  TEST_ASSERT_EQUAL(3600, on2);
  TEST_ASSERT_EQUAL(4100, off2);
  TEST_ASSERT_EQUAL(0, b.run.level(0));  // flicker gives back the level before it (0 after the fade)
}

void sound_lines_and_dmx() {
  ScriptBoard dmx = board();
  dmx.dmx_on = true;
  Bench b("effect a:\nwait for trig1\naudio_loop = 4\nsleep 1\naudio_single = 2\ndmx3 = 50%\nsleep 1\naudio_stop\ndmx3 = off\n", dmx);
  b.step(2500);
  b.press(0, 150);
  b.step(500);
  TEST_ASSERT_EQUAL(1, b.sound.size());
  TEST_ASSERT_TRUE(b.sound[0] == SoundCommand::loopFolder(4));  // the player loops folder 04 (sound.h)
  uint8_t c;
  uint16_t prm;
  b.sound[0].encode(c, prm);
  TEST_ASSERT_EQUAL_HEX8(0x17, c);
  TEST_ASSERT_EQUAL(4, prm);
  b.step(1000);
  b.sound[1].encode(c, prm);
  TEST_ASSERT_EQUAL_HEX8(0x0F, c);  // sound 2: folder 02, track 1
  TEST_ASSERT_EQUAL_HEX16(0x0201, prm);
  TEST_ASSERT_EQUAL(128, b.run.dmx()[2]);  // round(50 x 255 / 100), halves up
  b.step(1500);
  b.sound[2].encode(c, prm);
  TEST_ASSERT_EQUAL_HEX8(0x16, c);
  TEST_ASSERT_EQUAL(0, b.run.dmx()[2]);
  // audio_loop keeps playing after the effect is back at its wait for: the runner sends nothing more
  TEST_ASSERT_EQUAL(3, b.sound.size());
}

void random_presses_never_break_the_rules() {
  // SS-T3 (a small version; the golden set has the shared one): a run only starts from the wait for, and
  // never while the effect runs; outputs belong to one effect.
  uint32_t seed = 12345;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
  for (int round = 0; round < 50; round++) {
    Bench b("effect a:\nwait for trig1\nout1 = on\nsleep 0.7\nout1 = off\nsleep 0.3\neffect b:\nwait for trig2\nout2 = on\nwait for trig1\nout2 = off\n");
    b.step(2100);
    uint32_t prev_runs = 0;
    for (int k = 0; k < 60; k++) {
      int input = rnd() % 2;
      b.set(input, !b.in.closed[input]);
      Millis gap = 20 + rnd() % 400;
      for (Millis i = 0; i < gap; i += 10) {
        bool was = b.run.running(0);
        b.step(10);
        if (b.run.runs(0) != prev_runs) {
          TEST_ASSERT_FALSE_MESSAGE(was && b.run.line(0) != 2, "a new run while one was going");
          prev_runs = b.run.runs(0);
        }
      }
    }
  }
}

// The public example scripts (firmware/show-script-examples/, workspace-template/effects/): each passes the
// checker with the board settings its header names (`# Board setting: dmx_port = s4`), and a press on its
// first effect's input changes an output, a DMX channel or plays a sound.
void every_example_script_checks_and_runs() {
  std::vector<std::string> names;
  for (const std::string dir : {"../show-script-examples/", "workspace-template/effects/"}) {
    DIR* d = opendir(dir.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(d, dir.c_str());
    while (dirent* e = readdir(d)) {
      std::string n = e->d_name;
      if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0) names.push_back(dir + n);
    }
    closedir(d);
  }
  for (const std::string& n : names) {
    std::ifstream f(n, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    ScriptBoard b;  // rly1, rly2 shows, cc1 trigger repeat, cc2 sound trigger; user1-3 learn buttons
    b.dmx_on = text.find("dmx_port = s4") != std::string::npos;
    ScriptProgram* p = new ScriptProgram();
    ScriptCheck c = scriptCompile(text, {}, b, *p);
    TEST_ASSERT_TRUE_MESSAGE(c.ok(), (n + ": " + (c.ok() ? std::string() : c.errors[0].text)).c_str());
    // the first wait for (sound and pixels lines may come before it)
    uint16_t w = p->fx[0].first;
    while (w < p->fx[0].first + p->fx[0].count && p->step[w].op != ScriptStep::Op::Wait) w++;
    const ScriptStep& first = p->step[w];
    TEST_ASSERT_EQUAL_MESSAGE(ScriptStep::Op::Wait, first.op, (n + ": first wait for").c_str());
    ScriptRunner run;
    run.load(p, 0);
    ScriptInputs in;
    bool changed = false;
    std::vector<SoundCommand> snd;
    for (Millis now = 1; now < 20000; now++) {
      bool closed = now >= 3000 && now < 3300;
      if (closed != in.closed[first.target]) { in.closed[first.target] = closed; in.since[first.target] = now; }
      run.sample(now, in);
      if (now % 10 == 0) {
        run.tick(now, in, snd);
        for (int ch = 0; ch < CHANNEL_COUNT; ch++) changed |= run.level(ch) > 0;
        for (int k = 0; k < DMX_CHANNELS; k++) changed |= run.dmx()[k] > 0;
        changed |= !snd.empty();
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(changed, (n + ": a press does nothing").c_str());
    delete p;
  }
  TEST_ASSERT_EQUAL_MESSAGE(10, (int)names.size(), "the eight public examples and the workspace's two");
}

// Every limit at once: 8 effects, 224 lines, just under SCRIPT_MAX_BYTES (blank lines would only add text).
static std::string largest() {
  std::string t;
  for (int e = 0; e < SCRIPT_MAX_EFFECTS; e++) {
    t += "effect effect number " + std::to_string(e) + " with a long name:\n  wait for trig1\n";
    for (int i = 0; i < 26; i++) t += "  fade dmx" + std::to_string(e + 1) + " to 37.5% over 1.25 s # c\n";
  }
  return t;
}

void the_largest_script_fits_the_table() {
  std::string t = largest();
  ScriptBoard b;
  b.dmx_on = true;
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  ScriptCheck c = scriptCompile(t, {}, b, *p);
  TEST_ASSERT_TRUE(c.ok());
  TEST_ASSERT_TRUE(t.size() > SCRIPT_MAX_BYTES - 512 && t.size() <= SCRIPT_MAX_BYTES);
  TEST_ASSERT_EQUAL(SCRIPT_MAX_EFFECTS, p->effects);
  TEST_ASSERT_TRUE(p->steps > 200);
}


// A text over SCRIPT_MAX_BYTES is answered with the size message alone and nothing of it is read (a 20 KB text
// restarted an ESP32 on 2026-10-23: out of memory in the line copies)
void a_text_over_the_byte_limit_gets_the_size_message_alone() {
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  std::string many = "effect a:\n  wait for trig9\n";
  while (many.size() <= 22000) many += "  out9 = on\n  sleep x\n";
  for (const std::string& t : {std::string(22000, '#'), std::string(22000, 'x'), many, std::string(SCRIPT_MAX_BYTES + 1, '#')}) {
    ScriptCheck c = scriptCompile(t, {}, board(), *p);
    TEST_ASSERT_FALSE(c.ok());
    TEST_ASSERT_EQUAL(1, (int)c.errors.size());
    TEST_ASSERT_EQUAL(0, c.errors[0].line);
    TEST_ASSERT_EQUAL_STRING(("the script is " + std::to_string(t.size()) + " bytes. A board takes at most 8192.").c_str(), c.errors[0].text.c_str());
    TEST_ASSERT_EQUAL(0, (int)c.warnings.size());
    TEST_ASSERT_EQUAL(0, p->effects);
  }
}


// ------------------------------------------------------------------ names given to terminals (SS-N)

static ScriptBoard named() {
  ScriptBoard b = board();
  b.roles[0] = 0;          // rly1 shows
  b.names.push_back({0, "Lamp"});     // OUT1 is connected to "Lamp" (setting out1_name)
  b.names.push_back({4, "FOG"});      // the profile's terminal alias for RLY1
  b.names.push_back({8, "door mat"}); // TRIG1: two words, a label only
  b.names.push_back({9, "BELL"});     // TRIG2
  return b;
}

static std::string err0(const ScriptCheck& c) { return c.errors.empty() ? std::string() : c.errors[0].text; }

void a_name_works_wherever_a_terminal_does() {
  ScriptProgram a, b;
  const char* by_name = "effect porch:\nwait for bell\nLAMP = 50%\nfog = on\nsleep 1\nFog = off\nfade lamp to 0% over 2\nflicker lamp for 1\n";
  const char* by_id = "effect porch:\nwait for trig2\nout1 = 50%\nrly1 = on\nsleep 1\nrly1 = off\nfade out1 to 0% over 2\nflicker out1 for 1\n";
  TEST_ASSERT_TRUE(check(by_name, a, named()).ok());
  TEST_ASSERT_TRUE(check(by_id, b, named()).ok());
  TEST_ASSERT_TRUE_MESSAGE(sameTable(a, b), "a name gives the same table as the terminal's own name");
  // the own names still work, and a board without names is unchanged
  ScriptCheck c = check("effect a:\nwait for trig1\nlamp = on\n", a);
  TEST_ASSERT_EQUAL_STRING("line 3: there is no lamp on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.", err0(c).c_str());
}

void the_messages_show_the_terminal_beside_the_name() {
  ScriptProgram p;
  ScriptBoard b = named();
  ScriptCheck c = check("effect a:\nwait for bell\nfog = 50%\n", p, b);
  TEST_ASSERT_EQUAL_STRING("line 3: fog (rly1) can only be on or off, not 50%. Dimming works on out1 to out4.", err0(c).c_str());
  c = check("effect a:\nwait for bell\nfade fog to 0% over 1\n", p, b);
  TEST_ASSERT_EQUAL_STRING("line 3: fog (rly1) cannot fade. Fading works on out1 to out4.", err0(c).c_str());
  b.roles[0] = 1;
  c = check("effect a:\nwait for bell\nfog = on\n", p, b);
  TEST_ASSERT_EQUAL_STRING("line 3: fog (rly1) is set to pulse on every trigger (rly1_role). Set rly1_role to shows to use it in a script.",
                           err0(c).c_str());
  c = check("effect a:\nwait for bell\nlamp = on\neffect b:\nwait for trig1\nout1 = off\n", p, named());
  TEST_ASSERT_TRUE_MESSAGE(c.ok(), "two effects may set the same output (SS-R8: they take turns)");
  c = check("effect a:\nwait for bell\nlamp = on\n", p, named());
  TEST_ASSERT_TRUE(c.ok());
  TEST_ASSERT_EQUAL_STRING("line 1: effect \"a\" ends with lamp (out1) still on. It may stay on until the next trigger.", c.warnings[0].text.c_str());
  c = check("effect a:\nwait for bell\nlamp on\n", p, named());
  TEST_ASSERT_EQUAL_STRING("line 3: write it as \"lamp = on\".", err0(c).c_str());
  b = named();
  b.buttons_are_triggers = false;
  b.names.push_back({10, "STEP"});
  c = check("effect a:\nwait for step\nlamp = on\n", p, b);
  TEST_ASSERT_EQUAL_STRING("line 2: step (user1) is a learn-mode button (buttons = learn). Set buttons to triggers to use it in a script.",
                           err0(c).c_str());
}

void names_that_cannot_be_used_are_said_plainly() {
  ScriptProgram p;
  ScriptBoard b = named();
  b.names.push_back({1, "fog"});  // OUT2 has the name RLY1's alias has
  ScriptCheck c = check("effect a:\nwait for bell\nfog = on\n", p, b);
  TEST_ASSERT_EQUAL_STRING("line 3: \"fog\" is the name of both out2 and rly1. Write out2 or rly1, or give them different names.", err0(c).c_str());
  c = check("effect a:\nwait for door mat\nlamp = on\n", p, named());
  TEST_ASSERT_EQUAL_STRING("line 2: \"door mat\" is the name of trig1, but a script cannot use it: a name in a script is one word. Write trig1, or give it another name.",
                           err0(c).c_str());
  b = named();
  b.names.push_back({2, "sleep"});
  c = check("effect a:\nwait for bell\nsleep = on\nsleep 1\n", p, b);
  TEST_ASSERT_EQUAL_STRING("line 3: \"sleep\" is the name of out3, but a script cannot use it: it is a word of the script language. Write out3, or give it another name.",
                           err0(c).c_str());
  TEST_ASSERT_EQUAL(1, (int)c.errors.size());  // "sleep 1" is still a sleep
  b.names.push_back({3, "Trig1"});  // OUT4 "is connected to" trig1: the terminal's own name wins
  c = check("effect a:\nwait for trig1\nout4 = on\nsleep 1\nout4 = off\neffect b:\nwait for trig1\nout2 = on\nsleep 1\nout2 = off\n", p, b);
  TEST_ASSERT_TRUE(c.ok());
  TEST_ASSERT_EQUAL(1, (int)c.warnings.size());  // said once
  TEST_ASSERT_EQUAL_STRING("line 2: trig1 is also the name given to out4. Here trig1 means the terminal trig1.", c.warnings[0].text.c_str());
  c = check("effect a:\nwait for bell\ntrig1 = on\n", p, b);
  TEST_ASSERT_EQUAL_STRING("line 3: \"trig1\" is the name of out4, but a script cannot use it: it is the name of a terminal. Write out4, or give it another name.",
                           err0(c).c_str());
  // an input's name is not an output's, and the other way round
  c = check("effect a:\nwait for lamp\n", p, named());
  TEST_ASSERT_EQUAL_STRING("line 2: there is no lamp on this board. The inputs are trig1, trig2 and user1 to user3.", err0(c).c_str());
}

void names_change_the_stored_table_key() {
  ScriptBoard a = board(), b = board();
  TEST_ASSERT_EQUAL_UINT32(scriptTableKey(a, "{}", "fw"), scriptTableKey(b, "{}", "fw"));
  b.names.push_back({0, "lamp"});
  TEST_ASSERT_NOT_EQUAL(scriptTableKey(a, "{}", "fw"), scriptTableKey(b, "{}", "fw"));
  ScriptBoard c = board();
  c.names.push_back({4, "lamp"});  // the same name on another terminal
  TEST_ASSERT_NOT_EQUAL(scriptTableKey(b, "{}", "fw"), scriptTableKey(c, "{}", "fw"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(first_look_example_checks);
  RUN_TEST(case_spacing_and_phone_typing_give_the_same_table);
  RUN_TEST(effect_names);
  RUN_TEST(times_and_levels);
  RUN_TEST(every_error_is_reported_in_line_order);
  RUN_TEST(sounds_are_numbers_and_volume_is_a_percentage);
  RUN_TEST(a_run_and_its_timing);
  RUN_TEST(nothing_passes_in_the_boot_lockout_and_a_held_input_waits_for_release);
  RUN_TEST(held_through_the_end_never_restarts);
  RUN_TEST(press_release_press_during_a_run_is_one_run);
  RUN_TEST(a_short_tap_never_counts);
  RUN_TEST(two_effects_at_once_and_one_input_starts_both);
  RUN_TEST(a_wait_in_the_middle);
  RUN_TEST(fade_and_flicker_take_their_time);
  RUN_TEST(sound_lines_and_dmx);
  RUN_TEST(random_presses_never_break_the_rules);
  RUN_TEST(every_example_script_checks_and_runs);
  RUN_TEST(the_largest_script_fits_the_table);
  RUN_TEST(a_text_over_the_byte_limit_gets_the_size_message_alone);
  RUN_TEST(a_name_works_wherever_a_terminal_does);
  RUN_TEST(the_messages_show_the_terminal_beside_the_name);
  RUN_TEST(names_that_cannot_be_used_are_said_plainly);
  RUN_TEST(names_change_the_stored_table_key);
  return UNITY_END();
}
