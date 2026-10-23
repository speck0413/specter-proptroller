// Default sound behaviour: an ambient track loops while idle, an accepted trigger plays the trigger sound
// once, then the ambient track resumes. Shows that play their own sound take over until their sound ends.
//
// Files on the microSD card (the DFPlayer plays files by NUMBER, never by name; numbers start at 1):
//
// | File                          | Used as                                  |
// |-------------------------------|------------------------------------------|
// | /mp3/0001.mp3                 | trigger sound 1 (the default)            |
// | /mp3/0002.mp3, ...            | more trigger sounds                      |
// | /01/001.mp3                   | ambient loop                             |
//
// If /mp3/NNNN is missing, the trigger sound falls back to the N-th file on the card; if the ambient file
// is missing, ambient stays off until the next power-up (a one-file card is silent between triggers).
#pragma once
#include "dfplayer.h"
#include <vector>

namespace pt {

struct SoundConfig {
  bool ambient = true;
  uint8_t ambient_folder = 1;
  uint8_t ambient_track = 1;
  uint8_t ambient_volume = 15;
  uint16_t trigger_track = 1;
  uint8_t trigger_volume = 25;
};

struct Playing {
  enum class Kind : uint8_t { Idle, Ambient, Trigger, Show };
  Kind kind = Kind::Idle;
  uint16_t track = 0;
  bool fallback = false;
  bool operator==(const Playing& o) const { return kind == o.kind && track == o.track && fallback == o.fallback; }
};

static const Millis RESUME_MS = 300;

typedef std::vector<SoundCommand> Commands;

class SoundDirector {
 public:
  explicit SoundDirector(const SoundConfig& c = SoundConfig()) : cfg(c) {}
  SoundConfig cfg;
  Playing playing() const { return playing_; }
  bool ambientMissing() const { return ambient_missing_; }
  Commands setConfig(const SoundConfig& c, Millis now);
  // An accepted trigger with no sound of its own: play trigger sound n (0 = the configured one).
  Commands trigger(uint16_t n = 0);
  void observeShow(const SoundCommand& c, Millis now);
  Commands suspend();
  void resume(Millis now);
  Commands onEvent(const DfEvent& e, Millis now);
  Commands tick(Millis now);

 private:
  Commands ambientCmds();
  bool ambientWanted() const { return cfg.ambient && !ambient_missing_ && online_ && !suspended_; }
  Playing playing_;
  bool ambient_missing_ = false, online_ = false, suspended_ = false;
  bool has_resume_ = false;
  Millis resume_at_ = 0;
  void resumeAt(Millis t) { has_resume_ = true; resume_at_ = t; }
};

}  // namespace pt
