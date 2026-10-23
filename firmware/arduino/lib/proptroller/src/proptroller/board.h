// The board: owns every terminal and applies levels (the firmware neither measures nor checks load current,
// STATE-MACHINE.md SM-S3; each terminal's rating is in hardware/README.md).
//
// Every output stage is ACTIVE HIGH (a GPIO high switches the driver on) and every input ACTIVE LOW.
// Board's constructor drives all outputs off before it returns.
#pragma once
#include "types.h"

namespace pt {

// The pins of one board, already configured (outputs low, USER1-3 with internal pull-ups, TRIG1-2 none).
// Implemented once per platform (ESP32 LEDC, RP2040 PWM, the simulator). Experts can wrap it to watch or
// change every pin write.
class BoardPins {
 public:
  virtual ~BoardPins() {}
  // OUT1-4 (out = 0..3): level in permille, 0 = fully off, 1000 = fully on. Return false on a HAL error.
  virtual bool setOut(int out, Level level) = 0;
  // Digital outputs: 0 = RLY1, 1 = RLY2, 2 = CC1, 3 = CC2.
  virtual bool setDigital(int which, bool on) = 0;
  // TRIG1, TRIG2, USER1-3 (input = 0..4): true = active (the pin low, or high for an input the pin map says is
  // active high). Return false if unreadable.
  virtual bool readLow(int input, bool& low) = 0;
  // Terminals the board has (a board profile may leave some out: NO_PIN): an absent one is never driven or read.
  virtual bool hasOutput(int channel) { (void)channel; return true; }  // 0-7: OUT1-4, RLY1-2, CC1-2
  virtual bool hasInput(int input) { (void)input; return true; }      // 0-4: TRIG1-2, USER1-3
  virtual bool hasSound() { return true; }   // the DFPlayer port
  virtual bool hasPixels() { return true; }  // the PIXEL port
  virtual bool hasStatusLed() { return false; }
  virtual bool setStatusLed(bool) { return true; }
};

struct BoardReport {
  Level levels[CHANNEL_COUNT];
  bool inputs[INPUT_COUNT];
};

// Called around every output change (advanced hook). `level` can be modified in the before-hook.
typedef void (*OutputHook)(Channel c, Level& level, void* user);

class Board {
 public:
  explicit Board(BoardPins& pins);
  // Switch every output off (boot, e-stop, show stop).
  void allOff();
  // Apply requested levels for all eight channels: OUT1-4 as PWM levels, relays and contacts on for
  // any non-zero level. Writes `applied` (may be null).
  void apply(Millis now, const Level requested[CHANNEL_COUNT], Level* applied = nullptr);
  Level set(Millis now, Channel c, Level level);
  const Level* levels() const { return levels_; }
  // Read every input (active = pin low). Unreadable pins read inactive.
  void readInputs(bool out[INPUT_COUNT]);
  void setStatusLed(bool on);
  BoardReport report() const;
  BoardPins& pins() { return pins_; }
  bool has(Channel c) const { return pins_.hasOutput(index(c)); }
  bool has(Input i) const { return pins_.hasInput(index(i)); }
  bool hasSound() const { return pins_.hasSound(); }
  bool hasPixels() const { return pins_.hasPixels(); }
  uint32_t hal_errors = 0;
  // Advanced: hooks before / after a channel's level changes.
  OutputHook before_change = nullptr;
  OutputHook after_change = nullptr;
  void* hook_user = nullptr;

 private:
  BoardPins& pins_;
  Level levels_[CHANNEL_COUNT]{};
  Millis off_at_[4] = {0, 0, 0, 0};  // RLY1 .. CC2: when each last went off ...
  bool was_on_[4] = {false, false, false, false};  // ... if it ever was on
  bool inputs_[INPUT_COUNT]{};
};

}  // namespace pt
