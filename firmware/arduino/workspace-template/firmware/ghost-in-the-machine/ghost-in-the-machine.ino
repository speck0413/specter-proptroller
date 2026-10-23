// Ghost in the machine: a pressure mat on TRIG1 wakes the ghost. A moan plays (02/001.mp3), the 12 V lamp on OUT1
// blinks, and a 12 V fan on OUT2 blows for 5 seconds (power the board from 12 V: OUT1-OUT4 give its supply voltage). The board runs everything else by itself (the phone page, show scripts,
// updates), so loop() is free for your own code.
#include <ProptrollerAdvanced.h>

Proptroller prop;

void setup() {
  Serial.begin(115200);
  prop.advanced().when(TRIG1).playSound(1).blink(OUT1, 6, milliseconds(200), milliseconds(300)).turnOn(OUT2, seconds(5));
}

void loop() {
  // your own code; a delay() here never holds up the ghost
  Serial.println("waiting for a visitor");
  delay(5000);
}
