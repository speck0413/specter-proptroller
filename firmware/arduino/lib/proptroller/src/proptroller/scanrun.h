// One Wi-Fi scan as the network layer runs it (arduino/net.cpp): when to start it, when it has ended, and when
// to give up on it. Kept apart from the radio so the host tests can check it with a fake one.
//
// The radio decides when a scan has ended (finished()); nothing here reads a library's own time limit. On the
// ESP32, arduino-esp32 2.0.17's scanComplete() reports a scan as failed max_ms_per_chan * 20 ms after it started
// (2.4 s at 120 ms per channel) although the radio is still scanning; with the hotspot up the radio goes back to
// the hotspot's channel between channels and a scan takes longer than that, so every scan "failed" with nothing
// found. The only time limit is WIFI_SCAN_TIMEOUT_MS here, and a scan that reaches it is stopped (abort()) so the
// next one can start.
#pragma once
#include <vector>
#include "types.h"
#include "wifiscan.h"

namespace pt {

class ScanRadio {
 public:
  virtual ~ScanRadio() {}
  virtual bool start() = 0;                                // false: the radio cannot scan now
  virtual bool finished() = 0;                             // the scan started last has ended (found or failed)
  virtual bool collect(std::vector<WifiNetwork>& out) = 0;  // after finished(): what it found (false: it failed)
  virtual void abort() = 0;                                // stop a scan that took too long, drop what it found
};

// A join started this recently may still be under way (the ESP32 refuses to scan while it joins: ESP_ERR_WIFI_STATE).
static const Millis WIFI_JOIN_BUSY_MS = 8000;

// No scan starts while a join may be under way: the board wants a network, is not on it, and asked to join it less
// than WIFI_JOIN_BUSY_MS ago (joined_at 0: never). A join stamped after now (the loop read the clock first) is busy.
inline bool scanJoinBusy(Millis now, bool sta_wanted, bool connected, Millis joined_at) {
  if (!sta_wanted || connected || joined_at == 0) return false;
  return now < joined_at || now - joined_at < WIFI_JOIN_BUSY_MS;
}

class ScanRun {
 public:
  enum class End : uint8_t {
    None,      // no scan ended in this call
    Found,     // the scan ended with a list (found() networks, in tookMs())
    Failed,    // the radio said the scan failed
    Refused,   // the radio could not start one
    TimedOut,  // no end after WIFI_SCAN_TIMEOUT_MS: stopped
  };
  // Called from the network loop. join_busy: a join is under way (no scan starts then). App gives
  // wifiScanWanted() (true = start one now) and wifiScanDone(found, ok) (the controller, behind its lock).
  template <class App>
  End poll(Millis now, bool join_busy, ScanRadio& radio, App& app) {
    if (!running_) {
      if (join_busy || !app.wifiScanWanted()) return End::None;
      if (!radio.start()) {
        app.wifiScanDone({}, false);
        return End::Refused;
      }
      running_ = true;
      started_at_ = now;
      return End::None;
    }
    took_ms_ = now - started_at_;
    if (!radio.finished()) {
      if (took_ms_ < WIFI_SCAN_TIMEOUT_MS) return End::None;
      radio.abort();
      running_ = false;
      app.wifiScanDone({}, false);
      return End::TimedOut;
    }
    std::vector<WifiNetwork> found;
    bool ok = radio.collect(found);
    running_ = false;
    found_ = found.size();
    app.wifiScanDone(found, ok);
    return ok ? End::Found : End::Failed;
  }
  bool running() const { return running_; }
  Millis tookMs() const { return took_ms_; }  // how long the last scan ran (for the log)
  size_t found() const { return found_; }     // how many access points the last scan found (before tidying)

 private:
  bool running_ = false;
  Millis started_at_ = 0, took_ms_ = 0;
  size_t found_ = 0;
};

}  // namespace pt
