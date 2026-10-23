// When the board (re)joins its Wi-Fi network, and which tries count as failed (arduino/net.cpp). Kept apart from
// the radio so the host tests can check it: a network that drops (the router restarts, the board itself restarts
// and takes its Wi-Fi down) is joined again at once and is not a failed join, so the log never says "check the name
// and password" when they are fine.
#pragma once
#include "types.h"

namespace pt {

static const Millis WIFI_RETRY_MS = 10000, WIFI_RETRY_SLOW_MS = 30000;
static const Millis WIFI_RETRY_AP_MS = 60000;  // while the hotspot is up (always, on a board with a radio): a
                                               // station retry scans and moves the radio's channel, which
                                               // drops a phone on the hotspot
static const int WIFI_SLOW_AFTER_FAILURES = 5;  // retries slow down after this many failed joins in a row

class WifiJoin {
 public:
  enum class Step : uint8_t {
    Wait,    // nothing to do now
    Join,    // the first join after power-on
    Rejoin,  // the network dropped: join again (not a failure)
    Retry,   // the last join failed (failures() counts it): join again
  };
  // Called from the network loop. ap_on: the hotspot is up (slower retries). stopping: the board is
  // restarting (nothing is joined or counted any more). busy: a Wi-Fi scan is running (the join waits for it).
  Step poll(Millis now, bool connected, bool ap_on, bool stopping, bool busy) {
    if (stopping) return Step::Wait;
    if (connected) {
      failures_ = 0;
      was_connected_ = true;
      return Step::Wait;
    }
    if (busy) return Step::Wait;
    Step s;
    if (was_connected_) {
      was_connected_ = false;
      s = Step::Rejoin;
    } else {
      if (tried_ && now < next_try_) return Step::Wait;
      s = tried_ ? Step::Retry : Step::Join;
      if (tried_) failures_++;
    }
    tried_ = true;
    next_try_ = now + (ap_on ? WIFI_RETRY_AP_MS : failures_ < WIFI_SLOW_AFTER_FAILURES ? WIFI_RETRY_MS : WIFI_RETRY_SLOW_MS);
    return s;
  }
  int failures() const { return failures_; }

 private:
  bool tried_ = false, was_connected_ = false;
  int failures_ = 0;
  Millis next_try_ = 0;
};

}  // namespace pt
