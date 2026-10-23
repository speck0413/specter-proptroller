// Debounce, trigger arming and button gestures. Every input is active low; the Board turns pin levels
// into "active" booleans before they get here.
//
// * Debouncer: a level must be stable for debounce_ms before it is accepted.
// * Trigger: reports a press only after it has been seen RELEASED for release_ms. At power-on it starts
//   disarmed, so a trigger held (or stuck, or wired normally closed) at boot never triggers.
// * Button: press / release / click / long press for the USER buttons; like a trigger, a button held at power-on
//   gives nothing until it has been released.
#pragma once
#include "types.h"

namespace pt {

enum class Edge : uint8_t { None, Rise, Fall };

class Debouncer {
 public:
  explicit Debouncer(uint32_t debounce_ms = 20) : debounce_ms_(debounce_ms) {}
  void setDebounceMs(uint32_t ms) { debounce_ms_ = ms; }
  bool isActive() const { return stable_; }
  Edge update(bool raw_active, Millis now);

 private:
  uint32_t debounce_ms_;
  bool stable_ = false, candidate_ = false, started_ = false;
  Millis since_ = 0;
};

enum class TriggerEvent : uint8_t { None, Pressed, Released, IgnoredNotArmed };


class Trigger {
 public:
  explicit Trigger(uint32_t debounce_ms = TRIGGER_DEBOUNCE_MS, uint32_t release_ms = TRIGGER_DEBOUNCE_MS)
      : deb_(debounce_ms), release_ms_(release_ms) {}
  void setTimes(uint32_t debounce_ms, uint32_t release_ms) { deb_.setDebounceMs(debounce_ms); release_ms_ = release_ms; }
  bool isActive() const { return deb_.isActive(); }
  bool isReady() const { return ready_; }
  bool waitingForRelease() const { return !ready_; }
  TriggerEvent update(bool raw_active, Millis now);
  // Expert escape hatch: arm immediately (the held-at-boot rule no longer applies to this trigger).
  void forceArm() { ready_ = true; }

 private:
  Debouncer deb_;
  uint32_t release_ms_;
  bool has_released_ = false;
  Millis released_at_ = 0;
  bool ready_ = false;
};

enum class ButtonEventKind : uint8_t { None, Pressed, LongPress, Click, Released };
struct ButtonEvent {
  ButtonEventKind kind = ButtonEventKind::None;
  uint32_t held_ms = 0;  // for Released
  bool operator==(ButtonEventKind k) const { return kind == k; }
};

static const uint32_t BUTTON_DEBOUNCE_MS = 30;
static const uint32_t BUTTON_LONG_MS = 1000;

class Button {
 public:
  explicit Button(uint32_t debounce_ms = BUTTON_DEBOUNCE_MS, uint32_t long_ms = BUTTON_LONG_MS)
      : deb_(debounce_ms), long_ms_(long_ms), open_ms_(debounce_ms) {}
  bool isPressed() const { return deb_.isActive(); }
  // How long it has been held (0 when not pressed).
  bool heldMs(Millis now, uint32_t& out) const;
  ButtonEvent update(bool raw_active, Millis now);

 private:
  Debouncer deb_;
  uint32_t long_ms_;
  bool pressed_ = false;
  Millis pressed_at_ = 0;
  bool long_sent_ = false;
  // at power-on as if held: a button gives events only once it has been seen open for the debounce time
  uint32_t open_ms_;
  bool armed_ = false, has_open_ = false;
  Millis open_at_ = 0;
};

}  // namespace pt
