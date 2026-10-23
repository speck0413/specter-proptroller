#include "board.h"

namespace pt {

Board::Board(BoardPins& pins) : pins_(pins) {
  for (auto& l : levels_) l = 0;
  for (auto& i : inputs_) i = false;
  allOff();
}

void Board::allOff() {
  uint32_t err = 0;
  for (int i = 0; i < 4; i++)
    if (pins_.hasOutput(i)) err += pins_.setOut(i, 0) ? 0 : 1;
  for (int i = 0; i < 4; i++)
    if (pins_.hasOutput(4 + i)) err += pins_.setDigital(i, false) ? 0 : 1;
  if (pins_.hasStatusLed()) err += pins_.setStatusLed(false) ? 0 : 1;
  hal_errors += err;
  for (auto& l : levels_) l = 0;
}

void Board::apply(Millis now, const Level requested[CHANNEL_COUNT], Level* applied) {
  Level req[CHANNEL_COUNT];
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    req[i] = pins_.hasOutput(i) ? requested[i] : 0;  // an absent terminal stays off, never written
    if (before_change && req[i] != levels_[i]) before_change(ALL_CHANNELS[i], req[i], hook_user);
  }
  Level outs[4];
  for (int i = 0; i < 4; i++) outs[i] = req[i] < FULL ? req[i] : FULL;
  uint32_t err = 0;
  for (int i = 0; i < 4; i++) {
    if (outs[i] != levels_[i] && pins_.hasOutput(i)) {
      err += pins_.setOut(i, outs[i] >= FULL ? FULL : outs[i]) ? 0 : 1;
      levels_[i] = outs[i];
      if (after_change) { Level l = levels_[i]; after_change(ALL_CHANNELS[i], l, hook_user); }
    }
  }
  for (int k = 0; k < 4; k++) {
    int i = 4 + k;
    bool on = req[i] > 0;
    if (on && !levels_[i] && was_on_[k] && now - off_at_[k] < RELAY_MIN_DWELL_MS) on = false;  // off at once, on after the dwell
    if (!on && levels_[i]) { off_at_[k] = now; was_on_[k] = true; }
    if (on != (levels_[i] > 0) && pins_.hasOutput(i)) {
      err += pins_.setDigital(k, on) ? 0 : 1;
      levels_[i] = on ? FULL : 0;
      if (after_change) { Level l = levels_[i]; after_change(ALL_CHANNELS[i], l, hook_user); }
    }
  }
  hal_errors += err;
  if (applied)
    for (int i = 0; i < CHANNEL_COUNT; i++) applied[i] = levels_[i];
}

Level Board::set(Millis now, Channel c, Level level) {
  Level req[CHANNEL_COUNT];
  for (int i = 0; i < CHANNEL_COUNT; i++) req[i] = levels_[i];
  req[index(c)] = level;
  apply(now, req);
  return levels_[index(c)];
}

void Board::readInputs(bool out[INPUT_COUNT]) {
  for (int i = 0; i < INPUT_COUNT; i++) {
    bool low = false;
    if (!pins_.hasInput(i)) inputs_[i] = false;  // never read
    else if (pins_.readLow(i, low)) inputs_[i] = low;
    else { inputs_[i] = false; hal_errors++; }
    out[i] = inputs_[i];
  }
}

void Board::setStatusLed(bool on) {
  if (pins_.hasStatusLed() && !pins_.setStatusLed(on)) hal_errors++;
}

BoardReport Board::report() const {
  BoardReport s{};
  for (int i = 0; i < CHANNEL_COUNT; i++) s.levels[i] = levels_[i];
  for (int i = 0; i < INPUT_COUNT; i++) s.inputs[i] = inputs_[i];
  return s;
}

}  // namespace pt
