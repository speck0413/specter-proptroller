// The Wi-Fi networks a board can see, for the phone page's network picker (PROTOCOL.md "wifi_scan").
//
// The controller keeps the last list and decides when a new scan is due; the network layer (arduino/net.cpp, or
// the simulator) does the scanning on its own core and hands the list back. A scan never runs in the step: the
// controller only flips flags under its lock, and the radio is asked from the network loop.
#pragma once
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include "types.h"

namespace pt {

struct WifiNetwork {
  std::string ssid;
  int32_t rssi = 0;      // dBm (-30 strong ... -90 weak)
  bool secured = true;   // false: an open network (no password)
  WifiNetwork() {}
  WifiNetwork(const std::string& s, int32_t r, bool sec) : ssid(s), rssi(r), secured(sec) {}
};

static const size_t WIFI_SCAN_MAX_NETWORKS = 20;     // the longest list a page is given
static const Millis WIFI_SCAN_MIN_GAP_MS = 10000;    // scans never start closer together than this (all pages together)
static const Millis WIFI_SCAN_FRESH_MS = 30000;      // a plain wifi_scan reuses a list younger than this
static const Millis WIFI_SCAN_TIMEOUT_MS = 15000;    // a scan that has not reported by then counts as failed

// One entry per network name (the strongest access point of that name), hidden networks (no name) and names
// with control characters left out, strongest first (then by name), at most WIFI_SCAN_MAX_NETWORKS.
std::vector<WifiNetwork> tidyNetworks(const std::vector<WifiNetwork>& raw);

class WifiScan {
 public:
  // The network layer can scan (a Wi-Fi board); scan_now asks for a first list at once (the setup hotspot just
  // opened, so the list is ready before the phone gets there).
  void setAvailable(bool scan_now, Millis now);
  bool available() const { return available_; }
  // A page asks (op wifi_scan): a new scan is asked for when the list is missing, stale or failed, or on rescan;
  // never sooner than WIFI_SCAN_MIN_GAP_MS after the last one started. Returns how long until a refused rescan
  // could start (0 = none refused).
  Millis ask(Millis now, bool rescan);
  // The network layer: true = start a scan now (it is then running until finish()).
  bool takeStart(Millis now);
  void finish(Millis now, const std::vector<WifiNetwork>& raw, bool ok);
  bool scanning(Millis now);
  void toJson(JsonObject o, Millis now, Millis retry_ms);
  const std::vector<WifiNetwork>& networks() const { return nets_; }

 private:
  void expire(Millis now);
  bool available_ = false, wanted_ = false, running_ = false, have_ = false, failed_ = false, started_ever_ = false;
  Millis started_at_ = 0, done_at_ = 0;
  std::vector<WifiNetwork> nets_;
};

}  // namespace pt
