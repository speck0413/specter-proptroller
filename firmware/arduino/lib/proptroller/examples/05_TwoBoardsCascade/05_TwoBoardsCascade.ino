// 05 Two boards in a cascade
//
// What it does: the first board starts its own scene and, at the same moment, starts the second board.
// Load this sketch on both boards; change FIRST_BOARD to false for the second one.
//
// Wiring (contact closure, works with every board):
//  * a 3.5 mm cable from CC1 of the first board to TRIG1 of the second board. CC1 is an isolated contact
//    that closes for 0.2 s every time a trigger is accepted (its default job, "trigger repeat").
//  * a button on TRIG1 of the first board; lamps on OUT1 of both.
// Wireless (ESP32 boards on the same Wi-Fi channel): sendCue("go") on the first board starts every
// show with onCue("go") on the other boards, no cable needed.

#include <ProptrollerAdvanced.h>

const bool FIRST_BOARD = true;

Proptroller prop;

void setup() {
  prop.begin();
  if (FIRST_BOARD) {
    prop.advanced().when(TRIG1).turnOn(OUT1, seconds(2)).sendCue("go");
  } else {
    // started by the cable on TRIG1, or by the radio cue
    prop.advanced().when(TRIG1).alsoOnCue("go").turnOn(OUT1, seconds(2)).cooldown(seconds(3));
  }
}

void loop() {
  prop.update();
}
