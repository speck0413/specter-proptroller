// The show engine: triggers, scheduling, arbitration, cooldown, soft start, interlocks.
//
// Feed it events as they happen (handle(), press()) and call tick() regularly (every 10 ms on the firmware).
// Each tick returns a Frame: the level of every output, player commands, the active pixel effect, named
// cues to broadcast and notices for the UI.
//
// Rules:
// * Start: a show starts from any of its triggers. While it runs, a trigger is ignored (default), restarts
//   it or stops it. After it ends it rests for cooldown_ms (runs_per_cooldown runs per window); triggers
//   in that time are ignored, never queued.
// * Arbitration: when two running shows drive the same output, the higher priority wins, then the one
//   started last. An output a show has not reached yet is left to the others.
// * Shaping: every OUT has a soft-start envelope and every output an optional max-on watchdog.
//   Interlocks keep the output that was on first.
// * Pause: a pause input (held) or a pause request stops every show, switches everything off and ignores
//   triggers until released (the building's show-stop hook).
#pragma once
#include "ramp.h"
#include "show.h"
#include "show.h"
#include "window.h"
#include <memory>
#include <vector>

namespace pt {

struct OutputConfig {
  uint32_t soft_start_ms = 0;
  uint32_t max_on_ms = 0;
};

struct EngineConfig {
  OutputConfig outputs[CHANNEL_COUNT];
  std::vector<std::pair<Channel, Channel>> interlocks;
  bool has_pause_input = false;
  Input pause_input = Input::Trig2;
};

struct Cause {
  enum class By : uint8_t { ByInput, ByRelease, ByTimer, ByBoot, ByCue, ByAfter, ByApi };
  By by = By::ByApi;
  Input input = Input::Trig1;
  std::string name;
  uint16_t show = 0;
  bool operator==(const Cause& o) const;
  const char* byName() const;
};

// Released: an `until_release` run whose input was let go
enum class EndReason : uint8_t { Finished, Stopped, Restarted, Paused, Replaced, Released };
enum class [[nodiscard]] Ignored : uint8_t { None, Running, Cooldown, Disabled, Paused, Unknown };
const char* endReasonName(EndReason r);
const char* ignoredName(Ignored i);

struct Notice {
  enum class Kind : uint8_t { Started, Ended, IgnoredTrigger, Watchdog, Interlock, PausedChanged };
  Kind kind = Kind::Started;
  uint16_t show = 0;
  Cause cause;
  EndReason reason = EndReason::Finished;
  Ignored why = Ignored::None;
  Channel channel = Channel::Out1;
  bool paused = false;
  bool operator==(const Notice& o) const;
  static Notice started(uint16_t s, const Cause& c) { Notice n; n.kind = Kind::Started; n.show = s; n.cause = c; return n; }
  static Notice ended(uint16_t s, EndReason r) { Notice n; n.kind = Kind::Ended; n.show = s; n.reason = r; return n; }
  static Notice ignored(uint16_t s, Ignored w) { Notice n; n.kind = Kind::IgnoredTrigger; n.show = s; n.why = w; return n; }
};

struct Driver {
  enum class Kind : uint8_t { Idle, ByShow, Manual, Script };
  Kind kind = Kind::Idle;
  uint16_t show = 0;
};

enum class Block : uint8_t { None, Interlock, Watchdog, Paused };

struct Frame {
  Level levels[CHANNEL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
  std::vector<SoundCommand> sound;
  bool has_pixel = false;
  Effect pixel;
  Millis pixel_start = 0;
  std::vector<std::string> cues_out;
  std::vector<Notice> notices;
};

struct ShowStatus {
  enum class Kind : uint8_t { Idle, Running, Cooldown, Disabled };
  Kind kind = Kind::Idle;
  uint32_t elapsed_ms = 0, length_ms = 0, remaining_ms = 0;
  bool operator==(const ShowStatus& o) const {
    return kind == o.kind && elapsed_ms == o.elapsed_ms && length_ms == o.length_ms && remaining_ms == o.remaining_ms;
  }
};

class Engine {
 public:
  explicit Engine(const EngineConfig& cfg = EngineConfig());
  const EngineConfig& config() const { return cfg_; }
  void setConfig(const EngineConfig& cfg);

  // Shows, in id order.
  size_t showCount() const { return shows_.size(); }
  const Show& showAt(size_t i) const { return shows_[i].show; }
  const Show* show(uint16_t id) const;
  // Add or replace a show after validating it. A running copy is stopped.
  [[nodiscard]] ShowError setShow(Show s);
  bool deleteShow(uint16_t id);

  bool isPaused() const { return paused_; }
  bool isRunning(uint16_t id) const;
  bool anyRunning() const { return !runs_.empty(); }
  // A manual output (above 0: "Off" holds nothing on) or pixel test is on.
  bool anyManual() const {
    for (int i = 0; i < CHANNEL_COUNT; i++)
      if (has_manual_[i] && manual_[i] > 0) return true;
    return has_manual_pixel_;
  }
  // Notices waiting for the next tick: how many, and drop those after the first n (a firmware update's stop,
  // which nobody is told about).
  size_t pendingNotices() const { return notices_.size(); }
  void dropNoticesAfter(size_t n) { if (notices_.size() > n) notices_.resize(n); }
  bool status(uint16_t id, Millis now, ShowStatus& out) const;
  // For a show with more than one run per cooldown: runs left in this window and ms until it ends.
  bool runsLeft(uint16_t id, Millis now, uint32_t& left, uint32_t& window_ms) const;

  void setManual(Channel c, bool on, Level level);  // on=false releases
  bool manual(Channel c, Level& level) const;
  // The level of an output a show script, an output role or the effect owns (owned=false gives it back); shaped,
  // interlocked and paused like a show, and a manual test still wins.
  void setScript(Channel c, bool owned, Level level) { has_script_[(int)c] = owned; script_[(int)c] = level; }
  void setManualPixel(const Effect* e);  // null releases
  void sound(const SoundCommand& c) { out_sound_.push_back(c); }

  // Events. Each returns Ignored::None when it was accepted.
  Ignored pressed(Millis now, Input i);
  Ignored released(Millis now, Input i);
  void cue(Millis now, const std::string& name);
  Ignored run(Millis now, uint16_t id);
  void stop(Millis now, uint16_t id);
  void stopAll(Millis now);
  void pause(Millis now, bool on);
  // An input press: the ids of the shows it started (empty when no show listens: a plain trigger), or why
  // every listening show ignored it.
  Ignored press(Millis now, Input i, std::vector<uint16_t>& started);

  Frame tick(Millis now);

  // The fresh starts since the last call (a run counter's runs): a restart of a running show and a loop are not
  // fresh starts, as for the cooldown window.
  struct FreshRun {
    uint16_t show;
    Cause cause;
  };
  static const size_t FRESH_RUNS_MAX = 64;
  std::vector<FreshRun> takeFreshRuns() {
    std::vector<FreshRun> v;
    v.swap(fresh_);
    return v;
  }

  const Driver* drivers() const { return drivers_; }
  const Level* requested() const { return last_req_; }
  Block blocked(int i) const;
  bool ramping(Channel c) const;

 private:
  struct Rt {
    Show show;
    std::vector<std::unique_ptr<Compiled>> compiled;
    uint32_t length = 0;
    RunWindow window;
    std::vector<bool> has_next;
    std::vector<Millis> next_every;
    bool boot_done = false;
    explicit Rt(const Show& s);
    Rt(Rt&&) = default;
    Rt& operator=(Rt&&) = default;
  };
  struct Run {
    uint16_t show{};
    Millis start{};
    uint64_t seq{};
    std::vector<size_t> cursor;
    bool played_sound = false;
    bool held = false;  // started by an `until_release` press of `input`: ends at its release
    Input input = Input::Trig1;
  };
  int indexOf(uint16_t id) const;
  Ignored tryStart(int idx, const Cause& c, Millis now);
  Ignored startInner(int idx, const Cause& c, Millis now);
  void startRun(int idx, const Cause& c, Millis now);
  void endRunsOf(uint16_t id, EndReason r);
  void finishRun(const Run& run, EndReason r, Millis now);
  void setPaused(bool p);
  void rebuildShapers();
  void pixelLayer(Millis now, Frame& f) const;
  void bump(Millis now) { if (now > last_now_) last_now_ = now; }

  EngineConfig cfg_;
  std::vector<Rt> shows_;
  std::vector<Run> runs_;
  uint64_t seq_ = 0;
  Shaper shapers_[CHANNEL_COUNT];
  bool blocked_[CHANNEL_COUNT] = {false};
  bool has_manual_[CHANNEL_COUNT] = {false};
  bool has_script_[CHANNEL_COUNT] = {false};
  Level script_[CHANNEL_COUNT] = {0};
  Level manual_[CHANNEL_COUNT] = {0};
  bool has_manual_pixel_ = false;
  Effect manual_pixel_;
  bool paused_ = false;
  std::vector<std::string> pending_cues_;
  std::vector<uint16_t> pending_after_;
  std::vector<SoundCommand> out_sound_;
  std::vector<std::string> out_cues_;
  std::vector<Notice> notices_;
  std::vector<FreshRun> fresh_;
  Millis last_now_ = 0;
  Driver drivers_[CHANNEL_COUNT];
  Level last_req_[CHANNEL_COUNT] = {0};
};

}  // namespace pt
