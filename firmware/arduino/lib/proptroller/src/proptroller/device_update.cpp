// The controller's over-the-air updates (PROTOCOL.md "Firmware updates"): the refusals, update mode, the
// SHA-256 of the body, the progress events, the restart and the self-check of a new image. Writing the image
// is the board's UpdateTarget.
#include "device.h"
#include <ArduinoJson.h>
#include <string.h>

namespace pt {

static std::string errJson(ErrorCode c, const std::string& m) { return Response::err(false, 0, c, m).json(); }

static std::string errorJson(const UpdateError& e) {
  return errJson(e.kind == UpdateError::Kind::Internal ? ErrorCode::Internal : ErrorCode::Invalid, e.message);
}

const char* Controller::updateBusy() const {
  if (updating()) return update_text::ANOTHER_UPDATE;
  if (update_target_ && update_target_->state() == BootState::Pending) return update_text::SELF_CHECK_PENDING;
  if (engine_.anyManual()) return update_text::MANUAL_TEST;
  if (engine_.anyRunning()) return update_text::SHOW_RUNNING;
  if (fx_ && fx_->busy()) return update_text::EFFECT_RUNNING;
  if (scriptEffectRunning()) return update_text::SCRIPT_RUNNING;
  return nullptr;
}

bool Controller::updateBegin(const UpdateRequest& req, std::string& answer) {
  if (!update_target_) { answer = errJson(ErrorCode::Unsupported, update_text::UNSUPPORTED); return false; }
  uint32_t max = update_target_->maxBytes();
  if (locked()) {
    std::string why = pinRefused(req.has_pin, req.pin);
    if (!why.empty()) { answer = errJson(ErrorCode::Pin, why); return false; }
  }
  if (!req.octet_stream) { answer = errJson(ErrorCode::BadRequest, update_text::CONTENT_TYPE); return false; }
  if (!req.has_len) { answer = errJson(ErrorCode::BadRequest, update_text::NO_LENGTH); return false; }
  uint8_t expected[32];
  if (!req.has_sha256 || !parseSha256(req.sha256, expected)) { answer = errJson(ErrorCode::BadRequest, update_text::BAD_SHA_HEADER); return false; }
  if (const char* why = updateBusy()) { answer = errJson(ErrorCode::Refused, why); return false; }
  if (req.len == 0) { answer = errJson(ErrorCode::Invalid, update_text::EMPTY); return false; }
  if (req.len > max) { answer = errJson(ErrorCode::Invalid, updateTooBigText(req.len, max)); return false; }
  uint32_t total = (uint32_t)req.len;
  UpdateError e = update_target_->begin(total);
  if (!e.ok()) { answer = errorJson(e); return false; }
  // update mode: everything stops (nobody is told about the stopped shows), outputs go off on this tick
  size_t keep = engine_.pendingNotices();
  engine_.stopAll(now_);
  engine_.dropNoticesAfter(keep);
  engine_.sound(SoundCommand::simple(SoundCommand::Kind::Stop));
  board_.allOff();
  upd_.active = true;
  upd_.total = total;
  upd_.done = 0;
  upd_.next_event = 0;
  memcpy(upd_.expected, expected, 32);
  upd_.sha.reset();
  upd_.head.clear();
  upd_.header_checked = false;
  log("firmware update: receiving " + std::to_string(total) + " bytes");
  updateEvent("writing", 0, total, nullptr);
  return true;
}

bool Controller::updateWrite(const uint8_t* data, size_t n, std::string& answer) {
  if (!upd_.active || !update_target_) { answer = errJson(ErrorCode::BadRequest, update_text::CONNECTION_LOST); return false; }
  size_t room = upd_.total - upd_.done;
  if (n > room) n = room;
  upd_.sha.update(data, n);
  upd_.done += (uint32_t)n;
  UpdateError e;
  if (upd_.header_checked) {
    e = update_target_->write(data, n);
  } else {
    upd_.head.append((const char*)data, n);
    size_t need = update_target_->headerLen();
    if (need > upd_.total) need = upd_.total;
    if (upd_.head.size() >= need) {
      if (!update_target_->headerOk((const uint8_t*)upd_.head.data(), upd_.head.size())) {
        e = UpdateError::invalid(updateWrongBoardText(identity_.mcu));
      } else {
        upd_.header_checked = true;
        std::string head;
        head.swap(upd_.head);
        e = update_target_->write((const uint8_t*)head.data(), head.size());
      }
    }
  }
  if (!e.ok()) { answer = updateFailed(e); return false; }
  uint32_t done = upd_.done, total = upd_.total;
  if (done >= upd_.next_event + UPDATE_PROGRESS_STEP && done < total) {
    upd_.next_event = done - done % UPDATE_PROGRESS_STEP;
    updateEvent("writing", done, total, nullptr);
  }
  return true;
}

std::string Controller::updateFinish() {
  if (!upd_.active) return errJson(ErrorCode::BadRequest, update_text::CONNECTION_LOST);
  if (upd_.done < upd_.total) return updateFailed(UpdateError::invalid(update_text::CONNECTION_LOST));
  // an image shorter than the header the board checks
  if (!upd_.header_checked) return updateFailed(UpdateError::invalid(updateWrongBoardText(identity_.mcu)));
  uint8_t digest[32];
  upd_.sha.finish(digest);
  if (memcmp(digest, upd_.expected, 32) != 0) return updateFailed(UpdateError::invalid(update_text::SHA_MISMATCH));
  uint32_t total = upd_.total;
  UpdateError e;
  {
    // finishing reads the whole image back from flash (seconds on an ESP32): the step keeps running meanwhile
    // (update mode: outputs off; nothing else touches the target)
    Unlocked u(*this);
    e = update_target_->finish(digest);
  }
  if (!e.ok()) return updateFailed(e);
  upd_.active = false;
  upd_.restart_at_set = true;
  upd_.restart_at = now_ + UPDATE_RESTART_DELAY_MS;
  runs_.flushSoon();  // the lifetime run count is written by the loop before the restart
  log("firmware update: the new image is written and checked; restarting into it");
  updateEvent("restarting", total, total, nullptr);
  return Response::done(false, 0).json();
}

void Controller::updateAbort() {
  if (upd_.active) updateFailed(UpdateError::invalid(update_text::CONNECTION_LOST));
}

std::string Controller::updateFailed(const UpdateError& e) {
  uint32_t done = upd_.active ? upd_.done : 0, total = upd_.active ? upd_.total : 0;
  if (update_target_) update_target_->abort();
  upd_.active = false;
  upd_.head.clear();
  upd_.head.shrink_to_fit();
  std::string answer = errorJson(e);
  log("firmware update stopped: " + e.message);
  updateEvent("failed", done, total, &e.message);
  return answer;
}

void Controller::updateEvent(const char* phase, uint32_t done, uint32_t total, const std::string* message) {
  JsonDocument d;
  d["phase"] = phase;
  d["done"] = done;
  d["total"] = total;
  if (message) d["message"] = *message;
  std::string s;
  serializeJson(d, s);
  events_.push("update", s);
}

void Controller::updateTick(Millis now) {
  if (upd_.restart_at_set && now >= upd_.restart_at) {
    upd_.restart_at_set = false;
    reboot_ = true;
    return;
  }
  if (self_check_done_ || now < UPDATE_SELF_CHECK_MS) return;
  if (!update_target_ || update_target_->state() != BootState::Pending) return;
  // the verdict writes otadata (or the Pico bootloader's state): background() does it, from the loop, once
  self_check_done_ = true;
  self_check_due_ = true;
}

// The self-check's verdict, after UPDATE_SELF_CHECK_MS of a new image: valid when every stored record could be
// read. Marked with the lock given back (only the step runs meanwhile, and after self_check_done_ it no longer
// asks the target). A mark that fails to write goes back to the previous firmware as a failed check does (the
// simulator's failed check is a failing markValid); it is not tried again (README "How it runs").
void Controller::selfCheckVerdict() {
  self_check_due_ = false;
  bool ok = records_ok_;
  UpdateError e = ok ? UpdateError() : UpdateError::internal("the stored settings, shows or script could not be read");
  if (ok) {
    Unlocked u(*this);
    e = update_target_->markValid();
  }
  if (e.ok()) {
    log("the new firmware passed its self-check: it keeps booting");
    // a record converted from an older format can be written now (FLASH-MAP.md "Stored records")
    if (store_migrated_) markDirty();
    if (dirty_) save_due_ = true;
  } else {
    log("the new firmware failed its self-check (" + e.message + "): restarting into the previous one");
    bool back = false;
    {
      Unlocked u(*this);  // the ESP32 restarts in here; the Pico has nothing to write
      back = update_target_->markInvalid();
    }
    if (back) reboot_ = true;
    else log("there is no previous firmware on this board to go back to: flash it over USB");
  }
}

}  // namespace pt
