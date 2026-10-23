// A small polled HTTP/1.1 server with WebSocket support, independent of the network stack: the ESP32 and
// Pico W firmwares feed it WiFiClient connections, the host simulator POSIX sockets, so the device's HTTP
// and WebSocket handling is the same code everywhere.
//
// | Method | Path                       |                                                               |
// |--------|----------------------------|---------------------------------------------------------------|
// | GET    | /, /<file>                 | the phone web UI (gzipped, from flash)                        |
// | GET    | /api/v1/info, /api/v1/state| JSON                                                          |
// | POST   | /api/v1/rpc                | one request envelope in, one response out (JSON content type) |
// | POST   | /api/v1/update             | a firmware image, streamed to NetApp::updateWrite             |
// | GET    | /api/v1/ws                 | WebSocket: requests in, responses, state and events out       |
//
// CORS is allowed only for a Tauri web view (tauri://localhost, http(s)://tauri.localhost). Specter Studio talks to
// boards from its native core (no Origin header), so it needs no entry.
// Every WebSocket is a session (a number passed to NetApp::callFrom): pinged every 2 s, closed after 5 s of
// silence, and its end reported (NetApp::sessionClosed).
#pragma once
#include "../netapp.h"
#include "../serial_line.h"
#include "../script.h"
#include "../storage.h"
#include <deque>
#include <memory>

namespace pt {

// One TCP connection. read() and write() must not block for long.
class Conn {
 public:
  virtual ~Conn() {}
  // Bytes read (0 = nothing yet), or -1 when the peer closed.
  virtual int read(uint8_t* buf, size_t n) = 0;
  // Bytes accepted (may be fewer than n), or -1 on error.
  virtual int write(const uint8_t* buf, size_t n) = 0;
  virtual void close() = 0;
};

// MAX_REQUEST_BODY (serial_line.h): the largest legitimate request body, the same for serial.

struct HttpLimits {
  size_t max_header = 2048;
  size_t max_body = MAX_REQUEST_BODY;
  size_t small_body = 4096;          // a request or WebSocket message over this needs the one large slot
  size_t inbound_budget = 16 * 1024; // what all other clients may hold unread, together
  size_t max_queue = 16 * 1024;      // unsent bytes behind the one being sent: more closes the client
  Millis idle_ms = 30000;            // an idle keep-alive connection
  Millis stall_ms = 10000;           // output waiting with no progress (a phone gone out of range)
  Millis closing_ms = 2000;          // a closing connection whose last bytes do not go out
  Millis upload_idle_ms = 10000;     // a firmware upload that stops sending (update mode ends)
  Millis large_idle_ms = 3000;       // the large slot's holder that stops sending (others wait for the slot)
  Millis ws_poll_ms = 100;
  Millis ws_state_ms = 5000;      // send the state at least this often (keep-alive)
  Millis ws_state_min_ms = 200;   // and never more often than this
  size_t max_connections = 8;
  size_t max_ws = 4;              // WebSocket sessions (open pages); one more gets 503
  // The Pico W: lwIP has 5 TCP connections in all (MEMP_NUM_TCP_PCB), one for MQTT and one kept free so a new
  // connection can be accepted (and an idle one evicted) at all; its heap is the smallest.
  static HttpLimits picoW() {
    HttpLimits l;
    l.max_connections = 3;
    l.max_ws = 2;
    l.max_queue = 8 * 1024;
    l.inbound_budget = 8 * 1024;
    return l;
  }
};

// Parsed request head.
struct HttpHead {
  std::string method, path;
  size_t content_length = 0;
  bool json = false, close = false, upgrade_ws = false;
  bool has_ws_key = false, has_origin = false, has_host = false;
  std::string ws_key, origin, host;
  std::string if_none_match;  // the ETag of the copy the browser has
  bool has_length = false, octet_stream = false;
  bool has_sha256 = false, has_pin = false;  // X-Proptroller-Sha256, X-Proptroller-Pin (firmware updates)
  std::string sha256, pin;
};
[[nodiscard]] bool parseHead(const std::string& raw, HttpHead& out);

// WebSocket frames.
namespace ws {
enum Op : uint8_t { CONT = 0, TEXT = 1, BINARY = 2, CLOSE = 8, PING = 9, PONG = 10 };
// The frame at buf: 0 = its header is incomplete, -1 = invalid, otherwise its whole size (may be more than n):
// head bytes of header (the mask, if any, is the last 4 of them), then len bytes of payload.
long frame(const uint8_t* buf, size_t n, size_t max, bool require_mask, uint8_t& opcode, bool& fin, size_t& head, size_t& len);
// 0 = incomplete, -1 = invalid, otherwise bytes used (opcode / payload filled in, unmasked).
long decode(const uint8_t* buf, size_t n, size_t max, bool require_mask, uint8_t& opcode, bool& fin, std::string& payload);
std::string encode(uint8_t opcode, const std::string& payload);
std::string acceptKey(const std::string& key);
}  // namespace ws

// MQTT: `<p>/state` and `<p>/event` payloads get the boot they come from and a sequence number (one counter for
// both, raised by each stamped message), so a fleet backend can tell a restart from lost messages:
// {"boot_id":"1a2b3c4d","mqtt_seq":7,"v":1,...} (an event keeps its own "seq", the WebSocket's). Every other
// topic's payload is returned as it is.
std::string mqttStamp(const std::string& topic, const std::string& payload, const std::string& boot_id, uint32_t& seq);

// SHA-1 (the WebSocket accept key; also handy for salted password hashes).
void sha1(const uint8_t* data, size_t len, uint8_t out[20]);

class HttpServer {
 public:
  explicit HttpServer(NetApp& app, const HttpLimits& l = HttpLimits()) : app_(app), limits_(l) {}
  // Take ownership of a new connection (closed when the server is full).
  void add(Conn* c, Millis now);
  // Serve every connection a little. Call often (every few ms).
  void poll(Millis now);
  size_t connections() const { return conns_.size(); }
  // Whether add() would take a connection now (else leave it in the stack's backlog until a slot frees).
  bool canAdd() const;
  // What the clients' input buffers hold allocated, together (tests: a large body's memory is given back).
  size_t inputCapacity() const;
  uint32_t requests = 0;

 private:
  struct Segment {
    std::shared_ptr<const std::string> data;  // shared: one state frame for every client
    const uint8_t* ptr = nullptr;              // or bytes in flash (the web page)
    size_t len = 0, off = 0;
    bool state = false;                        // a state frame: a newer one replaces it while unsent
  };
  struct Client {
    std::unique_ptr<Conn> conn;
    std::string in;
    std::deque<Segment> out;
    Millis last_io = 0, last_tx = 0;  // last byte in; last progress out
    uint32_t served = 0;              // requests answered
    bool closing = false;  // close after the output drains
    bool ws = false;
    uint32_t seq = 0;
    uint64_t state_hash = 0;
    Millis last_state_at = 0;
    bool sent_state = false;
    uint32_t session = 0;
    Millis last_ping = 0;
    // POST /api/v1/update: the body streams to the firmware, never held whole
    bool upload = false;
    size_t upload_left = 0;
    bool upload_refused = false;  // the answer is known; the rest of the body is read and dropped
    std::string upload_answer;
    const char* upload_cors = nullptr;
  };
  struct SharedState {
    std::shared_ptr<const std::string> frame;
    uint64_t hash = 0;
  };
  static bool evictable(const Client& c);
  static size_t queuedBehind(const Client& c);
  void drop(size_t i);
  void endLarge(Client& c);
  void serveUpload(Client& c);
  void serveHttp(Client& c, Millis now);
  void serveWs(Client& c, Millis now);
  const SharedState& sharedState();
  void send(Client& c, std::string s);
  void sendStatic(Client& c, const uint8_t* p, size_t n);
  void sendState(Client& c, const std::shared_ptr<const std::string>& frame);
  void respond(Client& c, const char* status, const char* type, const std::string& body, bool close, const char* cors,
               const char* encoding = nullptr, const uint8_t* static_body = nullptr, size_t static_len = 0, bool head_only = false, const char* etag = nullptr);
  bool flush(Client& c);
  NetApp& app_;
  HttpLimits limits_;
  std::vector<std::unique_ptr<Client>> conns_;
  uint32_t next_session_ = 1;
  Client* big_ = nullptr;  // the one client receiving a large body
  Millis now_ = 0, last_push_ = 0;
  bool push_ = false;      // this poll pushes events and state to the WebSocket clients
  SharedState state_;
  uint8_t rx_[1024]{};  // what poll() reads into (a member, not 1 KB on the loop's stack)
};

}  // namespace pt
