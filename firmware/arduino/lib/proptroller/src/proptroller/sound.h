// Default sound behaviour: an ambient sound loops while idle, an accepted trigger plays the trigger sound
// once, then the ambient sound resumes. Shows that play their own sound take over until their sound ends.
//
// Sounds are numbered 1 to SOUND_MAX_TRACK, and one number means the same file everywhere (the ambient and
// trigger settings, a script's audio_single / audio_loop, the beginner layer's playSound). SOUND_MODE says how
// the player finds sound N:
//
// | SOUND_MODE         | Sound N is                                              | Play / loop          |
// |--------------------|---------------------------------------------------------|----------------------|
// | SoundMode::Folder  | the one file in folder NN: /NN/001<anything>.mp3        | 0x0F N,1 / 0x17 N    |
// | SoundMode::Mp3     | /mp3/NNNN<anything>.mp3 (the player reads 4 digits)     | 0x12, again on finish|
// | SoundMode::Root    | the N-th file written to the card's root                | 0x03 / 0x08          |
//
// Folder is the default (owner): one sound per folder, the order files were copied in does not matter, and the
// player loops a folder by itself. Datasheet (FN-M16P) 1.4: folders 01-99, files 001-255, and text may follow
// the digits ("001Yesterday Once More.mp3").
//
// A missing file: the ambient stays off until the card is put in again; a missing loop ends the loop.
#pragma once
#include "dfplayer.h"
#include <vector>

namespace pt {

enum class SoundMode : uint8_t { Folder, Mp3, Root };
static const SoundMode SOUND_MODE = SoundMode::Folder;
// The datasheet's limits: folders 01-99 (0x0F / 0x17); the folder "MP3" holds 0001-3000 (3.6.7, 0x12).
static const uint16_t SOUND_MAX_FOLDER = 99, SOUND_MAX_MP3 = 3000;
static const uint16_t SOUND_MAX_TRACK = SOUND_MODE == SoundMode::Folder ? SOUND_MAX_FOLDER : SOUND_MAX_MP3;
// Sound n, once, in SOUND_MODE (or another mode).
SoundCommand soundPlay(uint16_t n, SoundMode mode = SOUND_MODE);
// Sound n, over and over: a folder and a root file loop in the player; in /mp3 it plays once and the director
// plays it again when it finishes. 0: the loop off.
SoundCommand soundLoop(uint16_t n, SoundMode mode = SOUND_MODE);
// The mode a loop command belongs to (LoopFolder, LoopMp3 or LoopIndex).
SoundMode soundLoopMode(const SoundCommand& c);
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
  // comes back when that sound ends. 0: no loop. mode: another mode than SOUND_MODE (the sound op's loop_mp3).
  Commands scriptLoop(uint16_t n, Millis now, SoundMode mode = SOUND_MODE);
  uint16_t loopTrack() const { return loop_; }
  // The last audio_volume a script set (0-30), or -1: kept until it changes or the board restarts.
  int script_volume = -1;

 private:
  Commands ambientCmds();
  bool ambientWanted() const { return cfg.ambient && !ambient_missing_ && online_ && !suspended_; }
  bool loopWanted() const { return loop_ && online_ && !suspended_; }
  Commands loopCmds();
  uint16_t loop_ = 0;
  SoundMode loop_mode_ = SOUND_MODE;
  Playing playing_;
  bool ambient_missing_ = false, online_ = false, suspended_ = false;
  bool has_resume_ = false;
  Millis resume_at_ = 0;
  void resumeAt(Millis t) { has_resume_ = true; resume_at_ = t; }
};

}  // namespace pt
