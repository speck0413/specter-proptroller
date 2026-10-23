// 01 Blink an output
//
// What it does: switches OUT1 on for one second, every two seconds, forever.
//
// Wiring: a 12 V lamp or LED strip between OUT1 + and OUT1 -. Power the board from 12 V on the barrel
// jack. Nothing else is needed.
//
// Try: change seconds(1) to seconds(0.2) for a quick flash, or OUT1 to OUT2.

#include <ProptrollerAdvanced.h>

Proptroller prop;

void setup() {
  prop.begin();
  prop.advanced().every(seconds(2)).turnOn(OUT1, seconds(1));
}

void loop() {
  prop.update();
}
