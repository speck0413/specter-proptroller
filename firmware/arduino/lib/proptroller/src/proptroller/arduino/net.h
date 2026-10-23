// Wi-Fi networking on the ESP32 and the Pico W / Pico 2 W:
//
// * With no network configured the board opens the setup access point Proptroller-XXXX (WPA2, the ap_pass
//   setting, default "password") at http://192.168.4.1/.
// * With a network configured it joins it (DHCP, mDNS name.local); after three failed attempts it also
//   opens the access point and keeps retrying the network in the background.
// * HTTP / WebSocket server on port 80, UDP discovery on 47800, mDNS _proptroller._tcp, MQTT (PubSubClient),
//   and on the ESP32 named cues between controllers over ESP-NOW ("PTCUE1:<name>").
//
// Every call into the firmware goes through the NetApp and holds the controller lock.
#pragma once
#if defined(ARDUINO) && (defined(ESP32) || defined(PICO_CYW43_SUPPORTED))
#include "../net/http.h"
#include "hal.h"

namespace pt {

class ArduinoNet {
 public:
  ArduinoNet(NetApp& app, Lock& lock);
  ~ArduinoNet();
  void begin();
  // Call from loop(); never waits for long (arduino/net.cpp: what may wait, and how long).
  void poll();
  // The board is about to restart: its Wi-Fi going down from here on is not a failed join (nothing is joined or
  // logged any more). Safe from the step task.
  void stop();
  bool mqttConnected() const;
  // Publish on the MQTT connection, if any (advanced: your own topics).
  bool publish(const std::string& topic, const std::string& payload, bool retain = false);

  struct Impl;

 private:
  Impl* p_;
};

}  // namespace pt
#endif
