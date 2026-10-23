// The firmware behind the controller lock, for the network code: every call into the firmware takes the lock,
// and nothing else does, so sockets, DNS and the broker are always waited on without it and the show step
// (its own task on the ESP32, core 1 on the RP2040) never waits on the network. G is a scoped guard for L
// (Guard on the boards, std::lock_guard<std::mutex> in the host tests).
#pragma once
#include "../netapp.h"

namespace pt {

template <class L, class G>
class LockedApp : public NetApp {
 public:
  LockedApp(NetApp& a, L& l) : a_(a), l_(l) {}
  std::string call(const char* b, size_t n) override { G g(l_); return a_.call(b, n); }
  std::string callFrom(uint32_t ss, const char* b, size_t n) override { G g(l_); return a_.callFrom(ss, b, n); }
  void sessionClosed(uint32_t ss) override { G g(l_); a_.sessionClosed(ss); }
  uint32_t wsHeartbeatMs() override { return a_.wsHeartbeatMs(); }
  std::string infoJson() override { G g(l_); return a_.infoJson(); }
  std::string stateJson() override { G g(l_); return a_.stateJson(); }
  uint32_t eventsSince(uint32_t s, std::vector<std::string>& o) override { G g(l_); return a_.eventsSince(s, o); }
  const WebAsset* asset(const char* p) override { return a_.asset(p); }
  std::string announceJson(uint16_t port) override { G g(l_); return a_.announceJson(port); }
  std::vector<std::pair<std::string, std::string>> txt() override { G g(l_); return a_.txt(); }
  void netChanged(const NetState& n) override { G g(l_); a_.netChanged(n); }
  NetConfig netConfig() override { G g(l_); return a_.netConfig(); }
  std::vector<std::string> takeCuesOut() override { G g(l_); return a_.takeCuesOut(); }
  void cueIn(const std::string& c) override { G g(l_); a_.cueIn(c); }
  void wifiScanReady(bool now) override { G g(l_); a_.wifiScanReady(now); }
  bool wifiScanWanted() override { G g(l_); return a_.wifiScanWanted(); }
  void wifiScanDone(const std::vector<WifiNetwork>& f, bool ok) override { G g(l_); a_.wifiScanDone(f, ok); }
  bool mqttTarget(std::string& h, uint16_t& p) override { G g(l_); return a_.mqttTarget(h, p); }
  uint32_t mqttEpoch() override { G g(l_); return a_.mqttEpoch(); }
  std::string mqttClientId() override { G g(l_); return a_.mqttClientId(); }
  MqttMsg mqttWill() override { G g(l_); return a_.mqttWill(); }
  std::vector<MqttMsg> mqttOnConnect() override { G g(l_); return a_.mqttOnConnect(); }
  std::vector<std::string> mqttSubscriptions() override { G g(l_); return a_.mqttSubscriptions(); }
  std::vector<MqttMsg> mqttOnMessage(const std::string& t, const std::string& p) override { G g(l_); return a_.mqttOnMessage(t, p); }
  std::vector<MqttMsg> mqttPoll() override { G g(l_); return a_.mqttPoll(); }
  // firmware updates: each chunk is written under the lock (the step waits; outputs are off in update mode)
  bool updateBegin(const UpdateRequest& q, std::string& a) override { G g(l_); return a_.updateBegin(q, a); }
  bool updateWrite(const uint8_t* d, size_t n, std::string& a) override { G g(l_); return a_.updateWrite(d, n, a); }
  std::string updateFinish() override { G g(l_); return a_.updateFinish(); }
  void updateAbort() override { G g(l_); a_.updateAbort(); }

 private:
  NetApp& a_;
  L& l_;
};

}  // namespace pt
