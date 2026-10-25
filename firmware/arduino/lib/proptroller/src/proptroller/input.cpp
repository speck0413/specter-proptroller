#include "input.h"

namespace pt {

static inline Millis since(Millis now, Millis t) { return now > t ? now - t : 0; }

PT_STEP_CODE Edge Debouncer::update(bool raw, Millis now) {
  if (!started_) { started_ = true; candidate_ = raw; since_ = now; }
  if (raw != candidate_) { candidate_ = raw; since_ = now; }
  if (candidate_ != stable_ && since(now, since_) >= debounce_ms_) {
    stable_ = candidate_;
    return stable_ ? Edge::Rise : Edge::Fall;
  }
  return Edge::None;
}

PT_STEP_CODE TriggerEvent Trigger::update(bool raw, Millis now) {
  Edge edge = deb_.update(raw, now);
  // arming: raw and debounced level both inactive for release_ms; any raw activity restarts it
  if (raw) {
    has_released_ = false;
  } else if (!deb_.isActive()) {
    if (!has_released_) { has_released_ = true; released_at_ = now; }
    if (!ready_ && since(now, released_at_) >= release_ms_) ready_ = true;
  }
  if (edge == Edge::Rise) {
    has_released_ = false;
    if (ready_) { ready_ = false; return TriggerEvent::Pressed; }
    return TriggerEvent::IgnoredNotArmed;
  }
  if (edge == Edge::Fall) {
    has_released_ = true;
    released_at_ = now;
    return TriggerEvent::Released;
  }
  return TriggerEvent::None;
}

bool Button::heldMs(Millis now, uint32_t& out) const {
  if (!pressed_) return false;
  out = (uint32_t)since(now, pressed_at_);
  return true;
}

PT_STEP_CODE ButtonEvent Button::update(bool raw, Millis now) {
  ButtonEvent ev;
  Edge e = deb_.update(raw, now);
  if (!armed_) {
    if (raw || deb_.isActive()) has_open_ = false;
    else if (!has_open_) { has_open_ = true; open_at_ = now; }
    armed_ = has_open_ && since(now, open_at_) >= open_ms_;
    return ev;
  }
  if (e == Edge::Rise) {
    pressed_ = true;
    pressed_at_ = now;
    long_sent_ = false;
    ev.kind = ButtonEventKind::Pressed;
  } else if (e == Edge::Fall) {
    uint32_t held = pressed_ ? (uint32_t)since(now, pressed_at_) : 0;
    pressed_ = false;
    if (held < long_ms_ && !long_sent_) ev.kind = ButtonEventKind::Click;
    else { ev.kind = ButtonEventKind::Released; ev.held_ms = held; }
  } else if (pressed_ && !long_sent_ && since(now, pressed_at_) >= long_ms_) {
    long_sent_ = true;
    ev.kind = ButtonEventKind::LongPress;
  }
  return ev;
}

}  // namespace pt
