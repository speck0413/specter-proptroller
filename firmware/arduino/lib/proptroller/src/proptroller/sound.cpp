#include "sound.h"

namespace pt {

Commands SoundDirector::setConfig(const SoundConfig& c, Millis now) {
  bool changed = c.ambient != cfg.ambient || c.ambient_folder != cfg.ambient_folder || c.ambient_track != cfg.ambient_track;
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
  return {SoundCommand::volume(cfg.ambient_volume), SoundCommand::playFolder(cfg.ambient_folder, cfg.ambient_track)};
}

Commands SoundDirector::trigger(uint16_t n) {
  uint16_t track = n ? n : cfg.trigger_track;
  if (track < 1) track = 1;
  playing_ = Playing();
  playing_.kind = Playing::Kind::Trigger;
  playing_.track = track;
  has_resume_ = false;
  return {SoundCommand::volume(cfg.trigger_volume), SoundCommand::playMp3(track)};
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
      // ambient loops; anything else returns to ambient after a short pause
      resumeAt(playing_.kind == Playing::Kind::Ambient ? now : now + RESUME_MS);
      playing_ = Playing();
      break;
    case DfEvent::Kind::Error:
      if (e.value == 6 || e.value == 5) {
        if (playing_.kind == Playing::Kind::Trigger && !playing_.fallback) {
          // not in /mp3: play the N-th file on the card instead
          playing_.fallback = true;
          return {SoundCommand::playIndex(playing_.track)};
        } else if (playing_.kind == Playing::Kind::Ambient) {
          ambient_missing_ = true;
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
    if (ambientWanted()) return ambientCmds();
  }
  return {};
}

}  // namespace pt
