#include "wifiscan.h"
#include <algorithm>

namespace pt {

std::vector<WifiNetwork> tidyNetworks(const std::vector<WifiNetwork>& raw) {
  std::vector<WifiNetwork> v;
  for (auto& n : raw) {
    if (n.ssid.empty() || n.ssid.size() > 32) continue;
    bool plain = true;
    for (unsigned char c : n.ssid) plain &= c >= 0x20 && c != 0x7F;
    if (!plain) continue;
    auto same = std::find_if(v.begin(), v.end(), [&](const WifiNetwork& x) { return x.ssid == n.ssid; });
    if (same == v.end()) v.push_back(n);
    else if (n.rssi > same->rssi) *same = n;
  }
  // strongest first, then by name: an insertion sort (a few dozen entries; std::sort costs the ESP32 example about
  // 800 B of flash it does not have to spare)
  for (size_t i = 1; i < v.size(); i++)
    for (size_t j = i; j > 0; j--) {
      const WifiNetwork &a = v[j - 1], &b = v[j];
      if (a.rssi > b.rssi || (a.rssi == b.rssi && !(b.ssid < a.ssid))) break;
      std::swap(v[j - 1], v[j]);
    }
  if (v.size() > WIFI_SCAN_MAX_NETWORKS) v.resize(WIFI_SCAN_MAX_NETWORKS);
  return v;
}

void WifiScan::setAvailable(bool scan_now, Millis now) {
  available_ = true;
  if (scan_now) (void)ask(now, true);
}

void WifiScan::expire(Millis now) {
  if (running_ && now - started_at_ >= WIFI_SCAN_TIMEOUT_MS) {
    running_ = false;
    failed_ = true;
  }
}

Millis WifiScan::ask(Millis now, bool rescan) {
  expire(now);
  if (!available_ || wanted_ || running_) return 0;
  bool stale = !have_ || failed_ || now - done_at_ >= WIFI_SCAN_FRESH_MS;
  if (!rescan && !stale) return 0;
  if (started_ever_ && now - started_at_ < WIFI_SCAN_MIN_GAP_MS) return rescan ? WIFI_SCAN_MIN_GAP_MS - (now - started_at_) : 0;
  wanted_ = true;
  return 0;
}

bool WifiScan::takeStart(Millis now) {
  expire(now);
  if (!wanted_ || running_) return false;
  wanted_ = false;
  running_ = true;
  started_ever_ = true;
  started_at_ = now;
  return true;
}

void WifiScan::finish(Millis now, const std::vector<WifiNetwork>& raw, bool ok) {
  running_ = false;
  if (!ok) {
    failed_ = true;
    return;
  }
  nets_ = tidyNetworks(raw);
  have_ = true;
  failed_ = false;
  done_at_ = now;
}

bool WifiScan::scanning(Millis now) {
  expire(now);
  return wanted_ || running_;
}

void WifiScan::toJson(JsonObject o, Millis now, Millis retry_ms) {
  o["scanning"] = scanning(now);
  if (have_) o["age_ms"] = now - done_at_;
  if (retry_ms) o["retry_ms"] = retry_ms;
  if (failed_)
    o["error"] = "the board could not look for networks just now (it may be busy joining one): try Rescan in a moment";
  JsonArray a = o["networks"].to<JsonArray>();
  for (auto& n : nets_) {
    JsonObject x = a.add<JsonObject>();
    x["ssid"] = n.ssid;
    x["rssi"] = n.rssi;
    x["secured"] = n.secured;
  }
}

}  // namespace pt
