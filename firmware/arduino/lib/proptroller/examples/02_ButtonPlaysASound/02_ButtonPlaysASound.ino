// 02 A button plays a sound
//
// What it does: when someone presses the button, the board plays sound 1 and lights OUT1 for 3 seconds.
//
// Wiring:
//  * a push button (or a pressure mat) on TRIG1: a 3.5 mm plug, tip and sleeve. Pressing it joins them.
//  * a DFPlayer Mini in its socket, a speaker on SPK (or an amplifier on LINE OUT).
//  * the microSD card: your sound as sound 1, the file mp3/0001-<name>.mp3 (Studio's SD card tool does this).
//  * a lamp on OUT1.
//
// A button that is already pressed when the board powers up never triggers: release it once first.

#include <ProptrollerAdvanced.h>

Proptroller prop;

void setup() {
  prop.begin();
  prop.advanced().when(TRIG1).playSound(1).turnOn(OUT1, seconds(3)).cooldown(seconds(5));
}

void loop() {
  prop.update();
}
