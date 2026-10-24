// Proptroller: a show controller for the Spectre Proptroller board. The beginner layer: wait for a trigger, then
// turn outputs on for a time or one after another, and play a sound.
//
//   #include <Proptroller.h>
//   Proptroller prop;
//
//   void setup() {
//     prop.begin();
//     prop.when(TRIG1).playSound(1).turnOn(OUT1, seconds(2)).then().turnOn(OUT2, seconds(5));
//   }
//
//   void loop() {
//     prop.update();
//   }
//
// prop.begin() and prop.update() are optional: without them the first call starts the board, which then runs in
// the background (loop() is yours; use both or neither).
//
// That is the whole beginner vocabulary: prop.begin(), prop.update(), prop.when(TRIG1), and on the sequence
// turnOn(OUT1), turnOn(OUT1, seconds(2)), turnOff(OUT1), wait(seconds(1)), then(), playSound(1); time as
// seconds(...), milliseconds(...) or minutes(...).
//
// Safe by default, nothing to type: every output is off before anything else happens; a trigger must be pressed
// for 0.1 s and released before it can start the sequence again; a sequence that is running ignores its trigger;
// a trigger already pressed at power-on never starts anything. A maximum on-time per output is a setting, off
// by default: set one for heaters, pumps and solenoids. The board does not measure load current: keep each load
// within its terminal's rating (hardware/README.md).
//
// Everything else (rests between runs, other triggers, dimming, pixels, the network, your own state machines,
// the built-in pulse-and-recover effect, profiles and names) is the advanced layer: #include <ProptrollerAdvanced.h>
// and prop.advanced()... (see GOING-FURTHER.md).
#pragma once
#include <math.h>
#include <type_traits>
#include "proptroller/device.h"

namespace pt {
class Sim;
class NetApp;
struct SimWorld;
}

// ------------------------------------------------------------------ names for the board's terminals

enum PropInput : uint8_t { TRIG1, TRIG2, USER1, USER2, USER3 };
enum PropOutput : uint8_t { OUT1, OUT2, OUT3, OUT4, RLY1, RLY2, CC1, CC2 };

// ------------------------------------------------------------------ amounts with their units

struct Duration {
  uint32_t ms;
};
// A time is never a bare number in the sketch API (seconds or milliseconds?): only seconds(), milliseconds(),
// minutes() and hours() make one.
static_assert(!std::is_convertible<uint32_t, Duration>::value, "a Duration comes from seconds() / milliseconds(), never from a number");
static_assert(!std::is_convertible<float, Duration>::value, "a Duration comes from seconds() / milliseconds(), never from a number");
inline Duration milliseconds(uint32_t n) { return Duration{n}; }
// false for every T, but only once T is known: a static_assert on it fires only in the overload that is chosen
template <typename T>
struct ptAlwaysFalse : std::false_type {};
#define PT_NEEDS_A_UNIT(call) \
  call ": a time needs its unit: write seconds(2), milliseconds(500) or minutes(1), not a bare number"
// enable_if for a bare number (int, float, ...) where a time belongs
template <typename T>
using ptIfNumber = typename std::enable_if<std::is_arithmetic<T>::value, int>::type;
// rounded to the nearest millisecond; a negative amount is no time
inline uint32_t roundMs(float ms) { return ms <= 0.0f ? 0u : (uint32_t)lroundf(ms); }
inline Duration seconds(float n) { return Duration{roundMs(n * 1000.0f)}; }
inline Duration minutes(float n) { return Duration{roundMs(n * 60000.0f)}; }
inline Duration hours(float n) { return Duration{roundMs(n * 3600000.0f)}; }

struct Percent {
  uint16_t permille;  // 0-1000
};
inline Percent percent(float n) { return Percent{(uint16_t)(n <= 0 ? 0 : n >= 100 ? 1000 : n * 10.0f + 0.5f)}; }

struct Color {
  uint8_t red, green, blue;
};
inline Color rgb(uint8_t r, uint8_t g, uint8_t b) { return Color{r, g, b}; }
static const Color RED = {255, 0, 0};
static const Color GREEN = {0, 255, 0};
static const Color BLUE = {0, 0, 255};
static const Color WHITE = {255, 255, 255};
static const Color WARM_WHITE = {255, 160, 60};
static const Color ORANGE = {255, 96, 0};
static const Color YELLOW = {255, 200, 0};
static const Color PURPLE = {150, 0, 255};
static const Color BLACK = {0, 0, 0};

class Proptroller;
class PropAdvanced;
class AdvancedSequence;

// Library internals (ProptrollerAuto.cpp): the controller started by the first call that needs it, and served in
// the background, for a sketch that never calls prop.begin().
struct ProptrollerAuto {
  static void start(Proptroller& p);
  static bool active(const Proptroller& p);
  static bool skipUpdate(const Proptroller& p);  // a prop.update() the background already does
  static void stop(Proptroller& p);
};

// ------------------------------------------------------------------ a sequence of steps (one show)

// What prop.when(...) gives you. Each step starts at the same moment as the one before it, unless you put
// then() (wait until everything so far has finished) or wait(...) in between. The sequence is checked and
// stored at the end of the line; problems are printed on the Serial Monitor in plain words.
class Sequence {
 public:
  // Switch an output on (until turnOff, or for a time), or off. OUT1-4, RLY1-2, CC1-2.
  Sequence& turnOn(PropOutput o);
  Sequence& turnOn(PropOutput o, Duration how_long);
  Sequence& turnOff(PropOutput o);
  // Play a sound (a DFPlayer Mini with a microSD card) by its number: playSound(3) plays the card's
  // mp3/0003-<name>.mp3, the same sound 3 as a show script's audio_single = 3.
  Sequence& playSound(int number);
  // Time: then() waits until everything so far has finished; wait(seconds(2)) waits that long.
  Sequence& then();
  Sequence& wait(Duration d);
  // turnOn(OUT1, 2) and wait(2): a compile error that says what to write instead (never a guessed unit)
  template <typename T, ptIfNumber<T> = 0>
  Sequence& turnOn(PropOutput, T) {
    static_assert(ptAlwaysFalse<T>::value, PT_NEEDS_A_UNIT("turnOn(OUT1, 2)"));
    return *this;
  }
  template <typename T, ptIfNumber<T> = 0>
  Sequence& wait(T) {
    static_assert(ptAlwaysFalse<T>::value, PT_NEEDS_A_UNIT("wait(2)"));
    return *this;
  }

 protected:
  // The advanced steps and rules (public on AdvancedSequence, ProptrollerAdvanced.h).
  Sequence& dim(PropOutput o, Percent level);
  Sequence& fadeTo(PropOutput o, Percent level, Duration how_long);
  Sequence& fadeIn(PropOutput o, Duration how_long) { return fadeTo(o, percent(100), how_long); }
  Sequence& fadeOut(PropOutput o, Duration how_long) { return fadeTo(o, percent(0), how_long); }
  Sequence& blink(PropOutput o, int times, Duration on_for, Duration off_for);
  Sequence& playSound(int folder, int number);
  Sequence& loopSoundsInFolder(int folder);
  Sequence& setVolume(int volume_0_to_30);
  Sequence& stopSound();
  Sequence& setPixels(Color c);
  Sequence& flickerPixels(Color c);
  Sequence& flashPixels(Color c, Duration period);
  Sequence& chasePixels(Color c, Duration step);
  Sequence& rainbowPixels(Duration period);
  Sequence& fadeInPixels(Color c, Duration how_long);
  Sequence& pixelsOff();
  Sequence& sendCue(const char* name);
  Sequence& named(const char* name);
  Sequence& cooldown(Duration rest);
  Sequence& allowRunsPerCooldown(int runs);
  Sequence& lastsFor(Duration d);
  Sequence& repeatForever();
  Sequence& restartWhenTriggeredAgain();
  Sequence& stopWhenTriggeredAgain();
  Sequence& priority(int level);
  Sequence& alsoWhen(PropInput i);
  Sequence& alsoOnCue(const char* name);

 public:
  Sequence(Sequence&& o) noexcept;
  Sequence(const Sequence&) = delete;
  Sequence& operator=(const Sequence&) = delete;
  ~Sequence();
 protected:
  // The show being built (namespace pt), and store it now instead of at the end of the line (AdvancedSequence).
  pt::Show& show() { return show_; }
  bool commit();

 private:
  friend class Proptroller;
  friend class AdvancedSequence;
  friend class PropAdvanced;
  Sequence(Proptroller* p, uint16_t id, const char* what);
  pt::Track& track(pt::Target t) { return show_.trackMut(t); }
  void cue(pt::Target t, const pt::Action& a, uint32_t length);
  Proptroller* prop_;
  pt::Show show_;
  uint32_t at_ = 0, end_ = 0;
  std::string what_;
  bool done_ = false;
};

// ------------------------------------------------------------------ types of the advanced layer (ProptrollerAdvanced.h)

class PropTask;
class PulseAndRecover;
typedef void (*OutputChangeHook)(PropOutput output, int level_permille);
typedef pt::Firmware* (*FirmwareMaker)(pt::BoardParts& parts);
typedef void (*MessageHook)(const char* topic, const char* payload);

// ------------------------------------------------------------------ the controller

class Proptroller {
 public:
  Proptroller();
  ~Proptroller();
  Proptroller(const Proptroller&) = delete;  // one controller per board
  Proptroller& operator=(const Proptroller&) = delete;
  // Start everything: outputs off, settings and phone-made shows from flash, sound, Wi-Fi.
  void begin();
  // Keep everything running: call it in loop(). (Sequences run every 10 ms on their own.)
  void update();
  // A sequence that starts when this trigger is pressed (TRIG1, TRIG2, USER1-3).
  Sequence when(PropInput i);
  // Everything else (ProptrollerAdvanced.h): prop.advanced().cooldown, the network, your own code ...
  PropAdvanced& advanced();

 private:
  friend class PropAdvanced;
  PropAdvanced* adv_ = nullptr;
  // Run a firmware of your own instead of the generic controller (prop.when(...) and the direct-control
  // functions then do nothing; tasks, the network, MQTT topics and the serial port work as usual).
  void begin(FirmwareMaker make);

  // Use the board's built-in pulse-and-recover effect (TRIG1, TRIG2, OUT1, OUT2 are then the effect's).
  void withPulseAndRecover(const PulseAndRecover& effect);
  // Give a terminal your project's own name: nameInput(TRIG2, "REFILL", "Refill button"). The app, the wiring
  // view and the log then show "REFILL (TRIG2)", and input("REFILL") / when("REFILL") find it.
  void nameInput(PropInput i, const char* name, const char* label = "");
  void nameOutput(PropOutput o, const char* name, const char* label = "");
  // Any board setting by its key (the phone's Settings page lists them): setting("name", "Front door").
  void setting(const char* key, long value);
  void setting(const char* key, bool value);
  void setting(const char* key, const char* value);
  // A trigger by its name, or by a project's alias for it from the profile ("REFILL" for TRIG2).
  Sequence when(const char* name) { return when(input(name)); }
  // The input called `name` (a board name or a terminal alias); TRIG1 and a plain-word message when there is none.
  PropInput input(const char* name);
  // Load a profile (a project's own names, firmware/STATE-MACHINE.md §11): the JSON of an alias table. False and
  // a plain-word message when it is refused. Kept on the board; loading changes no value.
  bool loadProfile(const char* json);
  Sequence every(Duration period);
  Sequence atPowerOn(Duration delay = Duration{0});
  Sequence onCue(const char* name);

  // Direct control from loop() or a task.
  void turnOn(PropOutput o);
  void turnOff(PropOutput o);
  void dim(PropOutput o, Percent level);
  void release(PropOutput o);  // give the output back to the shows
  bool isPressed(PropInput i);
  bool wasPressed(PropInput i);  // true once for each new press (a trigger held at power-on never counts)
  void playSound(int number);
  void playSound(int folder, int number);
  void stopSound();
  void setVolume(int volume_0_to_30);
  void sendCue(const char* name);
  void stopEverything();
  // How long a trigger must be held to count, and released to count as released (100 ms is a good guard
  // against taps and bounce; the board's default).
  void triggerHoldTime(Duration time);
  uint64_t timeMs();

  // Going further.
  pt::Controller& controller() { return *ctl_; }
  pt::Engine& engine() { return ctl_->engine(); }
  pt::Board& board() { return ctl_->board(); }
  void addTask(PropTask& t);
  void afterOutputChange(OutputChangeHook h);
  void onRequest(pt::Controller::RequestHook h, void* user = nullptr) {
    if (needController("onRequest()")) ctl_->setRequestHook(h, user);
  }
  bool publish(const char* topic, const char* payload, bool retain = false);
  void onMessage(const char* topic, MessageHook h);
  void tick(uint64_t now_ms);
  // Between ticks: one 1 ms input sample (Firmware::sample).
  void sample(uint64_t now_ms);
  // Opt out of the held-at-power-on rule for one trigger (it may then start a show right after power-up).
  void allowTriggerHeldAtPowerOn(PropInput i);
  // Free heap, stack high-water marks and the boot time, in one line (also the serial command "mem").
  std::string memoryReport();
  // Print a message in the board's log (Serial Monitor and the phone's event log).
  void say(const char* message);

 public:
  struct Platform;
#if !defined(ARDUINO)
  // Host builds (tests, the simulator): the simulated board this controller runs on.
  pt::Sim& hostSim();
  // Host builds: what the network and the serial port talk to.
  pt::NetApp& hostApp();
  // Host builds with a firmware of your own: its simulated board.
  struct pt::SimWorld& hostWorld();
#endif

 private:
  friend class Sequence;
  void lock();
  void stepLock();
  void unlock();
  uint16_t nextShowId() { return next_id_++; }
  void reportProblem(const std::string& what, const pt::ShowError& e, const std::string& extra);
  static void hookAfter(pt::Channel c, pt::Level& l, void* user);
  void runTasks(uint64_t now);
  void start(FirmwareMaker make);
  void serialByte(uint8_t c);
  void serialPush(uint64_t now);
  bool needController(const char* what);
  Platform* plat_ = nullptr;
  pt::Controller* ctl_ = nullptr;
  pt::Firmware* fw_ = nullptr;
  bool generic_ = false;
  std::vector<PropTask*> tasks_;
  std::vector<std::pair<std::string, MessageHook>> subs_;
  uint32_t seen_[pt::INPUT_COUNT] = {0, 0, 0, 0, 0};
  uint16_t next_id_ = 101;
  bool started_ = false;
  OutputChangeHook after_ = nullptr;
  pt::Target show_target_hint_;
  friend bool proptrollerStep(Proptroller&, uint64_t, uint64_t&);
  friend struct ProptrollerAuto;
};
