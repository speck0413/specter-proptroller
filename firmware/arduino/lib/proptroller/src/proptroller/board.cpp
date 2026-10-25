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
  settled_ = false;
  led_ = 0;
}

PT_STEP_CODE void Board::apply(Millis now, const Level requested[CHANNEL_COUNT], Level* applied) {
  // every tick: the same request as last time and all of it on the pins is nothing to do (no hooks, no pin calls)
  bool same = settled_;
  for (int i = 0; i < CHANNEL_COUNT && same; i++) same = requested[i] == last_req_[i];
  if (same) {
    if (applied)
      for (int i = 0; i < CHANNEL_COUNT; i++) applied[i] = levels_[i];
    return;
  }
  for (int i = 0; i < CHANNEL_COUNT; i++) last_req_[i] = requested[i];
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
  // settled: every relay and contact is where it was asked to be (one held off for its dwell is not) and no pin call
  // failed
  settled_ = err == 0;
  for (int k = 0; k < 4 && settled_; k++) settled_ = !pins_.hasOutput(4 + k) || (req[4 + k] > 0) == (levels_[4 + k] > 0);
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

PT_STEP_CODE void Board::readInputs(bool out[INPUT_COUNT]) {
  bool all[INPUT_COUNT];
  if (pins_.readAllLow(all)) {
    for (int i = 0; i < INPUT_COUNT; i++) out[i] = inputs_[i] = all[i];
    return;
  }
  for (int i = 0; i < INPUT_COUNT; i++) {
    bool low = false;
    if (!pins_.hasInput(i)) inputs_[i] = false;  // never read
    else if (pins_.readLow(i, low)) inputs_[i] = low;
    else { inputs_[i] = false; hal_errors++; }
    out[i] = inputs_[i];
  }
}

PT_STEP_CODE void Board::setStatusLed(bool on) {
  if (led_ == (on ? 1 : 0)) return;  // every tick: written only when it changes
  if (pins_.hasStatusLed() && !pins_.setStatusLed(on)) { hal_errors++; return; }
  led_ = on ? 1 : 0;
}

BoardReport Board::report() const {
  BoardReport s{};
  for (int i = 0; i < CHANNEL_COUNT; i++) s.levels[i] = levels_[i];
  for (int i = 0; i < INPUT_COUNT; i++) s.inputs[i] = inputs_[i];
  return s;
}

}  // namespace pt
