// Proptroller, the advanced layer: everything that is not "wait for a trigger, then turn outputs on and play a
// sound". The beginner calls (Proptroller.h) stay as they are; this file adds prop.advanced():
//
//   #include <ProptrollerAdvanced.h>
//   Proptroller prop;
//
//   void setup() {
//     prop.begin();
//     prop.advanced().when(TRIG1).turnOn(OUT1, seconds(2)).cooldown(seconds(30)).allowRunsPerCooldown(3);
//   }
//
// What is here (GOING-FURTHER.md has the details):
//   - sequences: rests between runs (cooldown, allowRunsPerCooldown), when they start
//     (alsoWhen, alsoOnCue, every, atPowerOn, onCue), how they
//     end (lastsFor, repeatForever, restart/stopWhenTriggeredAgain, priority), dimming, fades, blinking, pixels,
//     more sound steps, cues to other boards;
//   - the built-in pulse-and-recover effect (PulseAndRecover, withPulseAndRecover) and a firmware of your own
//     (begin(FirmwareMaker));
//   - profiles and names (loadProfile, nameInput, nameOutput, input, when("NAME")), any board setting (setting),
//     trigger timing (triggerHoldTime, allowTriggerHeldAtPowerOn);
//   - direct control from loop(), your own state machines (PropTask, addTask), an output hook, MQTT,
//     memoryReport.
//
// The safe defaults of the beginner layer stay on: outputs off first, 0.1 s press and release, no retrigger while
// running, nothing from a trigger held at power-on; a maximum on-time per output if you set one (off by default).
#pragma once
#include "Proptroller.h"

// ------------------------------------------------------------------ a sequence with every step and rule

// What prop.advanced().when(...) gives you: the beginner steps plus all the others.
class AdvancedSequence : public Sequence {
 public:
  explicit AdvancedSequence(Sequence&& s) : Sequence(std::move(s)) {}
  AdvancedSequence(AdvancedSequence&& o) noexcept : Sequence(std::move(o)) {}

  // the beginner steps
  AdvancedSequence& turnOn(PropOutput o) { Sequence::turnOn(o); return *this; }
  AdvancedSequence& turnOn(PropOutput o, Duration how_long) { Sequence::turnOn(o, how_long); return *this; }
  AdvancedSequence& turnOff(PropOutput o) { Sequence::turnOff(o); return *this; }
  AdvancedSequence& playSound(int number) { Sequence::playSound(number); return *this; }
  AdvancedSequence& then() { Sequence::then(); return *this; }
  AdvancedSequence& wait(Duration d) { Sequence::wait(d); return *this; }
  // a bare number where a time belongs: a compile error that says what to write instead
  template <typename T, ptIfNumber<T> = 0>
  AdvancedSequence& turnOn(PropOutput, T) {
    static_assert(ptAlwaysFalse<T>::value, PT_NEEDS_A_UNIT("turnOn(OUT1, 2)"));
    return *this;
  }
  template <typename T, ptIfNumber<T> = 0>
  AdvancedSequence& wait(T) {
    static_assert(ptAlwaysFalse<T>::value, PT_NEEDS_A_UNIT("wait(2)"));
    return *this;
  }
  template <typename T, ptIfNumber<T> = 0>
  AdvancedSequence& cooldown(T) {
    static_assert(ptAlwaysFalse<T>::value, PT_NEEDS_A_UNIT("cooldown(30)"));
    return *this;
  }

  // Dim an output (OUT1-4 only) to a level, or fade it there over a time.
  AdvancedSequence& dim(PropOutput o, Percent level) { Sequence::dim(o, level); return *this; }
  AdvancedSequence& fadeTo(PropOutput o, Percent level, Duration how_long) { Sequence::fadeTo(o, level, how_long); return *this; }
  AdvancedSequence& fadeIn(PropOutput o, Duration how_long) { Sequence::fadeIn(o, how_long); return *this; }
  AdvancedSequence& fadeOut(PropOutput o, Duration how_long) { Sequence::fadeOut(o, how_long); return *this; }
  // Blink an output: on, off, so many times.
  AdvancedSequence& blink(PropOutput o, int times, Duration on_for, Duration off_for) { Sequence::blink(o, times, on_for, off_for); return *this; }

  // Sound: a file in a numbered folder instead of a sound number (playSound(5, 1) plays /05/001.mp3), a whole
  // folder in a loop, the volume.
  AdvancedSequence& playSound(int folder, int number) { Sequence::playSound(folder, number); return *this; }
  AdvancedSequence& loopSoundsInFolder(int folder) { Sequence::loopSoundsInFolder(folder); return *this; }
  AdvancedSequence& setVolume(int volume_0_to_30) { Sequence::setVolume(volume_0_to_30); return *this; }
  AdvancedSequence& stopSound() { Sequence::stopSound(); return *this; }

  // A strip of addressable pixels.
  AdvancedSequence& setPixels(Color c) { Sequence::setPixels(c); return *this; }
  AdvancedSequence& flickerPixels(Color c) { Sequence::flickerPixels(c); return *this; }
  AdvancedSequence& flashPixels(Color c, Duration period) { Sequence::flashPixels(c, period); return *this; }
  AdvancedSequence& chasePixels(Color c, Duration step) { Sequence::chasePixels(c, step); return *this; }
  AdvancedSequence& rainbowPixels(Duration period) { Sequence::rainbowPixels(period); return *this; }
  AdvancedSequence& fadeInPixels(Color c, Duration how_long) { Sequence::fadeInPixels(c, how_long); return *this; }
  AdvancedSequence& pixelsOff() { Sequence::pixelsOff(); return *this; }

  // The show being built (namespace pt), and store it now instead of at the end of the line.
  pt::Show& show() { return Sequence::show(); }
  bool commit() { return Sequence::commit(); }

  // Tell other boards on the network: sendCue("door") starts their onCue("door") sequences.
  AdvancedSequence& sendCue(const char* name) { Sequence::sendCue(name); return *this; }

  // The rules: a name for the phone and the log; a rest between runs (and how many runs before it); a fixed
  // length; loop; what a trigger does while the sequence runs (the default: nothing); which sequence wins an output.
  AdvancedSequence& named(const char* name) { Sequence::named(name); return *this; }
  AdvancedSequence& cooldown(Duration rest) { Sequence::cooldown(rest); return *this; }
  AdvancedSequence& allowRunsPerCooldown(int runs) { Sequence::allowRunsPerCooldown(runs); return *this; }
  AdvancedSequence& lastsFor(Duration d) { Sequence::lastsFor(d); return *this; }
  AdvancedSequence& repeatForever() { Sequence::repeatForever(); return *this; }
  AdvancedSequence& restartWhenTriggeredAgain() { Sequence::restartWhenTriggeredAgain(); return *this; }
  AdvancedSequence& stopWhenTriggeredAgain() { Sequence::stopWhenTriggeredAgain(); return *this; }
  AdvancedSequence& priority(int level) { Sequence::priority(level); return *this; }
  // More ways to start the same sequence.
  AdvancedSequence& alsoWhen(PropInput i) { Sequence::alsoWhen(i); return *this; }
  AdvancedSequence& alsoOnCue(const char* name) { Sequence::alsoOnCue(name); return *this; }
};

// ------------------------------------------------------------------ the built-in pulse-and-recover effect

// TRIG1 starts a run: OUT1 pulses, then OUT2 recovers; TRIG2 recovers only (firmware/STATE-MACHINE.md). Set it up
// with the calls below and start it with prop.advanced().withPulseAndRecover(...).
class PulseAndRecover {
 public:
  PulseAndRecover();  // a 1 s pulse, a 5.5 s recovery, 3 runs per 3 minutes, TRIG1 held 0.1 s to start
  PulseAndRecover& pulseFor(Duration time);       // how long OUT1 is on per run
  PulseAndRecover& recoverFor(Duration time);     // how long OUT2 runs after each pulse
  PulseAndRecover& runsBeforeRest(int runs);      // how many runs may start before the rest (1-20)
  PulseAndRecover& restFor(Duration time);        // the rest, counted from the first run (zero: no rest)
  PulseAndRecover& afterPulseFor(Duration time);  // OUT3 on this long after each pulse; ready again while OUT2 recovers
  PulseAndRecover& readyLampOnOut4();             // a lamp on OUT4, steady when the effect is ready
  PulseAndRecover& safetyOnAtPowerOn();           // hold TRIG1 for 2 s to release the safety
  PulseAndRecover& holdToStart(Duration time);    // how long TRIG1 must be held to start a run (default 0.1 s)
  // What TRIG2 does: the recovery without the pulse (the default), OUT2 for a time, or OUT2 while TRIG2 is held.
  PulseAndRecover& recoverRunsACycle();
  PulseAndRecover& recoverRunsFor(Duration time);
  PulseAndRecover& recoverWhileHeld();
  PulseAndRecover& triggerOutPulse(Duration time);  // CC1 / CC2 (trigger out / sound out) pulse per run (default 0.2 s)
  PulseAndRecover& releaseTime(Duration time);      // how long TRIG1 must be open to count as released (default 0.1 s)
  const std::vector<pt::ParamValue>& values() const { return values_; }

 private:
  PulseAndRecover& set(const char* key, const pt::Value& v);
  std::vector<pt::ParamValue> values_;
};

// ------------------------------------------------------------------ the advanced controller

// prop.advanced(): the same controller with all of its functions.
class PropAdvanced {
 public:
  explicit PropAdvanced(Proptroller& p) : p_(p) {}
  // the beginner calls
  void begin() { p_.begin(); }
  void update() { p_.update(); }
  AdvancedSequence when(PropInput i) { return AdvancedSequence(p_.when(i)); }

  // Run a firmware of your own instead of the generic controller (when(...) and the direct-control functions
  // then do nothing; tasks, the network, MQTT topics and the serial port work as usual).
  void begin(FirmwareMaker make) { p_.begin(make); }
  // Use the board's built-in pulse-and-recover effect (TRIG1, TRIG2, OUT1, OUT2 are then the effect's).
  void withPulseAndRecover(const PulseAndRecover& effect) { p_.withPulseAndRecover(effect); }

  // Give a terminal your project's own name: nameInput(TRIG2, "REFILL", "Refill button"). The app, the wiring
  // view and the log then show "REFILL (TRIG2)", and input("REFILL") / when("REFILL") find it.
  void nameInput(PropInput i, const char* name, const char* label = "") { p_.nameInput(i, name, label); }
  void nameOutput(PropOutput o, const char* name, const char* label = "") { p_.nameOutput(o, name, label); }
  // Any board setting by its key (the phone's Settings page lists them): setting("name", "Front door").
  void setting(const char* key, long value) { p_.setting(key, value); }
  void setting(const char* key, int value) { p_.setting(key, (long)value); }
  void setting(const char* key, bool value) { p_.setting(key, value); }
  void setting(const char* key, const char* value) { p_.setting(key, value); }
  // A trigger by its name, or by a project's alias for it from the profile ("REFILL" for TRIG2).
  AdvancedSequence when(const char* name) { return AdvancedSequence(p_.when(p_.input(name))); }
  // The input called `name` (a board name or a terminal alias); TRIG1 and a plain-word message when there is none.
  PropInput input(const char* name) { return p_.input(name); }
  // Load a profile (a project's own names, firmware/STATE-MACHINE.md §11): the JSON of an alias table. False and
  // a plain-word message when it is refused. Kept on the board; loading changes no value.
  bool loadProfile(const char* json) { return p_.loadProfile(json); }

  // More ways to start a sequence: every so often, once after power-on, on a cue from another board.
  AdvancedSequence every(Duration period) { return AdvancedSequence(p_.every(period)); }
  AdvancedSequence atPowerOn(Duration delay = Duration{0}) { return AdvancedSequence(p_.atPowerOn(delay)); }
  AdvancedSequence onCue(const char* name) { return AdvancedSequence(p_.onCue(name)); }

  // Direct control from loop() or a task.
  void turnOn(PropOutput o) { p_.turnOn(o); }
  void turnOff(PropOutput o) { p_.turnOff(o); }
  void dim(PropOutput o, Percent level) { p_.dim(o, level); }
  void release(PropOutput o) { p_.release(o); }  // give the output back to the shows
  bool isPressed(PropInput i) { return p_.isPressed(i); }
  bool wasPressed(PropInput i) { return p_.wasPressed(i); }  // once per new press (never one held at power-on)
  void playSound(int number) { p_.playSound(number); }
  void playSound(int folder, int number) { p_.playSound(folder, number); }
  void stopSound() { p_.stopSound(); }
  void setVolume(int volume_0_to_30) { p_.setVolume(volume_0_to_30); }
  void sendCue(const char* name) { p_.sendCue(name); }
  void stopEverything() { p_.stopEverything(); }
  // How long a trigger must be held to count, and released to count as released (default 0.1 s).
  void triggerHoldTime(Duration time) { p_.triggerHoldTime(time); }
  uint64_t timeMs() { return p_.timeMs(); }

  // Going further.
  pt::Controller& controller() { return p_.controller(); }
  pt::Engine& engine() { return p_.engine(); }
  pt::Board& board() { return p_.board(); }
  void addTask(PropTask& t) { p_.addTask(t); }
  void afterOutputChange(OutputChangeHook h) { p_.afterOutputChange(h); }
  void onRequest(pt::Controller::RequestHook h, void* user = nullptr) { p_.onRequest(h, user); }
  bool publish(const char* topic, const char* payload, bool retain = false) { return p_.publish(topic, payload, retain); }
  void onMessage(const char* topic, MessageHook h) { p_.onMessage(topic, h); }
  // Opt out of the held-at-power-on rule for one trigger (it may then start a show right after power-up).
  void allowTriggerHeldAtPowerOn(PropInput i) { p_.allowTriggerHeldAtPowerOn(i); }
  // Free heap, stack high-water marks and the boot time, in one line (also the serial command "mem").
  std::string memoryReport() { return p_.memoryReport(); }
  // Print a message in the board's log (Serial Monitor and the phone's event log).
  void say(const char* message) { p_.say(message); }
#if !defined(ARDUINO)
  pt::Sim& hostSim() { return p_.hostSim(); }
  struct pt::SimWorld& hostWorld() { return p_.hostWorld(); }
#endif

 private:
  Proptroller& p_;
};

// The running Proptroller's memoryReport() (for a firmware with its own serial console).
std::string proptrollerMemoryReport();

#if !defined(ARDUINO)
#include <mutex>
// Host builds: held by the background thread while it steps a controller that started without prop.begin();
// a test holds it to touch the simulated board.
std::recursive_mutex& proptrollerHostMutex();
#endif

// ------------------------------------------------------------------ your own code that runs every 10 ms

// A custom state machine: derive from PropTask, write update(), register it with prop.advanced().addTask(). It
// runs every 10 ms together with the show engine (with the controller locked), so it can read inputs and switch
// outputs with the same functions as loop().
class PropTask {
 public:
  virtual ~PropTask() {}
  virtual void update(PropAdvanced& prop, uint64_t now_ms) = 0;
};
