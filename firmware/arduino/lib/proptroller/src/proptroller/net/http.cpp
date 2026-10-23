#include "http.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace pt {

static const char* const ALLOWED_ORIGINS[] = {"tauri://localhost", "http://tauri.localhost", "https://tauri.localhost"};

static std::string lower(std::string s) {
  for (auto& c : s) c = (char)tolower((unsigned char)c);
  return s;
}
static std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

bool parseHead(const std::string& raw, HttpHead& h) {
  size_t eol = raw.find("\r\n");
  std::string first = raw.substr(0, eol);
  size_t s1 = first.find(' ');
  if (s1 == std::string::npos) return false;
  size_t s2 = first.find(' ', s1 + 1);
  if (s2 == std::string::npos) return false;
  h.method = first.substr(0, s1);
  std::string target = first.substr(s1 + 1, s2 - s1 - 1);
  std::string version = first.substr(s2 + 1);
  if (version.compare(0, 7, "HTTP/1.") != 0 || h.method.empty() || target.empty() || target[0] != '/') return false;
  h.path = target.substr(0, target.find('?'));
  h.close = version == "HTTP/1.0";
  size_t pos = eol == std::string::npos ? raw.size() : eol + 2;
  while (pos < raw.size()) {
    size_t e = raw.find("\r\n", pos);
    if (e == std::string::npos) e = raw.size();
    std::string line = raw.substr(pos, e - pos);
    pos = e + 2;
    if (line.empty()) continue;
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    std::string k = lower(trim(line.substr(0, colon))), v = trim(line.substr(colon + 1));
    if (k == "content-length") {
      if (v.empty()) return false;
      for (char ch : v)
        if (!isdigit((unsigned char)ch)) return false;
      h.content_length = strtoul(v.c_str(), nullptr, 10);
      h.has_length = true;
    } else if (k == "content-type") {
      h.json = lower(v).compare(0, 16, "application/json") == 0;
      h.octet_stream = lower(v).compare(0, 24, "application/octet-stream") == 0;
    } else if (k == "x-proptroller-sha256") {
      h.has_sha256 = true;
      h.sha256 = v;
    } else if (k == "x-proptroller-pin") {
      h.has_pin = true;
      h.pin = v;
    } else if (k == "connection") {
      std::string l = lower(v);
      if (l.find("close") != std::string::npos) h.close = true;
      if (l.find("keep-alive") != std::string::npos) h.close = false;
    } else if (k == "upgrade") {
      h.upgrade_ws = lower(v) == "websocket";
    } else if (k == "sec-websocket-key") {
      h.has_ws_key = true;
      h.ws_key = v;
    } else if (k == "if-none-match") {
      h.if_none_match = v;
    } else if (k == "origin") {
      h.has_origin = true;
      h.origin = v;
    } else if (k == "host") {
      h.has_host = true;
      h.host = v;
    }
  }
  return true;
}

static bool endsWith(const std::string& s, const char* t) {
  size_t n = strlen(t);
  return s.size() >= n && s.compare(s.size() - n, n, t) == 0;
}

std::string mqttStamp(const std::string& topic, const std::string& payload, const std::string& boot_id, uint32_t& seq) {
  if (payload.empty() || payload[0] != '{' || !(endsWith(topic, "/state") || endsWith(topic, "/event"))) return payload;
  char head[64];
  (void)snprintf(head, sizeof head, "{\"boot_id\":\"%s\",\"mqtt_seq\":%lu%s", boot_id.c_str(), (unsigned long)++seq, payload.size() > 2 ? "," : "");
  return head + payload.substr(1);
}

// ---------------------------------------------------------------- SHA-1 / base64 for the WebSocket accept key

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::string m((const char*)data, len);
  m += (char)0x80;
  while (m.size() % 64 != 56) m += (char)0;
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 7; i >= 0; i--) m += (char)((bits >> (i * 8)) & 0xFF);
  for (size_t chunk = 0; chunk < m.size(); chunk += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)(uint8_t)m[chunk + 4 * i] << 24) | ((uint32_t)(uint8_t)m[chunk + 4 * i + 1] << 16) |
             ((uint32_t)(uint8_t)m[chunk + 4 * i + 2] << 8) | (uint8_t)m[chunk + 4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f = 0, k = 0;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(h[i] >> (24 - 8 * j));
}

static std::string base64(const uint8_t* d, size_t n) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    o += T[(v >> 18) & 63];
    o += T[(v >> 12) & 63];
    o += i + 1 < n ? T[(v >> 6) & 63] : '=';
    o += i + 2 < n ? T[v & 63] : '=';
  }
  return o;
}

namespace ws {
std::string acceptKey(const std::string& key) {
  std::string s = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t h[20];
  sha1((const uint8_t*)s.data(), s.size(), h);
  return base64(h, 20);
}

long frame(const uint8_t* buf, size_t n, size_t max, bool require_mask, uint8_t& opcode, bool& fin, size_t& head, size_t& len) {
  if (n < 2) return 0;
  fin = buf[0] & 0x80;
  if (buf[0] & 0x70) return -1;
  opcode = buf[0] & 0x0F;
  bool masked = buf[1] & 0x80;
  if (require_mask && !masked) return -1;
  size_t i = 2;
  uint64_t l = buf[1] & 0x7F;
  if (l == 126) {
    if (n < 4) return 0;
    l = (uint64_t)buf[2] << 8 | buf[3];
    i = 4;
  } else if (l == 127) {
    if (n < 10) return 0;
    l = 0;
    for (int k = 0; k < 8; k++) l = l << 8 | buf[2 + k];
    i = 10;
  }
  if (l > max) return -1;
  head = i + (masked ? 4 : 0);
  len = (size_t)l;
  return (long)(head + len);
}

long decode(const uint8_t* buf, size_t n, size_t max, bool require_mask, uint8_t& opcode, bool& fin, std::string& payload) {
  size_t head = 0, len = 0;
  long total = frame(buf, n, max, require_mask, opcode, fin, head, len);
  if (total <= 0 || n < (size_t)total) return total < 0 ? -1 : 0;
  payload.assign((const char*)buf + head, len);
  if (buf[1] & 0x80)
    for (size_t k = 0; k < len; k++) payload[k] ^= buf[head - 4 + k % 4];
  return total;
}

std::string encode(uint8_t opcode, const std::string& p) {
  std::string f;
  f += (char)(0x80 | opcode);
  size_t n = p.size();
  if (n < 126) f += (char)n;
  else if (n <= 0xFFFF) { f += (char)126; f += (char)(n >> 8); f += (char)(n & 0xFF); }
  else { f += (char)127; for (int k = 7; k >= 0; k--) f += (char)((uint64_t)n >> (8 * k) & 0xFF); }
  f += p;
  return f;
}
}  // namespace ws

// ---------------------------------------------------------------- server

// Host: an address, <name>.local or <name> (with a port or not). Anything else is a name some other DNS gave
// the board's address: a web page using DNS rebinding.
static bool hostOk(const std::string& host) {
  std::string h = lower(host);
  if (!h.empty() && h[0] == '[') {  // [IPv6]:port
    size_t e = h.find(']');
    if (e == std::string::npos) return false;
    for (size_t i = 1; i < e; i++)
      if (!isxdigit((unsigned char)h[i]) && h[i] != ':' && h[i] != '.') return false;
    h = h.substr(e + 1);
    return h.empty() || (h[0] == ':' && h.size() > 1 && h.find_first_not_of("0123456789", 1) == std::string::npos);
  }
  size_t colon = h.find(':');
  if (colon != std::string::npos) {
    std::string port = h.substr(colon + 1);
    if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos) return false;
    h = h.substr(0, colon);
  }
  if (h.empty()) return false;
  if (h.find_first_not_of("0123456789.") == std::string::npos) return true;  // IPv4
  if (endsWith(h, ".local")) h = h.substr(0, h.size() - 6);
  return !h.empty() && h.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") == std::string::npos;
}

// Who may change things from a browser: the board's own page (Origin = http://<Host>) and a Tauri web view.
static bool originOk(const HttpHead& h) {
  if (!h.has_origin) return true;  // not a browser page (Specter Studio's native core, ptclient, curl)
  for (auto o : ALLOWED_ORIGINS)
    if (h.origin == o) return true;
  return h.has_host && lower(h.origin) == "http://" + lower(h.host);
}

// A large request answered: the slot is free again and the client's input buffer gives its memory back (a
// keep-alive connection would otherwise hold the largest body it ever sent until it closes).
void HttpServer::endLarge(Client& c) {
  if (big_ == &c) big_ = nullptr;
  if (c.in.capacity() > limits_.small_body + limits_.max_header) std::string(c.in).swap(c.in);
}

size_t HttpServer::inputCapacity() const {
  size_t n = 0;
  for (auto& c : conns_) n += c->in.capacity();
  return n;
}

bool HttpServer::evictable(const Client& c) { return !c.ws && !c.upload && c.served > 0 && c.out.empty() && c.in.empty(); }

bool HttpServer::canAdd() const {
  if (conns_.size() < limits_.max_connections) return true;
  for (auto& c : conns_)
    if (evictable(*c)) return true;
  return false;
}

void HttpServer::add(Conn* c, Millis now) {
  if (conns_.size() >= limits_.max_connections) {
    // full: the keep-alive connection idle the longest makes room (never one that has not sent its request
    // yet, a WebSocket or an upload)
    size_t victim = conns_.size();
    for (size_t i = 0; i < conns_.size(); i++)
      if (evictable(*conns_[i]) && (victim == conns_.size() || conns_[i]->last_io < conns_[victim]->last_io)) victim = i;
    if (victim == conns_.size()) { c->close(); delete c; return; }
    drop(victim);
  }
  std::unique_ptr<Client> cl(new Client());
  cl->conn.reset(c);
  cl->last_io = cl->last_tx = now;
  conns_.push_back(std::move(cl));
}

void HttpServer::drop(size_t i) {
  Client& c = *conns_[i];
  if (c.ws) app_.sessionClosed(c.session);
  if (c.upload && !c.upload_refused) app_.updateAbort();  // the connection ended before the whole image
  if (big_ == &c) big_ = nullptr;
  c.conn->close();
  conns_.erase(conns_.begin() + i);
}

void HttpServer::send(Client& c, std::string s) {
  if (c.out.empty()) c.last_tx = now_;
  Segment g;
  g.data = std::make_shared<const std::string>(std::move(s));
  c.out.push_back(std::move(g));
}

void HttpServer::sendStatic(Client& c, const uint8_t* p, size_t n) {
  if (c.out.empty()) c.last_tx = now_;
  Segment g;
  g.ptr = p;
  g.len = n;
  c.out.push_back(std::move(g));
}

// At most one state frame waits per client: a newer one replaces it.
void HttpServer::sendState(Client& c, const std::shared_ptr<const std::string>& frame) {
  for (auto& g : c.out)
    if (g.state && g.off == 0) { g.data = frame; return; }
  if (c.out.empty()) c.last_tx = now_;
  Segment g;
  g.data = frame;
  g.state = true;
  c.out.push_back(std::move(g));
}

// What waits behind the segment being sent (static bytes in flash cost nothing; each segment some heap).
size_t HttpServer::queuedBehind(const Client& c) {
  size_t n = 0;
  for (size_t i = 1; i < c.out.size(); i++) n += (c.out[i].data ? c.out[i].data->size() : 0) + 64;
  return n;
}

void HttpServer::respond(Client& c, const char* status, const char* type, const std::string& body, bool close, const char* cors,
                         const char* encoding, const uint8_t* static_body, size_t static_len, bool head_only, const char* etag) {
  char num[24];
  (void)snprintf(num, sizeof num, "%u", (unsigned)(static_body ? static_len : body.size()));
  std::string h = "HTTP/1.1 ";
  h += status;
  h += "\r\nContent-Type: ";
  h += type;
  h += "\r\nContent-Length: ";
  h += num;
  // the page's files: kept by the browser and checked again each time by their ETag (a few hundred bytes instead of
  // 25 KB per load); everything else is never stored
  if (etag) { h += "\r\nCache-Control: no-cache\r\nETag: "; h += etag; h += "\r\n"; }
  else h += "\r\nCache-Control: no-store\r\n";
  if (encoding) {
    h += "Content-Encoding: ";
    h += encoding;
    h += "\r\nVary: Accept-Encoding\r\n";
    // the page is never shown inside another site's frame (a framed page could be clicked into Restart)
    h += "X-Frame-Options: DENY\r\nContent-Security-Policy: frame-ancestors 'none'\r\n";
  }
  if (cors) {
    h += "Access-Control-Allow-Origin: ";
    h += cors;
    h += "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, X-Proptroller-Sha256, X-Proptroller-Pin\r\nVary: Origin\r\n";
  }
  h += close ? "Connection: close\r\n\r\n" : "Connection: keep-alive\r\n\r\n";
  if (static_body) {
    send(c, std::move(h));
    if (!head_only) sendStatic(c, static_body, static_len);  // HEAD: the same headers (the real length), no body
  } else {
    send(c, h + body);
  }
  if (close) c.closing = true;
}

bool HttpServer::flush(Client& c) {
  while (!c.out.empty()) {
    Segment& g = c.out.front();
    const uint8_t* p = g.ptr ? g.ptr : (const uint8_t*)g.data->data();
    size_t len = g.ptr ? g.len : g.data->size();
    if (g.off < len) {
      int n = c.conn->write(p + g.off, len - g.off);
      if (n < 0) return false;
      if (n > 0) c.last_tx = now_;
      g.off += (size_t)n;
      if (g.off < len) return true;  // the socket is full; try again next poll
    }
    c.out.pop_front();
  }
  return true;
}

// POST /api/v1/update: what has arrived goes to the firmware; a refusal is answered after the rest of the body
// was read and dropped (so the client is not cut off while it still sends), and the connection closes after
// the answer.
void HttpServer::serveUpload(Client& c) {
  size_t n = c.in.size() < c.upload_left ? c.in.size() : c.upload_left;
  if (n > 0) {
    if (!c.upload_refused && !app_.updateWrite((const uint8_t*)c.in.data(), n, c.upload_answer)) c.upload_refused = true;
    c.in.erase(0, n);
    c.upload_left -= n;
  }
  if (c.upload_left > 0) return;
  c.upload = false;
  std::string answer = c.upload_refused ? c.upload_answer : app_.updateFinish();
  std::string().swap(c.upload_answer);
  respond(c, "200 OK", "application/json", answer, true, c.upload_cors);
}

void HttpServer::serveHttp(Client& c, Millis now) {
  if (c.upload) { serveUpload(c); return; }
  for (;;) {
    if (c.closing) return;
    size_t end = c.in.find("\r\n\r\n");
    if (end == std::string::npos) {
      if (c.in.size() > limits_.max_header) respond(c, "431 Request Header Fields Too Large", "text/plain", "header too large", true, nullptr);
      return;
    }
    HttpHead h;
    if (!parseHead(c.in.substr(0, end), h)) {
      respond(c, "400 Bad Request", "text/plain", "bad request", true, nullptr);
      return;
    }
    if (h.has_host && !hostOk(h.host)) {
      respond(c, "403 Forbidden", "text/plain", "unknown host name: open the board by its address or <name>.local", true, nullptr);
      return;
    }
    if (!originOk(h) && (h.upgrade_ws || (h.method != "GET" && h.method != "HEAD"))) {
      respond(c, "403 Forbidden", "text/plain", "a page from another site may not control this board", true, nullptr);
      return;
    }
    const char* cors = nullptr;
    if (h.has_origin)
      for (auto o : ALLOWED_ORIGINS)
        if (h.origin == o) cors = o;
    if (h.method == "POST" && h.path == "/api/v1/update") {
      c.in.erase(0, end + 4);
      requests++;
      c.served++;
      c.upload = true;
      c.upload_left = h.content_length;
      c.upload_cors = cors;
      UpdateRequest q;
      q.has_len = h.has_length;
      q.len = h.content_length;
      q.octet_stream = h.octet_stream;
      q.has_sha256 = h.has_sha256;
      q.sha256 = h.sha256;
      q.has_pin = h.has_pin;
      q.pin = h.pin;
      c.upload_answer.clear();
      c.upload_refused = !app_.updateBegin(q, c.upload_answer);
      serveUpload(c);
      return;
    }
    if (h.content_length > limits_.max_body) {
      respond(c, "413 Payload Too Large", "text/plain", "too large", true, nullptr);
      return;
    }
    // one large body at a time (a backup, a script): it is held whole until it is answered
    if (h.content_length > limits_.small_body && big_ != &c) {
      if (big_) {
        respond(c, "503 Service Unavailable", "text/plain", "busy: another large request is arriving, try again", true, cors);
        return;
      }
      big_ = &c;
      c.last_io = now;  // large_idle_ms counts from here
    }
    size_t used = end + 4 + h.content_length;
    if (c.in.capacity() < used) c.in.reserve(used);  // once, not doubling as it arrives
    if (c.in.size() < used) return;  // wait for the body
    requests++;
    c.served++;
    bool close = h.close;
    if (h.method == "GET" && h.path == "/api/v1/ws" && h.upgrade_ws) {
      size_t open = 0;
      for (auto& x : conns_) open += x->ws ? 1 : 0;
      if (!h.has_ws_key) respond(c, "400 Bad Request", "text/plain", "missing key", true, nullptr);
      else if (open >= limits_.max_ws) respond(c, "503 Service Unavailable", "text/plain", "too many pages open on this board", true, nullptr);
      else {
        send(c, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + ws::acceptKey(h.ws_key) + "\r\n\r\n");
        c.ws = true;
        c.session = next_session_++;
        if (next_session_ == 0) next_session_ = 1;
        c.last_ping = now;
        std::vector<std::string> ignore;
        c.seq = app_.eventsSince(0, ignore);
        c.last_state_at = 0;
        c.sent_state = false;
      }
      c.in.erase(0, used);
      endLarge(c);
      return;
    }
    if (h.method == "OPTIONS") respond(c, "204 No Content", "text/plain", "", close, cors);
    else if (h.method == "GET" && h.path == "/api/v1/info") respond(c, "200 OK", "application/json", app_.infoJson(), close, cors);
    else if (h.method == "GET" && h.path == "/api/v1/state") respond(c, "200 OK", "application/json", app_.stateJson(), close, cors);
    else if (h.method == "POST" && h.path == "/api/v1/rpc") {
      if (!h.json) respond(c, "415 Unsupported Media Type", "text/plain", "Content-Type must be application/json", close, cors);
      else respond(c, "200 OK", "application/json", app_.call(c.in.data() + end + 4, h.content_length), close, cors);  // parsed in place
    } else if (h.method == "GET" || h.method == "HEAD") {
      std::string p = h.path == "/" ? "/index.html" : h.path;
      const WebAsset* a = app_.asset(p.c_str());
      if (!a) respond(c, "404 Not Found", "text/plain", "not found", close, nullptr);
      else if (!h.if_none_match.empty() && h.if_none_match == a->etag) respond(c, "304 Not Modified", a->type, "", close, nullptr, "gzip", nullptr, 0, false, a->etag);
      else respond(c, "200 OK", a->type, "", close, nullptr, "gzip", a->gz, a->gz_len, h.method == "HEAD", a->etag);
    } else {
      respond(c, "405 Method Not Allowed", "text/plain", "method not allowed", close, cors);
    }
    c.in.erase(0, used);
    endLarge(c);
  }
}

// The state the WebSocket clients get this poll: built once, shared by all; hash leaves out what changes on its own
// (uptime_ms), so an idle board pushes nothing but the 5 s keep-alive.
const HttpServer::SharedState& HttpServer::sharedState() {
  if (!state_.frame) {
    std::string st = stateEvent(app_.stateJson());
    state_.hash = stateHash(st);
    state_.frame = std::make_shared<const std::string>(ws::encode(ws::TEXT, st));
  }
  return state_;
}

void HttpServer::serveWs(Client& c, Millis now) {
  // complete frames in, unmasked and handled where they lie in the input buffer
  for (;;) {
    uint8_t op = 0;
    bool fin = false;
    size_t head = 0, len = 0;
    long total = ws::frame((const uint8_t*)c.in.data(), c.in.size(), limits_.max_body, true, op, fin, head, len);
    if (total == 0) break;
    if (total < 0) {
      send(c, ws::encode(ws::CLOSE, std::string("\x03\xea", 2)));
      c.closing = true;
      return;
    }
    if (len > limits_.small_body && big_ != &c) {
      if (big_) break;  // another large message first: this one waits in the socket
      big_ = &c;
      c.last_io = now;  // large_idle_ms counts from here
    }
    if (c.in.capacity() < (size_t)total) c.in.reserve((size_t)total);  // once, not doubling as it arrives
    if (c.in.size() < (size_t)total) break;
    char* p = &c.in[head];
    for (size_t k = 0; k < len; k++) p[k] = (char)(p[k] ^ c.in[head - 4 + k % 4]);
    c.last_io = now;
    if (op == ws::PING) send(c, ws::encode(ws::PONG, std::string(p, len)));
    else if (op == ws::CLOSE) { send(c, ws::encode(ws::CLOSE, std::string(p, len))); c.closing = true; }
    else if (op == ws::TEXT || op == ws::BINARY) { requests++; send(c, ws::encode(ws::TEXT, app_.callFrom(c.session, p, len))); }
    c.in.erase(0, (size_t)total);
    endLarge(c);
    if (c.closing) return;
  }
  // heartbeat: a session that sends nothing (not even a pong) for wsHeartbeatMs is over
  uint32_t hb = app_.wsHeartbeatMs();
  if (hb) {
    if (now - c.last_io >= hb) {
      send(c, ws::encode(ws::CLOSE, std::string("\x03\xe9", 2)));
      c.closing = true;
      return;
    }
    if (now - c.last_ping >= 2000) {
      send(c, ws::encode(ws::PING, ""));
      c.last_ping = now;
    }
  }
  // events and state out, to every client in the same poll (the state is built once)
  if (!push_ && c.sent_state) return;
  std::vector<std::string> evs;
  c.seq = app_.eventsSince(c.seq, evs);
  for (auto& e : evs) send(c, ws::encode(ws::TEXT, e));
  if (!c.sent_state || now - c.last_state_at >= limits_.ws_state_min_ms) {
    const SharedState& s = sharedState();
    if (!c.sent_state || s.hash != c.state_hash || now - c.last_state_at >= limits_.ws_state_ms) {
      sendState(c, s.frame);
      c.state_hash = s.hash;
      c.last_state_at = now;
      c.sent_state = true;
    }
  }
}

void HttpServer::poll(Millis now) {
  now_ = now;
  push_ = now - last_push_ >= limits_.ws_poll_ms;
  if (push_) last_push_ = now;
  state_.frame.reset();
  // what the clients hold unread: one large request, the rest small and within inbound_budget together
  size_t small_in = 0;
  for (auto& c : conns_)
    if (c.get() != big_ && !c->upload) small_in += c->in.size();
  for (size_t i = 0; i < conns_.size();) {
    Client& c = *conns_[i];
    bool dead = false;
    bool large = &c == big_ || c.upload;
    size_t cap = (large ? limits_.max_body : limits_.small_body) + limits_.max_header;
    for (int k = 0; k < 16 && !c.closing && c.in.size() < cap && (large || small_in < limits_.inbound_budget); k++) {
      int n = c.conn->read(rx_, sizeof rx_);
      if (n < 0) { dead = true; break; }
      if (n == 0) break;
      c.in.append((const char*)rx_, (size_t)n);
      c.last_io = now;
      if (!large) small_in += (size_t)n;
    }
    size_t before = c.in.size();
    bool was_closing = c.closing;
    if (!dead && !c.closing) {
      if (c.ws) serveWs(c, now);
      else {
        serveHttp(c, now);
        if (c.ws) serveWs(c, now);  // just upgraded: the state goes out at once
      }
    }
    if (!large && c.in.size() < before) small_in -= before - c.in.size();
    if (c.closing && !was_closing) c.last_tx = now;  // closing_ms counts from here
    if (!dead && !flush(c)) dead = true;
    if (!dead && c.out.empty() && c.closing) dead = true;
    // a client that does not read: more than max_queue waiting behind what it is being sent, or no progress
    if (!dead && queuedBehind(c) > limits_.max_queue) dead = true;
    if (!dead && !c.out.empty() && now - c.last_tx > (c.closing ? limits_.closing_ms : limits_.stall_ms)) dead = true;
    if (!dead && !c.ws && c.out.empty() && now - c.last_io > (c.upload ? limits_.upload_idle_ms : limits_.idle_ms)) dead = true;
    if (!dead && &c == big_ && now - c.last_io > limits_.large_idle_ms) dead = true;
    if (dead) drop(i);
    else i++;
  }
}

}  // namespace pt
