// Default sound behaviour: an ambient sound loops while idle, an accepted trigger plays the trigger sound
// once, then the ambient sound resumes. Shows that play their own sound take over until their sound ends.
//
// Sounds are numbered 1 to SOUND_MAX_TRACK, and one number means the same file everywhere (the ambient and
// trigger settings, a script's audio_single / audio_loop, the beginner layer's playSound). SOUND_MODE says how
// the player finds sound N:
//
// | SOUND_MODE         | Sound N is                                              | Play / loop          |
// |--------------------|---------------------------------------------------------|----------------------|
// | SoundMode::Mp3     | /mp3/NNNN<anything>.mp3 (the player reads 4 digits)     | 0x12, again on finish|
// | SoundMode::Root    | the N-th file written to the card's root                | 0x03 / 0x08          |
//
// A missing file: the ambient stays off until the card is put in again; a missing loop ends the loop.
#pragma once
#include "dfplayer.h"
#include <vector>

namespace pt {

enum class SoundMode : uint8_t { Mp3, Root };
static const SoundMode SOUND_MODE = SoundMode::Mp3;
// DFPlayer Mini datasheet (FN-M16P), 3.6.7: the folder "MP3" holds tracks 0001 to 3000 (0x12 plays one).
static const uint16_t SOUND_MAX_TRACK = 3000;
// Sound n, once, in SOUND_MODE (or in /mp3 when mp3 is true).
SoundCommand soundPlay(uint16_t n, bool mp3 = SOUND_MODE == SoundMode::Mp3);
// Sound n, over and over: in /mp3 it plays once and the director plays it again when it finishes.
SoundCommand soundLoop(uint16_t n, bool mp3 = SOUND_MODE == SoundMode::Mp3);
// A script's audio_volume = <level>: a percentage (permille 0-1000) on the player's 0-30, halves up.
uint8_t soundVolume(uint16_t permille);

struct SoundConfig {
  bool ambient = true;
  uint16_t ambient_track = 2;
  uint8_t ambient_volume = 15;
  uint16_t trigger_track = 1;
  uint8_t trigger_volume = 25;
};

struct Playing {
  enum class Kind : uint8_t { Idle, Ambient, Trigger, Show, Loop };
  Kind kind = Kind::Idle;
  uint16_t track = 0;
  bool operator==(const Playing& o) const { return kind == o.kind && track == o.track; }
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
  // A show script's background loop (audio_loop, SHOW-SCRIPT.md SS-R7): sound n loops, at the ambient volume (or
  // the script's audio_volume once it set one); a sound played over it (audio_single) takes over and the loop
  // comes back when that sound ends. 0: no loop. mp3: loop /mp3/NNNN whatever SOUND_MODE is (the sound op's
  // loop_mp3).
  Commands scriptLoop(uint16_t n, Millis now, bool mp3 = SOUND_MODE == SoundMode::Mp3);
  uint16_t loopTrack() const { return loop_; }
  // The last audio_volume a script set (0-30), or -1: kept until it changes or the board restarts.
  int script_volume = -1;

 private:
  Commands ambientCmds();
  bool ambientWanted() const { return cfg.ambient && !ambient_missing_ && online_ && !suspended_; }
  bool loopWanted() const { return loop_ && online_ && !suspended_; }
  Commands loopCmds();
  uint16_t loop_ = 0;
  bool loop_mp3_ = SOUND_MODE == SoundMode::Mp3;
  Playing playing_;
  bool ambient_missing_ = false, online_ = false, suspended_ = false;
  bool has_resume_ = false;
  Millis resume_at_ = 0;
  void resumeAt(Millis t) { has_resume_ = true; resume_at_ = t; }
};

}  // namespace pt
