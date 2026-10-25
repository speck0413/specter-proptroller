#if !defined(ARDUINO) && (defined(__unix__) || defined(__APPLE__) || defined(_WIN32))
#include "posix.h"
#include <errno.h>
#include <random>
// MQTT in the sim env (platformio.ini [sim_mqtt]); the host tests have none, nor has Windows (espMqttClient's host
// client is POSIX only: env sim_win)
#if __has_include(<espMqttClient.h>) && !defined(_WIN32)
#include <espMqttClient.h>
#include "mqtt.h"
#define PT_POSIX_MQTT 1
#endif

#ifdef _WIN32
// Winsock: the same calls, with closesocket, ioctlsocket(FIONBIO) and WSAGetLastError; no SIGPIPE on Windows
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
static int sockClose(SOCKET fd) { return ::closesocket(fd); }
#define PT_SOCKOPT(p) reinterpret_cast<const char*>(p)
#define PT_RECVBUF(p) reinterpret_cast<char*>(p)
#define PT_SENDBUF(p) reinterpret_cast<const char*>(p)
#else
// EAGAIN and EWOULDBLOCK are the same number on Linux, not everywhere
static bool wouldBlock() {
#if EAGAIN == EWOULDBLOCK
  return errno == EAGAIN;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
static int sockClose(int fd) { return ::close(fd); }
#define PT_SOCKOPT(p) (p)
#define PT_RECVBUF(p) (p)
#define PT_SENDBUF(p) (p)
#endif

namespace pt {

static uint32_t linkRandom() {
  static std::mt19937 g{std::random_device{}()};
  return (uint32_t)g();
}

// The board link's way out on a computer: the discovery socket, to every peer given, else a broadcast on the port.
struct PosixNet::Radio : LinkRadio {
  PosixNet& n;
  std::vector<sockaddr_in> peers;
  explicit Radio(PosixNet& net) : n(net) {}
  void sendTo(const std::string& f, const sockaddr_in& a) {
    if (n.udp_fd_ >= 0) (void)sendto(n.udp_fd_, PT_SENDBUF(f.data()), (int)f.size(), 0, (const sockaddr*)&a, sizeof a);
  }
  void broadcast(const std::string& f) override {
    if (!peers.empty()) {
      for (auto& a : peers) sendTo(f, a);
      return;
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    a.sin_port = htons(n.udp_port_);
    sendTo(f, a);
  }
  void reply(const std::string& f, const LinkAddr& to) override {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = to.ip;
    a.sin_port = htons(to.port);
    sendTo(f, a);
  }
};

void PosixNet::setLinkPeers(const std::vector<std::pair<std::string, uint16_t>>& peers) {
  radio_->peers.clear();
  for (auto& p : peers) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(p.second);
    if (inet_pton(AF_INET, p.first.c_str(), &a.sin_addr) == 1) radio_->peers.push_back(a);
  }
}

class PosixConn : public Conn {
 public:
  explicit PosixConn(int fd) : fd_(fd) {}
  ~PosixConn() override { close(); }
  int read(uint8_t* buf, size_t n) override {
    ssize_t r = ::recv(fd_, PT_RECVBUF(buf), (int)n, 0);
    if (r > 0) return (int)r;
    if (r == 0) return -1;
    return wouldBlock() ? 0 : -1;
  }
  int write(const uint8_t* buf, size_t n) override {
    ssize_t r = ::send(fd_, PT_SENDBUF(buf), (int)n, MSG_NOSIGNAL);
    if (r >= 0) return (int)r;
    return wouldBlock() ? 0 : -1;
  }
  void close() override {
    if (fd_ >= 0) { sockClose(fd_); fd_ = -1; }
  }

 private:
  int fd_;
};

#ifdef _WIN32
static void nonblocking(int fd) {
  u_long on = 1;
  ioctlsocket((SOCKET)fd, FIONBIO, &on);
}
#else
static void nonblocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
#endif

#if PT_POSIX_MQTT
static uint32_t hostRandom() {
  static std::mt19937 g{std::random_device{}()};
  return (uint32_t)g();
}

// espMqttClient's POSIX transport, made to behave like the boards' sockets: a 1 s connect, sends that never
// block, and a peer that closed or reset is noticed (the library's own would wait for the keep-alive).
class SimMqttTransport : public espMqttClientInternals::ClientPosix {
 public:
  bool connect(IPAddress ip, uint16_t port) override {
    stop();
    _sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_sockfd < 0) return false;
    int one = 1;
    setsockopt(_sockfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fcntl(_sockfd, F_SETFL, fcntl(_sockfd, F_GETFL, 0) | O_NONBLOCK);
    memset(&_host, 0, sizeof _host);
    _host.sin_family = AF_INET;
    _host.sin_addr.s_addr = htonl(static_cast<uint32_t>(ip));
    _host.sin_port = htons(port);
    if (::connect(_sockfd, (sockaddr*)&_host, sizeof _host) == 0) return true;
    pollfd p{_sockfd, POLLOUT, 0};
    int err = 0;
    socklen_t len = sizeof err;
    if (errno != EINPROGRESS || ::poll(&p, 1, 1000) != 1 || getsockopt(_sockfd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err) {
      stop();
      return false;
    }
    return true;
  }
  bool connect(const char* host, uint16_t port) override {
    IPAddress ip = _hostToIP(host);
    return ip != IPAddress(0) && connect(ip, port);
  }
  size_t write(const uint8_t* buf, size_t n) override {
    ssize_t r = ::send(_sockfd, buf, n, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0 && !wouldBlock()) stop();
    return r > 0 ? (size_t)r : 0;
  }
  int read(uint8_t* buf, size_t n) override {
    ssize_t r = ::recv(_sockfd, buf, n, MSG_DONTWAIT);
    if (r == 0 || (r < 0 && !wouldBlock())) stop();
    return r > 0 ? (int)r : 0;
  }
};

class SimMqttClient : public MqttClientSetup<SimMqttClient> {
 public:
  SimMqttClient() : MqttClientSetup(espMqttClientTypes::UseInternalTask::NO) { _transport = &t_; }

 private:
  SimMqttTransport t_;
};

struct PosixMqtt {
  MqttSession<SimMqttClient> session;
  std::atomic<bool> stop{false};
  std::thread loop;  // espMqttClient's connects and sends (they may wait) happen here, never in poll()
  explicit PosixMqtt(NetApp& app) : session(app, hostRandom) {
    session.client.onMessage([this](const espMqttClientTypes::MessageProperties&, const char* t, const uint8_t* p, size_t n, size_t i,
                                    size_t total) { session.piece(t, p, n, i, total); });
    loop = std::thread([this] {
      while (!stop) {
        session.client.loop();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    });
  }
  ~PosixMqtt() {
    stop = true;
    loop.join();
  }
};

PosixNet::PosixNet(NetApp& app)
    : app_(app), http_(app), mqtt_(new PosixMqtt(app)), link_(new LinkPeer(linkRandom)), radio_(new Radio(*this)) {}
#else
struct PosixMqtt {};
PosixNet::PosixNet(NetApp& app) : app_(app), http_(app), link_(new LinkPeer(linkRandom)), radio_(new Radio(*this)) {}
#endif

PosixNet::~PosixNet() {
  if (listen_fd_ >= 0) sockClose(listen_fd_);
  if (udp_fd_ >= 0) sockClose(udp_fd_);
}

bool PosixNet::start(uint16_t http_port, uint16_t udp_port) {
#ifdef _WIN32
  static bool wsa = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
  if (!wsa) return false;
#else
  (void)signal(SIGPIPE, SIG_IGN);
#endif
  listen_fd_ = (int)socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
#ifdef _WIN32
  // on Windows SO_REUSEADDR would let a second program bind a port already listened on (a practice board's
  // fixed port taken twice); exclusive use instead, and a closed listener's port is free at once there anyway
  setsockopt(listen_fd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, PT_SOCKOPT(&one), sizeof one);
#else
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, PT_SOCKOPT(&one), sizeof one);
#endif
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(http_port);
  if (bind(listen_fd_, (sockaddr*)&a, sizeof a) < 0 || listen(listen_fd_, 16) < 0) return false;
  socklen_t len = sizeof a;
  getsockname(listen_fd_, (sockaddr*)&a, &len);
  http_port_ = ntohs(a.sin_port);
  nonblocking(listen_fd_);
  if (udp_port) {
    udp_fd_ = (int)socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, PT_SOCKOPT(&one), sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    sockaddr_in u{};
    u.sin_family = AF_INET;
    u.sin_addr.s_addr = htonl(INADDR_ANY);
    u.sin_port = htons(udp_port);
    if (bind(udp_fd_, (sockaddr*)&u, sizeof u) < 0) return false;
    setsockopt(udp_fd_, SOL_SOCKET, SO_BROADCAST, PT_SOCKOPT(&one), sizeof one);  // the board link (link.h)
    nonblocking(udp_fd_);
    udp_port_ = udp_port;
  }
  return true;
}

void PosixNet::poll(Millis now) {
  while (http_.canAdd()) {  // when full, a new connection waits in the listen backlog
#ifdef _WIN32
    SOCKET s = accept((SOCKET)listen_fd_, nullptr, nullptr);
    if (s == INVALID_SOCKET) break;
    int fd = (int)s;
#else
    int fd = accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) break;
#endif
    nonblocking(fd);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, PT_SOCKOPT(&one), sizeof one);
    http_.add(new PosixConn(fd), now);
  }
  http_.poll(now);
#if PT_POSIX_MQTT
  mqtt_->session.poll(now, true);
#endif
  if (udp_fd_ >= 0) {
    char buf[512];
    sockaddr_in from{};
    socklen_t fl = sizeof from;
    ssize_t n = 0;
    while ((n = recvfrom(udp_fd_, buf, (int)sizeof buf, 0, (sockaddr*)&from, &fl)) > 0) {
      if (isDiscover(buf, (size_t)n)) {
        std::string a = app_.announceJson(http_port_);
        sendto(udp_fd_, a.data(), (int)a.size(), 0, (sockaddr*)&from, fl);
      } else {
        LinkAddr la;
        la.ip = from.sin_addr.s_addr;
        la.port = ntohs(from.sin_port);
        link_->received((const uint8_t*)buf, (size_t)n, la, now, app_, *radio_);
      }
      fl = sizeof from;
    }
    link_->poll(now, app_, *radio_);
  }
}

}  // namespace pt
#endif
