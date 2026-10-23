// 07 MQTT (home automation, show control systems)
//
// What it does: connects to an MQTT broker (set it on the phone: Settings -> MQTT broker). The board then
// publishes its state on proptroller/<name>/state, takes requests on proptroller/<name>/rpc and settings
// on proptroller/<name>/config/set/<key> (see the protocol). This sketch adds two topics of its own:
//  * "lab/lights" in: "on" or "off" switches OUT1;
//  * "lab/button" out: "pressed" every time USER1 is pressed.
//
// Wiring: a 12 V lamp on OUT1 (power the board from 12 V). The board must be on your Wi-Fi network (example 06).
// Try it: mosquitto_sub -t 'lab/#' -v   and   mosquitto_pub -t lab/lights -m on

#include <ProptrollerAdvanced.h>

Proptroller prop;

void lightsMessage(const char* topic, const char* payload) {
  if (strcmp(payload, "on") == 0) {
    prop.advanced().turnOn(OUT1);
  } else {
    prop.advanced().turnOff(OUT1);
  }
}

void setup() {
  prop.begin();
  prop.advanced().onMessage("lab/lights", lightsMessage);
}

void loop() {
  prop.update();
  if (prop.advanced().wasPressed(USER1)) {
    prop.advanced().publish("lab/button", "pressed");
  }
}
