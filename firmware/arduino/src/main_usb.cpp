// The generic Proptroller firmware for the Raspberry Pi Pico (no Wi-Fi): shows come from flash and the
// protocol runs as JSON lines over the USB serial port (one request per line in; answers, events and the
// state out).
#include <Proptroller.h>

Proptroller prop;

void setup() {
  prop.begin();
}

void loop() {
  prop.update();
}
