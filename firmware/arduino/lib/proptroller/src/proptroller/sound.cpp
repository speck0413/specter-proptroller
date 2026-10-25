#include "sound.h"

namespace pt {

SoundCommand soundPlay(uint16_t n, SoundMode mode) {
  switch (mode) {
    case SoundMode::Mp3: return SoundCommand::playMp3(n);
    case SoundMode::Root: return SoundCommand::playIndex(n);
    case SoundMode::Folder:
    default: return SoundCommand::playFolder((uint8_t)n, 1);
  }
}
SoundCommand soundLoop(uint16_t n, SoundMode mode) {
  switch (mode) {
    case SoundMode::Mp3: return SoundCommand::loopMp3(n);
    case SoundMode::Root: return SoundCommand::loopIndex(n);
    case SoundMode::Folder:
    default: return SoundCommand::loopFolder((uint8_t)n);
  }
}
SoundMode soundLoopMode(const SoundCommand& c) {
  return c.kind == SoundCommand::Kind::LoopMp3 ? SoundMode::Mp3 : c.kind == SoundCommand::Kind::LoopIndex ? SoundMode::Root : SoundMode::Folder;
}
uint8_t soundVolume(uint16_t permille) { return (uint8_t)(((uint32_t)(permille > 1000 ? 1000 : permille) * 30 + 500) / 1000); }

Commands SoundDirector::setConfig(const SoundConfig& c, Millis now) {
  bool changed = c.ambient != cfg.ambient || c.ambient_track != cfg.ambient_track;
  cfg = c;
  if (changed) {
    ambient_missing_ = false;
    if (playing_.kind == Playing::Kind::Ambient) {
      playing_ = Playing();
      if (!cfg.ambient) return {SoundCommand::simple(SoundCommand::Kind::Stop)};
    }
    resumeAt(now);
  }
  return {};
}

Commands SoundDirector::ambientCmds() {
  playing_ = Playing();
  playing_.kind = Playing::Kind::Ambient;
  return {SoundCommand::volume(cfg.ambient_volume), soundPlay(cfg.ambient_track)};  // played again when it finishes
}

Commands SoundDirector::loopCmds() {
  playing_ = Playing();
  playing_.kind = Playing::Kind::Loop;
  playing_.track = loop_;
  return {SoundCommand::volume(script_volume >= 0 ? (uint8_t)script_volume : cfg.ambient_volume), soundLoop(loop_, loop_mode_)};
}

Commands SoundDirector::scriptLoop(uint16_t n, Millis now, SoundMode mode) {
  if (n == 0) {
    loop_ = 0;
    if (playing_.kind != Playing::Kind::Loop) return {};
    playing_ = Playing();
    resumeAt(now + RESUME_MS);  // the ambient, when there is one
    return {SoundCommand::simple(SoundCommand::Kind::Stop)};
  }
  if (loop_ == n && loop_mode_ == mode && playing_.kind == Playing::Kind::Loop) return {};  // already looping that one
  loop_ = n;
  loop_mode_ = mode;
  // a sound playing over it (a script's audio_single, a show's sound, the trigger sound) finishes first
  if (playing_.kind == Playing::Kind::Show || playing_.kind == Playing::Kind::Trigger) return {};
  if (!loopWanted()) { resumeAt(now); return {}; }
  has_resume_ = false;
  return loopCmds();
}

Commands SoundDirector::trigger(uint16_t n) {
  uint16_t track = n ? n : cfg.trigger_track;
  if (track < 1) track = 1;
  playing_ = Playing();
  playing_.kind = Playing::Kind::Trigger;
  playing_.track = track;
  has_resume_ = false;
  return {SoundCommand::volume(cfg.trigger_volume), soundPlay(track)};
}

void SoundDirector::observeShow(const SoundCommand& c, Millis now) {
  if (c.startsPlayback()) {
    playing_ = Playing();
    playing_.kind = Playing::Kind::Show;
    has_resume_ = false;
  } else if (c.kind == SoundCommand::Kind::Stop) {
    playing_ = Playing();
    resumeAt(now + RESUME_MS);
  }
}

Commands SoundDirector::suspend() {
  suspended_ = true;
  has_resume_ = false;
  Playing was = playing_;
  playing_ = Playing();
  if (was.kind == Playing::Kind::Idle) return {};
  return {SoundCommand::simple(SoundCommand::Kind::Stop)};
}

void SoundDirector::resume(Millis now) {
  if (suspended_) {
    suspended_ = false;
    resumeAt(now + RESUME_MS);
  }
}

Commands SoundDirector::onEvent(const DfEvent& e, Millis now) {
  switch (e.kind) {
    case DfEvent::Kind::Online:
      online_ = true;
      resumeAt(now);
      break;
    case DfEvent::Kind::Finished:
      if (playing_.kind == Playing::Kind::Loop) {
        if (loop_mode_ == SoundMode::Mp3) return {soundLoop(loop_, SoundMode::Mp3)};  // no loop command: play it again
        break;  // the player loops a folder or a root file itself
      }
      // ambient loops; anything else returns to the loop or the ambient after a short pause
      resumeAt(playing_.kind == Playing::Kind::Ambient ? now : now + RESUME_MS);
      playing_ = Playing();
      break;
    case DfEvent::Kind::Error:
      if (e.value == 6 || e.value == 5) {
        if (playing_.kind == Playing::Kind::Ambient) {
          ambient_missing_ = true;
          playing_ = Playing();
        } else if (playing_.kind == Playing::Kind::Loop) {
          loop_ = 0;  // the loop's file is not on the card: no loop until the script sets one again
          playing_ = Playing();
        } else {
          playing_ = Playing();
          resumeAt(now + RESUME_MS);
        }
      } else {
        if (playing_.kind != Playing::Kind::Ambient) resumeAt(now + RESUME_MS);
        playing_ = Playing();
      }
      break;
    case DfEvent::Kind::CardInserted:
      ambient_missing_ = false;
      resumeAt(now + RESUME_MS);
      break;
    case DfEvent::Kind::CardRemoved:
      playing_ = Playing();
      break;
    case DfEvent::Kind::Ack: case DfEvent::Kind::Reply: case DfEvent::Kind::Timeout:
    default:
      break;
  }
  return {};
}

Commands SoundDirector::tick(Millis now) {
  if (has_resume_ && now >= resume_at_ && playing_.kind == Playing::Kind::Idle) {
    has_resume_ = false;
    if (loopWanted()) return loopCmds();  // a script's loop goes before the ambient
    if (ambientWanted()) return ambientCmds();
  }
  return {};
}

}  // namespace pt
