// HTTP / WebSocket server tests over an in-memory connection (the cases of proptroller-http's tests).
#include <unity.h>
#include <list>
#include "proptroller/net/http.h"
#include "proptroller/sim/sim.h"
#include "proptroller/password.h"

using namespace pt;
void setUp() {}
void tearDown() {}

// The test keeps the pipe; the server owns (and deletes) the connection.
struct Pipe {
  std::string in, out;
  bool closed = false, peer_closed = false;
  size_t window = 1 << 20;  // bytes accepted per write
};
struct MemConn : Conn {
  Pipe& p;
  explicit MemConn(Pipe& pipe) : p(pipe) {}
  std::string& in = p.in;
  std::string& out = p.out;
  bool& closed = p.closed;
  bool& peer_closed = p.peer_closed;
  size_t& window = p.window;
  int read(uint8_t* b, size_t n) override {
    if (in.empty()) return peer_closed ? -1 : 0;
    size_t k = in.size() < n ? in.size() : n;
    memcpy(b, in.data(), k);
    in.erase(0, k);
    return (int)k;
  }
  int write(const uint8_t* b, size_t n) override {
    size_t k = n < window ? n : window;
    out.append((const char*)b, k);
    return (int)k;
  }
  void close() override { closed = true; }
};

static std::string body(const std::string& resp) { return resp.substr(resp.find("\r\n\r\n") + 4); }

void head_parsing() {
  HttpHead h;
  TEST_ASSERT_TRUE(parseHead("POST /api/v1/rpc?x=1 HTTP/1.1\r\nContent-Length: 12\r\nContent-Type: application/json; charset=utf-8\r\nOrigin: tauri://localhost\r\nConnection: close", h));
  TEST_ASSERT_EQUAL_STRING("/api/v1/rpc", h.path.c_str());
  TEST_ASSERT_EQUAL(12, h.content_length);
  TEST_ASSERT_TRUE(h.json && h.close && h.has_origin);
  HttpHead g;
  TEST_ASSERT_FALSE(parseHead("GARBAGE", g));
  TEST_ASSERT_FALSE(parseHead("GET nopath HTTP/1.1", g));
  HttpHead o;
  TEST_ASSERT_TRUE(parseHead("GET / HTTP/1.0", o));
  TEST_ASSERT_TRUE(o.close);
}

void websocket_accept_key_rfc6455() {
  TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", ws::acceptKey("dGhlIHNhbXBsZSBub25jZQ==").c_str());
}

void frames_round_trip() {
  std::string p(300, 'x');
  std::string f = ws::encode(ws::TEXT, p);
  // a client frame is masked
  std::string m;
  m += (char)0x81;
  m += (char)(0x80 | 5);
  const char mask[4] = {1, 2, 3, 4};
  m.append(mask, 4);
  const char* hello = "hello";
  for (int i = 0; i < 5; i++) m += (char)(hello[i] ^ mask[i % 4]);
  uint8_t op;
  bool fin;
  std::string out;
  TEST_ASSERT_EQUAL((long)m.size(), ws::decode((const uint8_t*)m.data(), m.size(), 1000, true, op, fin, out));
  TEST_ASSERT_EQUAL_STRING("hello", out.c_str());
  TEST_ASSERT_EQUAL(0, ws::decode((const uint8_t*)m.data(), 4, 1000, true, op, fin, out));
  TEST_ASSERT_EQUAL(-1, ws::decode((const uint8_t*)f.data(), f.size(), 1000, true, op, fin, out));  // unmasked from a client
  TEST_ASSERT_EQUAL((long)f.size(), ws::decode((const uint8_t*)f.data(), f.size(), 1000, false, op, fin, out));
  TEST_ASSERT_EQUAL(300, out.size());
}

void http_routes() {
  Sim sim("h");
  sim.addExample();  // a fresh board has no shows
  HttpServer srv(sim);
  Pipe c_pipe; srv.add(new MemConn(c_pipe), 0); Pipe* c = &c_pipe;
  c->in = "GET /api/v1/info HTTP/1.1\r\nHost: x\r\n\r\n";
  srv.poll(1);
  TEST_ASSERT_TRUE(c->out.find("200 OK") != std::string::npos);
  TEST_ASSERT_TRUE(body(c->out).find("\"fw\":\"generic\"") != std::string::npos);
  c->out.clear();
  std::string req = R"({"op":"run","show":1,"id":4})";
  c->in = "POST /api/v1/rpc HTTP/1.1\r\nContent-Type: application/json\r\nOrigin: tauri://localhost\r\nContent-Length: " + std::to_string(req.size()) + "\r\n\r\n" + req;
  srv.poll(2);
  TEST_ASSERT_EQUAL_STRING(R"({"v":1,"id":4,"ok":true,"result":{"kind":"done"}})", body(c->out).c_str());
  TEST_ASSERT_TRUE(c->out.find("Access-Control-Allow-Origin: tauri://localhost") != std::string::npos);
  c->out.clear();
  c->in = "POST /api/v1/rpc HTTP/1.1\r\nContent-Length: 2\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\n\r\n{}";
  srv.poll(3);
  TEST_ASSERT_TRUE(c->out.find("415") != std::string::npos);
  TEST_ASSERT_TRUE(c->out.find("Access-Control") == std::string::npos);
  c->out.clear();
  c->in = "GET /nope HTTP/1.1\r\n\r\nDELETE / HTTP/1.1\r\n\r\n";
  srv.poll(4);
  TEST_ASSERT_TRUE(c->out.find("404") != std::string::npos && c->out.find("405") != std::string::npos);
  c->out.clear();
  c->window = 100;  // a slow socket: the gzipped page goes out over several polls
  c->in = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
  for (int i = 0; i < 100 && !c->closed; i++) srv.poll(5 + i);
  TEST_ASSERT_TRUE(c->closed);
  const WebAsset* a = findWebAsset("/index.html");
  TEST_ASSERT_EQUAL(a->gz_len, body(c->out).size());
  TEST_ASSERT_TRUE(c->out.find("Content-Encoding: gzip") != std::string::npos);
  // never inside another site's frame (clickjacking)
  TEST_ASSERT_TRUE(c->out.find("X-Frame-Options: DENY\r\n") != std::string::npos);
  TEST_ASSERT_TRUE(c->out.find("Content-Security-Policy: frame-ancestors 'none'\r\n") != std::string::npos);
  // kept by the browser and checked again by its ETag: the same copy is a 304 with no body
  TEST_ASSERT_TRUE(c->out.find(std::string("ETag: ") + a->etag + "\r\n") != std::string::npos);
  TEST_ASSERT_TRUE(c->out.find("Cache-Control: no-cache\r\n") != std::string::npos);
  TEST_ASSERT_EQUAL(0, srv.connections());
  Pipe c2_pipe; srv.add(new MemConn(c2_pipe), 200); Pipe* c2 = &c2_pipe;
  c2->in = std::string("GET / HTTP/1.1\r\nIf-None-Match: ") + a->etag + "\r\nConnection: close\r\n\r\n";
  for (int i = 0; i < 10 && !c2->closed; i++) srv.poll(200 + i);
  TEST_ASSERT_TRUE(c2->out.rfind("HTTP/1.1 304 Not Modified", 0) == 0);
  TEST_ASSERT_EQUAL(0, body(c2->out).size());
  // HEAD: the real length, no body
  Pipe c3_pipe; srv.add(new MemConn(c3_pipe), 300); Pipe* c3 = &c3_pipe;
  c3->in = "HEAD / HTTP/1.1\r\nConnection: close\r\n\r\n";
  for (int i = 0; i < 10 && !c3->closed; i++) srv.poll(300 + i);
  TEST_ASSERT_TRUE(c3->out.find("Content-Length: " + std::to_string(a->gz_len) + "\r\n") != std::string::npos);
  TEST_ASSERT_EQUAL(0, body(c3->out).size());
}

void bad_and_oversized_requests() {
  Sim sim("h");
  HttpServer srv(sim);
  Pipe c_pipe; srv.add(new MemConn(c_pipe), 0); Pipe* c = &c_pipe;
  c->in = "NOT HTTP\r\n\r\n";
  srv.poll(1);
  TEST_ASSERT_TRUE(c->out.find("400 Bad Request") != std::string::npos && c->closed);
  Pipe d_pipe; srv.add(new MemConn(d_pipe), 0); Pipe* d = &d_pipe;
  d->in = "POST /api/v1/rpc HTTP/1.1\r\nContent-Length: 999999\r\n\r\n";
  srv.poll(2);
  TEST_ASSERT_TRUE(d->out.find("413") != std::string::npos);
  Pipe e_pipe; srv.add(new MemConn(e_pipe), 0); Pipe* e = &e_pipe;
  e->in = "GET / HTTP/1.1\r\n";
  srv.poll(3);
  srv.poll(40000);
  TEST_ASSERT_TRUE_MESSAGE(e->closed, "idle keep-alive closed");
}

static std::string clientFrame(const std::string& text) {
  std::string m;
  m += (char)0x81;
  const char mask[4] = {9, 8, 7, 6};
  if (text.size() < 126) m += (char)(0x80 | text.size());
  else { m += (char)(0x80 | 126); m += (char)(text.size() >> 8); m += (char)(text.size() & 0xFF); }
  m.append(mask, 4);
  for (size_t i = 0; i < text.size(); i++) m += (char)(text[i] ^ mask[i % 4]);
  return m;
}

static std::vector<std::string> serverFrames(std::string& s) {
  std::vector<std::string> v;
  for (;;) {
    uint8_t op;
    bool fin;
    std::string p;
    long n = ws::decode((const uint8_t*)s.data(), s.size(), 1 << 20, false, op, fin, p);
    if (n <= 0) break;
    s.erase(0, (size_t)n);
    v.push_back(p);
  }
  return v;
}

void websocket_session() {
  Sim sim("h");
  sim.addExample();  // a fresh board has no shows
  HttpServer srv(sim);
  Pipe c_pipe; srv.add(new MemConn(c_pipe), 0); Pipe* c = &c_pipe;
  c->in = "GET /api/v1/ws HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
  srv.poll(1);
  TEST_ASSERT_TRUE(c->out.find("101 Switching Protocols") != std::string::npos);
  TEST_ASSERT_TRUE(c->out.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
  std::string rest = body(c->out);
  auto f = serverFrames(rest);
  TEST_ASSERT_TRUE(f.size() >= 1 && f[0].find("\"event\":\"state\"") != std::string::npos);
  c->out.clear();
  c->in = clientFrame(R"({"op":"run","show":1,"id":9})");
  srv.poll(2);
  for (int i = 0; i < 30; i++) sim.tick();
  srv.poll(500);
  auto g = serverFrames(c->out);
  bool resp = false, notice = false, state = false;
  for (auto& x : g) {
    resp |= x == R"({"v":1,"id":9,"ok":true,"result":{"kind":"done"}})";
    notice |= x.find("\"notice\":\"started\"") != std::string::npos;
    state |= x.find("\"event\":\"state\"") != std::string::npos;
  }
  TEST_ASSERT_TRUE(resp && notice && state);
  // ping, then close
  c->out.clear();
  std::string ping;
  ping += (char)0x89;
  ping += (char)0x80;
  ping.append("\0\0\0\0", 4);
  c->in = ping;
  srv.poll(600);
  TEST_ASSERT_EQUAL(0x8A, (uint8_t)c->out[0]);
  std::string close;
  close += (char)0x88;
  close += (char)0x80;
  close.append("\0\0\0\0", 4);
  c->in = close;
  srv.poll(700);
  TEST_ASSERT_TRUE(c->closed);
}

// A firmware that ties state to WebSocket sessions and wants a heartbeat.
struct SessionApp : Sim {
  std::vector<uint32_t> calls, closed;
  SessionApp() : Sim("s") {}
  std::string callFrom(uint32_t ss, const char* b, size_t n) override { calls.push_back(ss); return Sim::call(b, n); }
  void sessionClosed(uint32_t ss) override { closed.push_back(ss); }
  uint32_t wsHeartbeatMs() override { return 5000; }
};

void websocket_sessions_and_heartbeat() {
  SessionApp app;
  HttpServer srv(app);
  Pipe c_pipe; srv.add(new MemConn(c_pipe), 0); Pipe* c = &c_pipe;
  Pipe d_pipe; srv.add(new MemConn(d_pipe), 0); Pipe* d = &d_pipe;
  const char* up = "GET /api/v1/ws HTTP/1.1\r\nUpgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
  c->in = up;
  d->in = up;
  srv.poll(10);
  c->in = clientFrame(R"({"op":"info","id":1})");
  d->in = clientFrame(R"({"op":"info","id":2})");
  srv.poll(20);
  TEST_ASSERT_EQUAL(2, app.calls.size());
  TEST_ASSERT_TRUE(app.calls[0] != app.calls[1] && app.calls[0] > 0);
  // c answers the pings, d says nothing
  for (Millis t = 100; t <= 7000; t += 100) {
    if (t % 1000 == 0) {
      std::string pong;
      pong += (char)0x8A;
      pong += (char)0x80;
      pong.append("\0\0\0\0", 4);
      c->in += pong;
    }
    srv.poll(t);
  }
  TEST_ASSERT_TRUE(c->out.find((char)0x89) != std::string::npos);  // pings went out
  TEST_ASSERT_FALSE(c->closed);
  TEST_ASSERT_TRUE(d->closed);
  TEST_ASSERT_EQUAL(1, app.closed.size());
  TEST_ASSERT_EQUAL(app.calls[1], app.closed[0]);
}

static std::string shaHex(const std::string& b) {
  uint8_t d[32];
  sha256((const uint8_t*)b.data(), b.size(), d);
  return toHex(d, 32);
}

static std::string updateHead(size_t len, const std::string& sha, const char* extra = "") {
  return "POST /api/v1/update HTTP/1.1\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(len) +
         "\r\nX-Proptroller-Sha256: " + sha + "\r\n" + extra + "\r\n";
}

// POST /api/v1/update: streamed in pieces over several polls, the answer, Connection: close, the restart
void update_streams_the_body() {
  Sim sim("u");
  HttpServer srv(sim);
  Pipe p;
  srv.add(new MemConn(p), 0);
  std::string img = sim_image::build("0.3.0", "abc1234", true, 200 * 1024);
  p.in = updateHead(img.size(), shaHex(img), "Origin: tauri://localhost\r\n") + img.substr(0, 5000);
  srv.poll(1);
  TEST_ASSERT_TRUE(sim.ctl().updating());
  TEST_ASSERT_TRUE(p.out.empty());
  for (size_t at = 5000; at < img.size(); at += 30000) {
    p.in += img.substr(at, 30000);
    srv.poll(2);
  }
  for (int i = 0; i < 20 && p.out.empty(); i++) srv.poll(2);  // the server reads at most 16 KB a poll
  TEST_ASSERT_EQUAL_STRING(R"({"v":1,"ok":true,"result":{"kind":"done"}})", body(p.out).c_str());
  TEST_ASSERT_TRUE(p.out.find("Connection: close") != std::string::npos);
  TEST_ASSERT_TRUE(p.out.find("Access-Control-Allow-Headers: Content-Type, X-Proptroller-Sha256, X-Proptroller-Pin") != std::string::npos);
  srv.poll(3);
  TEST_ASSERT_TRUE(p.closed);
  sim.step(1100);
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("fw_version":"0.3.0","fw_impl":"cpp","fw_build":"abc1234")") != std::string::npos);
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("state":"pending")") != std::string::npos);
}

// a refusal is answered only after the rest of the body was read and dropped; nothing was written
void update_refusal_after_the_body() {
  Sim sim("u");
  HttpServer srv(sim);
  Pipe p;
  srv.add(new MemConn(p), 0);
  sim.request(R"({"op":"set_output","channel":"OUT3","level":500,"ms":600000})");
  std::string img = sim_image::build("0.3.0", "abc1234", true, 50 * 1024);
  p.in = updateHead(img.size(), shaHex(img)) + img.substr(0, 1000);
  srv.poll(1);
  TEST_ASSERT_TRUE(p.out.empty());  // still sending: not cut off
  p.in = img.substr(1000);
  for (int i = 0; i < 20 && p.out.empty(); i++) srv.poll(2);
  TEST_ASSERT_EQUAL_STRING(R"({"v":1,"ok":false,"error":{"code":"refused","message":"a manual output test is on: release it first"}})", body(p.out).c_str());
  TEST_ASSERT_TRUE(p.out.find("Connection: close") != std::string::npos);
  TEST_ASSERT_FALSE(sim.ctl().updating());
  // no Content-Length: answered at once
  Pipe q;
  srv.add(new MemConn(q), 3);
  q.in = "POST /api/v1/update HTTP/1.1\r\nContent-Type: application/octet-stream\r\nX-Proptroller-Sha256: " + shaHex(img) + "\r\n\r\n";
  srv.poll(3);
  TEST_ASSERT_EQUAL_STRING(R"({"v":1,"ok":false,"error":{"code":"bad_request","message":"Content-Length is required"}})", body(q.out).c_str());
  // an OPTIONS preflight names the update headers
  Pipe o;
  srv.add(new MemConn(o), 4);
  o.in = "OPTIONS /api/v1/update HTTP/1.1\r\nOrigin: tauri://localhost\r\n\r\n";
  srv.poll(4);
  TEST_ASSERT_TRUE(o.out.find("X-Proptroller-Sha256, X-Proptroller-Pin") != std::string::npos);
}

// the connection ends half way: the update stops, the board leaves update mode, nothing boots next
void update_connection_lost() {
  Sim sim("u");
  HttpServer srv(sim);
  Pipe p;
  srv.add(new MemConn(p), 0);
  std::string img = sim_image::build("0.3.0", "abc1234", true, 50 * 1024);
  p.in = updateHead(img.size(), shaHex(img)) + img.substr(0, 20000);
  srv.poll(1);
  TEST_ASSERT_TRUE(sim.ctl().updating());
  p.peer_closed = true;
  srv.poll(2);
  TEST_ASSERT_FALSE(sim.ctl().updating());
  TEST_ASSERT_EQUAL(0, srv.connections());
  TEST_ASSERT_EQUAL(-1, sim.slots().boot_next);
  std::vector<std::string> ev;
  sim.ctl().eventsSince(0, ev);
  TEST_ASSERT_TRUE(ev.back().find(R"("phase":"failed")") != std::string::npos);
  TEST_ASSERT_TRUE(ev.back().find("the connection ended before the whole image arrived") != std::string::npos);
}

// A socket that takes nothing (a phone out of range): poll() returns at once, the answer stays queued and goes
// out whole once the socket drains (the Conn contract: write() takes only what fits, never blocks).
void stalled_socket_never_blocks() {
  Sim sim("h");
  HttpServer srv(sim);
  Pipe p;
  p.window = 0;
  srv.add(new MemConn(p), 0);
  p.in = "GET /api/v1/info HTTP/1.1\r\n\r\n";
  for (int i = 1; i <= 50; i++) srv.poll(i);
  TEST_ASSERT_TRUE(p.out.empty());
  TEST_ASSERT_EQUAL(1, srv.connections());
  p.window = 7;
  for (int i = 51; i < 2000; i++) srv.poll(i);
  TEST_ASSERT_TRUE(p.out.find("200 OK") == 9);
  TEST_ASSERT_TRUE(body(p.out).find("\"fw\":\"generic\"") != std::string::npos);
}

// MQTT state and events carry boot_id and one sequence number (PROTOCOL.md "MQTT"); other topics are untouched
void mqtt_stamp_boot_id_and_seq() {
  uint32_t seq = 0;
  TEST_ASSERT_EQUAL_STRING(R"({"boot_id":"0a1b2c3d","mqtt_seq":1,"v":1,"uptime_ms":5})",
                           mqttStamp("proptroller/door/state", R"({"v":1,"uptime_ms":5})", "0a1b2c3d", seq).c_str());
  TEST_ASSERT_EQUAL_STRING(R"({"boot_id":"0a1b2c3d","mqtt_seq":2,"v":1,"seq":4,"event":"params_changed"})",
                           mqttStamp("proptroller/door/event", R"({"v":1,"seq":4,"event":"params_changed"})", "0a1b2c3d", seq).c_str());
  TEST_ASSERT_EQUAL_STRING("true", mqttStamp("proptroller/door/online", "true", "0a1b2c3d", seq).c_str());
  TEST_ASSERT_EQUAL_STRING("{}", mqttStamp("proptroller/door/config/name", "{}", "0a1b2c3d", seq).c_str());
  TEST_ASSERT_EQUAL_STRING(R"({"boot_id":"0a1b2c3d","mqtt_seq":3})", mqttStamp("x/state", "{}", "0a1b2c3d", seq).c_str());
  TEST_ASSERT_EQUAL(3, seq);
}

// Origin / Host (DNS rebinding, a foreign page): the reviewer's ws_any_origin repro, inverted
static std::string ask(HttpServer& srv, const std::string& req, Millis t) {
  static std::list<Pipe> pipes;  // the server keeps (and may still use) each connection
  pipes.emplace_back();
  Pipe& p = pipes.back();
  srv.add(new MemConn(p), t);
  p.in = req;
  for (int i = 0; i < 3; i++) srv.poll(t + i);
  return p.out.substr(0, p.out.find("\r\n"));
}
void origin_and_host_checks() {
  Sim sim("o");
  HttpServer srv(sim);
  const std::string ws = "GET /api/v1/ws HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
  const std::string rpc = "POST /api/v1/rpc HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n";
  // a WebSocket from another site's page is refused; nothing it sends reaches the board
  Pipe p;
  srv.add(new MemConn(p), 0);
  p.in = ws + "Host: 192.168.4.1\r\nOrigin: http://evil.example\r\n\r\n";
  p.in += clientFrame(R"({"v":1,"id":1,"op":"set_params","values":[{"key":"name","value":"pwned"}]})");
  srv.poll(1);
  srv.poll(2);
  TEST_ASSERT_TRUE(p.out.find("403 Forbidden") == 9);
  TEST_ASSERT_TRUE(p.out.find("101 Switching") == std::string::npos);
  TEST_ASSERT_TRUE(p.closed);
  TEST_ASSERT_TRUE(sim.infoJson().find("pwned") == std::string::npos);
  // the board's own page, by address, by name.local and by name (with a port), and the desktop app
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 101 Switching Protocols", ask(srv, ws + "Host: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\n\r\n", 10).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 101 Switching Protocols", ask(srv, ws + "Host: Front-Door.local\r\nOrigin: http://front-door.local\r\n\r\n", 20).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 101 Switching Protocols", ask(srv, ws + "Host: localhost:8080\r\nOrigin: http://localhost:8080\r\n\r\n", 30).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 101 Switching Protocols", ask(srv, ws + "Host: 10.0.0.7\r\nOrigin: tauri://localhost\r\n\r\n", 40).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 200 OK", ask(srv, rpc + "Host: [fe80::1]:80\r\nOrigin: https://tauri.localhost\r\n\r\n{}", 50).c_str());
  // no Origin: not a browser page (the desktop app's HTTP client, ptclient, curl)
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 200 OK", ask(srv, rpc + "Host: 192.168.4.1\r\n\r\n{}", 60).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 200 OK", ask(srv, rpc + "\r\n{}", 70).c_str());
  // a page on another site: no POST, no upload; GET still works (the page itself, read-only state)
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 403 Forbidden", ask(srv, rpc + "Host: 192.168.4.1\r\nOrigin: http://evil.example\r\n\r\n{}", 80).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 403 Forbidden", ask(srv, rpc + "Host: 192.168.4.1\r\nOrigin: null\r\n\r\n{}", 90).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 403 Forbidden", ask(srv, "POST /api/v1/update HTTP/1.1\r\nContent-Length: 10\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.2\r\n\r\n", 100).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 200 OK", ask(srv, "GET /api/v1/info HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://evil.example\r\n\r\n", 110).c_str());
  // DNS rebinding: another name for the board's address, even with a matching Origin, even for a GET
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 403 Forbidden", ask(srv, ws + "Host: evil.example\r\nOrigin: http://evil.example\r\n\r\n", 120).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 403 Forbidden", ask(srv, "GET /api/v1/state HTTP/1.1\r\nHost: evil.example:80\r\n\r\n", 130).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 403 Forbidden", ask(srv, "GET / HTTP/1.1\r\nHost: a.b.local\r\n\r\n", 140).c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP/1.1 403 Forbidden", ask(srv, "GET / HTTP/1.1\r\nHost: 192.168.4.1:x\r\n\r\n", 150).c_str());
}

static const char* const UPGRADE = "GET /api/v1/ws HTTP/1.1\r\nUpgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
static std::string pingFrame() { return std::string("\x89\x80\0\0\0\0", 6); }
static std::string pongFrame() { return std::string("\x8a\x80\0\0\0\0", 6); }
static int countStates(std::string out) {
  out = out.substr(out.find("\r\n\r\n") == std::string::npos ? 0 : out.find("\r\n\r\n") + 4);
  int n = 0;
  for (auto& f : serverFrames(out)) n += f.find("\"event\":\"state\"") != std::string::npos;
  return n;
}

// Backpressure: a reader that does not read is closed once more than max_queue waits behind what it is sent
void queue_cap_closes_a_reader_that_does_not_read() {
  SessionApp app;
  HttpLimits l;
  l.stall_ms = 1000000;  // only the queue cap here
  HttpServer srv(app, l);
  Pipe p;
  srv.add(new MemConn(p), 0);
  p.in = UPGRADE;
  srv.poll(1);
  p.window = 0;
  Millis t = 2;
  for (int i = 0; i < 400 && !p.closed; i++, t++) {
    p.in += pingFrame();  // each answered with a pong that cannot go out
    srv.poll(t);
  }
  TEST_ASSERT_TRUE(p.closed);
  TEST_ASSERT_EQUAL(1, app.closed.size());
  TEST_ASSERT_EQUAL(0, srv.connections());
}

// At most one unsent state frame per client: while the socket is full a newer state replaces the waiting one
void one_unsent_state_frame_per_client() {
  Sim sim("q");
  HttpServer srv(sim);
  Pipe p;
  p.window = 0;
  srv.add(new MemConn(p), 0);
  p.in = UPGRADE;
  srv.poll(sim.now());
  sim.request(R"({"op":"run","show":1})");
  for (int i = 0; i < 300; i++) {  // 3 s of a running show: the state changes all the time
    sim.tick();
    if (i % 100 == 0) p.in += pongFrame();
    srv.poll(sim.now());
  }
  p.window = 1 << 20;
  srv.poll(sim.now());
  TEST_ASSERT_EQUAL(1, countStates(p.out));
}

// An idle board sends nothing but the 5 s keep-alive (uptime_ms is left out of the comparison, as for MQTT)
void idle_board_sends_only_the_keep_alive() {
  Sim sim("i");
  sim.step(3000);  // the ambient loop has started
  HttpServer srv(sim);
  Pipe p;
  srv.add(new MemConn(p), sim.now());
  p.in = UPGRADE;
  srv.poll(sim.now());
  TEST_ASSERT_EQUAL(1, countStates(p.out));
  p.out.clear();
  for (int i = 0; i < 49; i++) {  // 4.9 s
    sim.step(100);
    if (i % 10 == 0) p.in += pongFrame();
    srv.poll(sim.now());
  }
  std::string o = p.out;
  TEST_ASSERT_EQUAL(0, countStates(o));
  sim.step(200);
  srv.poll(sim.now());
  TEST_ASSERT_EQUAL(1, countStates(p.out));
}

// The state is built once per push for all clients
struct CountingSim : Sim {
  int states = 0;
  CountingSim() : Sim("c") {}
  std::string stateJson() override { states++; return Sim::stateJson(); }
};
void state_built_once_for_all_clients() {
  CountingSim sim;
  sim.addExample();  // a fresh board has no shows
  HttpServer srv(sim);
  Pipe p[4];
  for (auto& x : p) { srv.add(new MemConn(x), sim.now()); x.in = UPGRADE; }
  srv.poll(sim.now());
  sim.request(R"({"op":"run","show":1})");
  sim.states = 0;
  for (int i = 0; i < 100; i++) {  // 1 s, a poll every 10 ms, the state changing
    sim.tick();
    srv.poll(sim.now());
  }
  printf("4 clients, 1 s: %d state builds\n", sim.states);
  TEST_ASSERT_TRUE(sim.states <= 6);
  for (auto& x : p) TEST_ASSERT_TRUE(countStates(x.out) >= 5);
}

// A closing connection whose close frame cannot go out is dropped 2 s later (a zombie holds no slot)
void zombie_dropped_after_two_seconds() {
  SessionApp app;
  HttpServer srv(app);
  Pipe p;
  srv.add(new MemConn(p), 0);
  p.in = UPGRADE;
  srv.poll(1);
  p.window = 0;
  Millis t = 2;
  for (; t < 5100 && !p.closed; t += 100) srv.poll(t);  // silent: the heartbeat ends it at 5 s
  TEST_ASSERT_FALSE(p.closed);
  for (; t < 8000 && !p.closed; t += 100) srv.poll(t);
  TEST_ASSERT_TRUE(p.closed);
  TEST_ASSERT_TRUE(t <= 5000 + 2000 + 300);  // heartbeat at 5 s, dropped 2 s later
  TEST_ASSERT_EQUAL(1, app.closed.size());
}

// Connection limits: WebSocket sessions capped with a 503; only idle keep-alive connections that were served
// are evicted, never a new one or an upload
void connection_limits() {
  Sim sim("l");
  HttpLimits l = HttpLimits::picoW();
  HttpServer srv(sim, l);
  static std::list<Pipe> pipes;
  auto open = [&](const std::string& req, Millis t) -> Pipe& {
    pipes.emplace_back();
    Pipe& x = pipes.back();
    srv.add(new MemConn(x), t);
    x.in = req;
    srv.poll(t);
    return x;
  };
  Pipe& a = open(UPGRADE, 1);
  Pipe& b = open(UPGRADE, 2);
  Pipe& c = open(UPGRADE, 3);  // a third page on a Pico W
  TEST_ASSERT_TRUE(a.out.find("101") != std::string::npos && b.out.find("101") != std::string::npos);
  TEST_ASSERT_TRUE(c.out.find("503 Service Unavailable") == 9 && c.closed);
  srv.poll(4);
  TEST_ASSERT_EQUAL(2, srv.connections());
  // the third slot: a connection that has not sent its request yet is never evicted
  Pipe& d = open("", 5);
  TEST_ASSERT_FALSE(srv.canAdd());
  Pipe& e = open("GET /api/v1/info HTTP/1.1\r\n\r\n", 6);  // refused: nothing may make room
  TEST_ASSERT_TRUE(e.closed && e.out.empty());
  TEST_ASSERT_FALSE(d.closed);
  d.in = "GET /api/v1/info HTTP/1.1\r\n\r\n";  // served, kept alive: now it may make room
  srv.poll(7);
  TEST_ASSERT_TRUE(srv.canAdd());
  Pipe& f = open("GET /api/v1/info HTTP/1.1\r\n\r\n", 8);
  TEST_ASSERT_TRUE(d.closed);
  TEST_ASSERT_TRUE(f.out.find("200 OK") == 9);
}

static std::string shaHex(const std::string& b);
static std::string updateHead(size_t len, const std::string& sha, const char* extra);

// An upload is never evicted; one that stops sending ends update mode after upload_idle_ms (reviewer repro A)
void upload_never_evicted_and_times_out() {
  Sim sim("u");
  HttpServer srv(sim);
  Pipe up;
  srv.add(new MemConn(up), 0);
  std::string img = sim_image::build("0.3.0", "abc1234", true, 50 * 1024);
  up.in = updateHead(img.size(), shaHex(img), "") + img.substr(0, 1000);
  srv.poll(1);
  TEST_ASSERT_TRUE(sim.ctl().updating());
  std::list<Pipe> others;
  for (int i = 0; i < 12; i++) {
    others.emplace_back();
    others.back().in = "GET /api/v1/info HTTP/1.1\r\n\r\n";
    if (srv.canAdd()) srv.add(new MemConn(others.back()), 2);
    srv.poll(2);
  }
  TEST_ASSERT_FALSE(up.closed);
  TEST_ASSERT_TRUE(sim.ctl().updating());
  for (Millis t = 3; t < 12000; t += 500) srv.poll(t);
  TEST_ASSERT_TRUE(up.closed);
  TEST_ASSERT_FALSE_MESSAGE(sim.ctl().updating(), "update mode ends when the upload stops");
}

// Memory: bodies up to the largest backup; one large body at a time (503 for a second); a WebSocket message that
// needs the large slot waits for it; what the other clients can pin is bounded
void request_memory_limits() {
  Sim sim("m");
  HttpServer srv(sim);
  TEST_ASSERT_TRUE(MAX_REQUEST_BODY >= 40 * 1024 && MAX_REQUEST_BODY <= 44 * 1024);
  std::list<Pipe> p;
  auto post = [&](size_t len, const std::string& body) {
    p.emplace_back();
    srv.add(new MemConn(p.back()), 0);
    p.back().in = "POST /api/v1/rpc HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(len) + "\r\n\r\n" + body;
    return &p.back();
  };
  Pipe* big = post(MAX_REQUEST_BODY + 1, "");
  srv.poll(1);
  TEST_ASSERT_TRUE(big->out.find("413") == 9);
  // two large bodies at once: the first is received, the second is told to try again
  std::string filler(30000, ' ');
  Pipe* one = post(30002, "{}");
  Pipe* two = post(30002, "{}");
  srv.poll(2);
  TEST_ASSERT_TRUE(two->out.find("503 Service Unavailable") == 9);
  one->in += filler;
  for (int i = 0; i < 10; i++) srv.poll(3 + i);
  TEST_ASSERT_TRUE(one->out.find("200 OK") == 9);
  // the reviewer's 6 x 40 KB: one large slot, the rest refused; the server holds little
  std::vector<Pipe*> six;
  for (int i = 0; i < 6; i++) six.push_back(post(40000, std::string(39999, ' ')));
  for (int k = 0; k < 3; k++) srv.poll(20 + k);
  size_t refused = 0, unread = 0;
  for (auto x : six) { refused += x->out.find("503") == 9; unread += x->in.size(); }
  TEST_ASSERT_EQUAL(5, refused);
  printf("6 x 40 KB: %zu B taken in by the server\n", 6 * 39999 - unread);
}

// A keep-alive connection gives a large body's memory back once it is answered (review note N5), and the body is
// received into one buffer of its size, not doubled as it arrives
void a_keep_alive_connection_gives_a_large_body_back() {
  Sim sim("k");
  HttpServer srv(sim);
  Pipe c;
  srv.add(new MemConn(c), 0);
  std::string req = R"({"op":"info","pad":")" + std::string(30000, ' ') + "\"}";
  c.in = "POST /api/v1/rpc HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(req.size()) + "\r\n\r\n" + req;
  size_t peak = 0;
  for (int i = 0; i < 60 && c.out.find("\r\n\r\n") == std::string::npos; i++) {
    srv.poll(1 + i);
    if (srv.inputCapacity() > peak) peak = srv.inputCapacity();
  }
  TEST_ASSERT_TRUE_MESSAGE(c.out.find("200 OK") == 9, c.out.substr(0, 200).c_str());
  TEST_ASSERT_FALSE(c.closed);
  TEST_ASSERT_TRUE_MESSAGE(peak >= req.size() && peak < req.size() + 4096, std::to_string(peak).c_str());
  TEST_ASSERT_TRUE_MESSAGE(srv.inputCapacity() <= 4096, std::to_string(srv.inputCapacity()).c_str());
  // the connection still serves
  c.out.clear();
  c.in = "GET /api/v1/info HTTP/1.1\r\n\r\n";
  srv.poll(100);
  TEST_ASSERT_TRUE(c.out.find("200 OK") == 9);
  // a large WebSocket message is given back the same way
  Pipe w;
  srv.add(new MemConn(w), 200);
  w.in = UPGRADE;
  srv.poll(201);
  std::string msg = R"({"op":"info","id":1,"pad":")" + std::string(20000, ' ') + "\"}";
  std::string f;
  f += (char)0x81;
  f += (char)(0x80 | 127);
  for (int k = 7; k >= 0; k--) f += (char)((uint64_t)msg.size() >> (8 * k) & 0xff);
  f.append("\0\0\0\0", 4);  // mask of zeros: the payload as it is
  f += msg;
  w.out.clear();
  w.in = f;
  for (int i = 0; i < 60 && w.out.find("\"id\":1") == std::string::npos; i++) srv.poll(202 + i);
  TEST_ASSERT_TRUE_MESSAGE(w.out.find("\"id\":1") != std::string::npos, w.out.substr(0, 200).c_str());
  TEST_ASSERT_TRUE_MESSAGE(srv.inputCapacity() <= 8192, std::to_string(srv.inputCapacity()).c_str());
}

void large_websocket_messages_take_turns() {
  Sim sim("w");
  HttpServer srv(sim);
  Pipe a, b;
  srv.add(new MemConn(a), 0);
  srv.add(new MemConn(b), 0);
  a.in = UPGRADE;
  b.in = UPGRADE;
  srv.poll(1);
  std::string pad(10000, ' ');
  a.in = clientFrame("{\"op\":\"info\",\"id\":1" + pad + "}");
  b.in = clientFrame("{\"op\":\"info\",\"id\":2" + pad + "}");
  for (int i = 0; i < 40; i++) srv.poll(2 + i);
  TEST_ASSERT_TRUE(a.out.find("\"id\":1,\"ok\":true") != std::string::npos);
  TEST_ASSERT_TRUE(b.out.find("\"id\":2,\"ok\":true") != std::string::npos);
  // a large POST that stalls holds the slot only large_idle_ms: the waiting WebSocket message is then answered
  Pipe s;
  srv.add(new MemConn(s), 100);
  s.in = "POST /api/v1/rpc HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 30000\r\n\r\n" + std::string(5000, ' ');
  srv.poll(100);
  a.out.clear();
  a.in = clientFrame("{\"op\":\"info\",\"id\":3" + pad + "}");
  Millis t = 101;
  for (; t < 4500 && a.out.find("\"id\":3,\"ok\":true") == std::string::npos; t += 50) {
    if (t % 1000 < 50) a.in += pongFrame();
    srv.poll(t);
  }
  TEST_ASSERT_TRUE(s.closed);
  TEST_ASSERT_TRUE(a.out.find("\"id\":3,\"ok\":true") != std::string::npos);
  TEST_ASSERT_TRUE(t > 3000 && !a.closed);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(head_parsing);
  RUN_TEST(websocket_accept_key_rfc6455);
  RUN_TEST(frames_round_trip);
  RUN_TEST(http_routes);
  RUN_TEST(bad_and_oversized_requests);
  RUN_TEST(websocket_session);
  RUN_TEST(websocket_sessions_and_heartbeat);
  RUN_TEST(update_streams_the_body);
  RUN_TEST(update_refusal_after_the_body);
  RUN_TEST(update_connection_lost);
  RUN_TEST(stalled_socket_never_blocks);
  RUN_TEST(mqtt_stamp_boot_id_and_seq);
  RUN_TEST(origin_and_host_checks);
  RUN_TEST(queue_cap_closes_a_reader_that_does_not_read);
  RUN_TEST(one_unsent_state_frame_per_client);
  RUN_TEST(idle_board_sends_only_the_keep_alive);
  RUN_TEST(state_built_once_for_all_clients);
  RUN_TEST(zombie_dropped_after_two_seconds);
  RUN_TEST(connection_limits);
  RUN_TEST(upload_never_evicted_and_times_out);
  RUN_TEST(request_memory_limits);
  RUN_TEST(a_keep_alive_connection_gives_a_large_body_back);
  RUN_TEST(large_websocket_messages_take_turns);
  return UNITY_END();
}
