// 04 Ambient plus a trigger
//
// What it does: while nobody is around, a candle flickers on the pixel strip and a dim glow stays on OUT3;
// the ambient sound loops (sound 2, the board does this by itself). When the motion sensor triggers, the
// scene takes over: a bright flash, a sound and the lamp at full; when the scene ends, the ambient look
// comes back by itself.
//
// Wiring:
//  * a motion sensor with a relay or open-collector output on TRIG2 (tip and sleeve);
//  * a WS2812 / NeoPixel strip on PIXEL (5 V, data, ground; up to about 16 LEDs at full white). Fit a
//    470-1000 uF capacitor (6.3 V or more) across 5 V and ground where the strip's wires start, and plug or
//    unplug the strip only with the power off;
//  * a 12 V lamp on OUT3 (power the board from 12 V; the dim glow is dimming: 2 A or less);
//  * the microSD card: the scene's sound as sound 1 (01/001-scene.mp3), the ambient loop as sound 2
//    (02/001-ambient.mp3).
//
// priority(0) makes the ambient look give way to the scene (priority 1, the default) on shared outputs.

#include <ProptrollerAdvanced.h>

Proptroller prop;

void setup() {
  prop.begin();

  prop.advanced().atPowerOn()
      .named("Ambient")
      .priority(0)
      .repeatForever()
      .flickerPixels(ORANGE)
      .dim(OUT3, percent(20))
      .lastsFor(seconds(10));

  prop.advanced().when(TRIG2)
      .named("Scene")
      .playSound(1)
      .flashPixels(WHITE, milliseconds(120))
      .turnOn(OUT3)
      .wait(seconds(3))
      .setPixels(RED)
      .wait(seconds(3))
      .cooldown(seconds(20));
}

void loop() {
  prop.update();
}
