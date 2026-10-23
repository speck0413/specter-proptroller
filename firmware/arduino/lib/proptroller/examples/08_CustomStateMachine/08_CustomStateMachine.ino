// 08 Your own state machine
//
// What it does: a drawbridge. A button (TRIG1) lowers it: the motor runs DOWN (RLY2) until the bottom
// limit switch (TRIG2) closes, waits 10 s, then runs UP (RLY1) for a fixed 6 s. A warning lamp (OUT4) blinks
// whenever it moves. If the bottom switch never closes, the motor stops after 8 s (a safety timeout).
//
// This is the "going further" pattern: a class with an update() that runs every 10 ms, a variable that
// says which state it is in, and a switch that decides what happens next. The same prop functions as in
// loop() switch the outputs. Turn on Settings -> Outputs -> "RLY1 / RLY2 never together" so the two
// directions can never be on at once, even if the code is wrong.
//
// Wiring: the motor's reversing relays on RLY1 (up) and RLY2 (down), for a low-voltage DC motor (30 V DC at
// most on a rev A board); the button on TRIG1; the bottom limit switch on TRIG2; a 12 V lamp on OUT4 (power the
// board from 12 V).

#include <ProptrollerAdvanced.h>

Proptroller prop;

// The times, in milliseconds (update() gets the time in milliseconds).
const uint64_t LOWERING_TIMEOUT_MS = 8000;  // the bottom switch should close before this
const uint64_t WAIT_AT_BOTTOM_MS = 10000;   // how long it stays down
const uint64_t RAISING_TIME_MS = 6000;      // how long the motor runs up

class Drawbridge : public PropTask {
 public:
  enum State { UP, LOWERING, DOWN, RAISING };
  State state = UP;
  uint64_t since = 0;  // when the current state started

  void enter(State s, uint64_t now) {
    state = s;
    since = now;
  }

  void update(PropAdvanced& p, uint64_t now) override {
    uint64_t in_state = now - since;
    switch (state) {
      case UP:
        if (p.wasPressed(TRIG1)) {
          p.turnOn(RLY2);
          p.turnOn(OUT4);
          enter(LOWERING, now);
        }
        break;
      case LOWERING:
        if (p.isPressed(TRIG2) || in_state > LOWERING_TIMEOUT_MS) {
          p.turnOff(RLY2);
          p.turnOff(OUT4);
          if (in_state > LOWERING_TIMEOUT_MS) p.say("drawbridge: the bottom switch never closed, stopped the motor");
          enter(DOWN, now);
        }
        break;
      case DOWN:
        if (in_state > WAIT_AT_BOTTOM_MS) {
          p.turnOn(RLY1);
          p.turnOn(OUT4);
          enter(RAISING, now);
        }
        break;
      case RAISING:
        if (in_state > RAISING_TIME_MS) {
          p.turnOff(RLY1);
          p.turnOff(OUT4);
          enter(UP, now);
        }
        break;
    }
  }
};

Drawbridge bridge;

void setup() {
  prop.begin();
  prop.advanced().addTask(bridge);
}

void loop() {
  prop.update();
}
