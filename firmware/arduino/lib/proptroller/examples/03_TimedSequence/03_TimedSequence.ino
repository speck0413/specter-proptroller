// 03 A timed sequence
//
// What it does: a doorway scene. When someone steps on the mat: a door-knock sound, the lamp flickers
// (OUT1 blinks), then RLY1 presses a battery-powered prop's "try me" button for half a second while a fan
// ramps up (OUT2). It then rests for 30 seconds before it can run again.
//
// Wiring (power the board from 12 V: OUT1-OUT4 give the board's supply voltage):
//  * a pressure mat on TRIG1;
//  * a 12 V lamp on OUT1, a small 12 V fan on OUT2 (fading dims the fan: keep it to 2 A or less at 12 V,
//    1 A at 24 V);
//  * the "try me" button of a battery-powered prop wired to RLY1 COM and NO (a dry contact: it just closes,
//    like the button). Low voltage only: never a fog machine's wired remote (on many machines it carries
//    mains); a fog machine with a DMX IN socket goes over DMX (firmware/SHOW-SCRIPT.md section 7);
//  * sounds: a knock as sound 1 (01/001-knock.mp3) on the microSD card.
//
// Each step starts at the same moment as the one before, unless you write then() (wait until everything so
// far is finished) or wait(...).

#include <ProptrollerAdvanced.h>

Proptroller prop;

void setup() {
  prop.begin();
  prop.advanced().when(TRIG1)
      .named("Doorway")
      .playSound(1)
      .blink(OUT1, 6, milliseconds(100), milliseconds(150))
      .then()
      .turnOn(RLY1, seconds(0.5))
      .fadeIn(OUT2, seconds(1.5))
      .wait(seconds(4))
      .turnOff(OUT2)
      .cooldown(seconds(30));
}

void loop() {
  prop.update();
}
