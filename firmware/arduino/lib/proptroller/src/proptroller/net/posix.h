// The network layer on a host computer (the `sim` environment; on Windows `sim_win`, through Winsock and without
// MQTT): the HTTP / WebSocket server on a TCP port, UDP discovery and MQTT, so the phone UI, the desktop app, the
// protocol client and a broker can talk to a simulated board. MQTT is the ESP32's session (mqtt.h) on
// espMqttClient's POSIX client, which a thread of its own drives (as AsyncTCP's task does on the ESP32): set
// mqtt_broker / mqtt_port (or start the simulator with --mqtt).
#pragma once
#if !defined(ARDUINO) && (defined(__unix__) || defined(__APPLE__) || defined(_WIN32))
#include "http.h"
#include "../link.h"
#include <atomic>
#include <memory>
#include <thread>

namespace pt {

struct PosixMqtt;

class PosixNet {
 public:
  explicit PosixNet(NetApp& app);
  ~PosixNet();
  // http_port 0 = any free port; udp_port 0 = no discovery. Returns false if a socket cannot be opened.
  bool start(uint16_t http_port, uint16_t udp_port);
  void poll(Millis now);
  uint16_t httpPort() const { return http_port_; }
  HttpServer& http() { return http_; }
  // The board link's peer-to-peer frames (link.h) go to these addresses (host, UDP port) instead of a broadcast:
  // simulated boards on one computer each have their own port (main_sim --link-peers).
  void setLinkPeers(const std::vector<std::pair<std::string, uint16_t>>& peers);
  const LinkPeer& link() const { return *link_; }

 private:
  struct Radio;
  friend struct Radio;
  NetApp& app_;
  HttpServer http_;
  std::unique_ptr<PosixMqtt> mqtt_;
  int listen_fd_ = -1, udp_fd_ = -1;
  uint16_t http_port_ = 0, udp_port_ = 0;
  std::unique_ptr<LinkPeer> link_;
  std::unique_ptr<Radio> radio_;
};

}  // namespace pt
#endif
