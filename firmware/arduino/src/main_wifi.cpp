// The generic Proptroller firmware for the ESP32 DevKit, Pico W and Pico 2 W: every show is made on the
// phone (or the desktop app). Join the "Proptroller-XXXX" Wi-Fi (password "password", a simple default:
// change it under Settings -> Wi-Fi) and open
// http://192.168.4.1/ to set it up.
#include <Proptroller.h>

Proptroller prop;

void setup() {
  prop.begin();
}

void loop() {
  prop.update();
}
