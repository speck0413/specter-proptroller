// Tracking babies: a distance sensor on expansion S3 (analog in). While someone stands close, the doll heads'
// motor on OUT2 runs; USER1 on the board runs it for 2 seconds to test the wiring.
#include <ProptrollerAdvanced.h>

Proptroller prop;

#if defined(ESP32)
const int SENSOR_PIN = 39;  // expansion S3 on an ESP32 DevKit
#else
const int SENSOR_PIN = 26;  // expansion S3 on a Pico (GP26)
#endif
const int CLOSE = 2000;     // a reading (0-4095) that means "someone is close"

void setup() {
  prop.when(USER1).turnOn(OUT2, seconds(2));
}

void loop() {
  int distance = analogRead(SENSOR_PIN);
  if (distance > CLOSE) {
    prop.advanced().turnOn(OUT2);
  } else {
    prop.advanced().release(OUT2);  // back to the shows (the test button)
  }
  delay(50);
}
