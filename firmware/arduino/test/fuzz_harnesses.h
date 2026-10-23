// The fuzz targets: each takes any bytes and must neither crash nor leak nor trip a sanitizer. libFuzzer drives
// them (src/fuzz_main.cpp, the fuzz_* environments, tools/fuzz.sh); test/test_fuzz_corpus replays the seeds and
// every input that once failed (test/fuzz-regressions/<target>/) through the same functions in the Unity tests.
#pragma once
#include <stdint.h>
#include <string.h>
#include <map>
#include <memory>
#include <string>
#include "proptroller/net/http.h"
#include "proptroller/password.h"
#include "proptroller/script.h"
#include "proptroller/script_record.h"
#include "proptroller/sim/sim.h"
#include "proptroller/storage_ab.h"
#include "proptroller/flash_map.h"
#include "proptroller/update.h"
#include "proptroller/lz.h"
#include "proptroller/settings.h"
#include "picoboot.h"
#include "picoboot_map.h"

namespace fuzz {
using namespace pt;

// One simulated board for a whole run (making one per input would be slow); a fresh one now and then, so state
// from earlier inputs cannot pile up.
inline Sim& board(bool fresh = false) {
  static std::unique_ptr<Sim> sim;
  static unsigned uses = 0;
  if (!sim || fresh || ++uses % 512 == 0) sim.reset(new Sim("f0f1f2f3f4f5"));
  return *sim;
}

// A protocol request envelope (JSON) through the parser and the controller, as HTTP, WebSocket, serial and
// MQTT hand them over; then a step, so what the request started runs.
inline int request(const uint8_t* d, size_t n) {
  Sim& s = board();
  std::string in((const char*)d, n);
  (void)s.call(in.data(), in.size());
  s.ctl().setOrigin(Controller::Origin::Session, 1);
  (void)s.ctl().callFrom(1, in.data(), in.size());
  s.ctl().setOrigin(Controller::Origin::Http);
  (void)s.ctl().callSerial(in.data(), in.size());
  (void)s.ctl().mqttOnMessage(s.ctl().mqttPrefix() + "/rpc", in);
  s.tick();
  return 0;
}

// A show script (with a sound list made of the same bytes) through the checker and, when it passes, the runner.
inline int script(const uint8_t* d, size_t n) {
  std::string text((const char*)d, n);
  SoundList sounds;
  std::string why;
  (void)soundListParse(text, sounds, why);
  ScriptBoard b;
  b.dmx_on = n % 2 == 1;
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  ScriptCheck c = scriptCompile(text, sounds, b, *p);
  if (c.ok() && p->effects) {
    ScriptRunner r;
    r.load(p.get(), 0);
    ScriptInputs in;
    std::vector<SoundCommand> snd;
    for (Millis t = 1; t < 6000; t += 7) {
      in.closed[t / 700 % INPUT_COUNT] = (t / 350) % 2 == 0;
      r.sample(t, in);
      if (t % 10 == 0) r.tick(t, in, snd);
    }
  }
  return 0;
}

// A stored show-script record (script_record.h) as power-on reads it: the record and its step table checked
// and, when a table decodes, run as the board would run it; then a board that finds the bytes in its store.
inline int script_record(const uint8_t* d, size_t n) {
  std::string bytes((const char*)d, n);
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  ScriptRecordRead r = scriptRecordDecode(bytes, *p);
  if (r.has_table) {
    ScriptRunner run;
    run.load(p.get(), 0);
    ScriptInputs in;
    std::vector<SoundCommand> snd;
    for (Millis t = 1; t < 4000; t += 7) {
      in.closed[t / 500 % INPUT_COUNT] = (t / 250) % 2 == 0;
      run.sample(t, in);
      if (t % 10 == 0) run.tick(t, in, snd);
    }
    // the table survives its own round trip
    std::string again = scriptTableEncode(*p);
    std::unique_ptr<ScriptProgram> q(new ScriptProgram());
    if (!scriptTableDecode((const uint8_t*)again.data(), again.size(), *q)) abort();
  }
  Sim s("s0s1s2s3s4s5");
  (void)s.scriptFlash().save(bytes);
  s.powerCycle(0);
  s.step(3000);
  s.press(Input::Trig1, 100);
  s.step(100);
  (void)s.request(R"({"op":"get_script"})");
  return 0;
}

struct MemFs : FileIo {
  std::map<std::string, std::string> f;
  bool read(const char* p, std::string& o) override {
    auto it = f.find(p);
    if (it == f.end()) return false;
    o = it->second;
    return true;
  }
  bool write(const char* p, const std::string& v) override { f[p] = v; return true; }
  bool remove(const char* p) override { return f.erase(p) > 0; }
};

// Corrupt, truncated or hostile stored records: the raw LittleFS files (A/B, CRC) and the JSON records behind
// them, read at power-on by a board (settings and shows, the script).
inline int records(const uint8_t* d, size_t n) {
  std::string bytes((const char*)d, n);
  MemFs fs;
  fs.f["/cfg-a.bin"] = bytes;
  fs.f["/cfg-b.bin"] = bytes.substr(n / 2);
  ABStorage ab(fs);
  std::string out;
  (void)ab.load(out);
  JsonDocument doc;
  uint32_t v = 0;
  (void)readRecord(bytes, STORED_VERSION, STORED_MIGRATIONS, STORED_MIGRATION_COUNT, doc, v);
  Sim s("r0r1r2r3r4r5");
  (void)s.flash().save(bytes);
  (void)s.scriptFlash().save(bytes);
  s.powerCycle(0);
  s.step(20);
  (void)s.stateJson();
  return 0;
}

// Raw bytes as HTTP requests on one connection, in pieces, over several polls (heads, bodies, WebSocket frames,
// an update upload).
struct Pipe {
  std::string in, out;
  bool closed = false;
};
struct PipeConn : Conn {
  Pipe& p;
  explicit PipeConn(Pipe& pipe) : p(pipe) {}
  int read(uint8_t* b, size_t n) override {
    if (p.in.empty()) return 0;
    size_t k = p.in.size() < n ? p.in.size() : n;
    memcpy(b, p.in.data(), k);
    p.in.erase(0, k);
    return (int)k;
  }
  int write(const uint8_t* b, size_t n) override {
    if (p.out.size() < 65536) p.out.append((const char*)b, n);
    return (int)n;
  }
  void close() override { p.closed = true; }
};
inline int http(const uint8_t* d, size_t n) {
  Sim& s = board();
  HttpServer srv(s);
  Pipe p;
  srv.add(new PipeConn(p), 0);
  size_t step = n / 3 + 1;
  for (size_t at = 0, k = 0; k < 6; k++) {
    if (at < n) {
      p.in.append((const char*)d + at, std::min(step, n - at));
      at += step;
    }
    srv.poll(k * 50);
  }
  srv.poll(100000);  // the idle timeout closes what is left
  s.ctl().updateAbort();
  return 0;
}

// A backup document restored onto a board (put_backup), and the board's own backup round-tripped.
inline int backup(const uint8_t* d, size_t n) {
  Sim& s = board();
  std::string req = "{\"op\":\"put_backup\",\"data\":" + std::string((const char*)d, n) + "}";
  (void)s.call(req.data(), req.size());
  std::string req2 = "{\"op\":\"put_backup\",\"keep_name\":false,\"data\":" + std::string((const char*)d, n) + "}";
  (void)s.call(req2.data(), req2.size());
  std::string get = R"({"op":"get_backup"})";
  (void)s.call(get.data(), get.size());
  return 0;
}

// Firmware images: the board checks (ESP32 header, Pico vector table and markers, simulator header) and a whole
// update on the simulated board, in chunks, with a right and a wrong SHA-256.
inline int image(const uint8_t* d, size_t n) {
  (void)esp_image::isEsp32App(d, n);
  (void)pico_image::vectorsOk(d, n, PICOW_SLOT_BYTES, pico_image::RP2040_RAM_END);
  (void)pico_image::vectorsOk(d, n, PICO2W_SLOT_BYTES, pico_image::RP2350_RAM_END);
  MarkerScan scan(pico_image::mcuMarker("pico-w"), pico_image::implMarker());
  for (size_t i = 0; i < n; i += 13) scan.add(d + i, std::min<size_t>(13, n - i));
  std::string v, b;
  bool pass = false;
  (void)sim_image::parse(std::string((const char*)d, n), v, b, pass);
  Sim s("u0u1u2u3u4u5");
  uint8_t sha[32];
  sha256(d, n, sha);
  for (int wrong = 0; wrong < 2; wrong++) {
    UpdateRequest q;
    q.has_len = true;
    q.len = n;
    q.octet_stream = true;
    q.has_sha256 = true;
    q.sha256 = toHex(sha, 32);
    if (wrong) q.sha256[0] = q.sha256[0] == '0' ? '1' : '0';
    std::string answer;
    if (!s.ctl().updateBegin(q, answer)) continue;
    bool ok = true;
    for (size_t i = 0; i < n && ok; i += 509) ok = s.ctl().updateWrite(d + i, std::min<size_t>(509, n - i), answer);
    if (ok) (void)s.ctl().updateFinish();
    s.step(1200);  // a good image restarts the board into it
  }
  return 0;
}

// An HTTP request head alone (parseHead), as fast as it goes: what serveHttp hands it, up to the blank line.
inline int http_head(const uint8_t* d, size_t n) {
  HttpHead h;
  (void)parseHead(std::string((const char*)d, n), h);
  return 0;
}

// WebSocket frames: the first byte picks the size limit and whether a mask is required; the rest is a stream of
// frames decoded one after another, as serveWs does; and every payload survives encode then decode.
inline int ws_frame(const uint8_t* d, size_t n) {
  if (n < 1) return 0;
  size_t max = d[0] & 1 ? 4096 : 70000;
  bool mask = d[0] & 2;
  const uint8_t* b = d + 1;
  size_t left = n - 1;
  for (int k = 0; k < 64 && left; k++) {
    uint8_t op = 0;
    bool fin = false;
    size_t head = 0, len = 0;
    long total = ws::frame(b, left, max, mask, op, fin, head, len);
    std::string payload;
    long used = ws::decode(b, left, max, mask, op, fin, payload);
    if (total < 0 ? used != -1 : (used != 0 && used != total)) abort();
    if (used <= 0) break;
    if (head + len != (size_t)used || payload.size() != len) abort();
    std::string again = ws::encode(op & 0x0F, payload);
    uint8_t op2 = 0;
    bool fin2 = false;
    std::string back;
    if (ws::decode((const uint8_t*)again.data(), again.size(), SIZE_MAX, false, op2, fin2, back) != (long)again.size() || back != payload ||
        op2 != (op & 0x0F) || !fin2)
      abort();
    b += used;
    left -= (size_t)used;
  }
  (void)ws::acceptKey(std::string((const char*)d, n));
  return 0;
}

// MQTT messages to the board: the first line is the topic (under the board's own prefix when it does not start
// with '/', else as it is), the rest the payload; then the stamp the board puts on what it publishes.
inline int mqtt(const uint8_t* d, size_t n) {
  Sim& s = board();
  std::string in((const char*)d, n);
  size_t nl = in.find('\n');
  std::string topic = in.substr(0, nl), payload = nl == std::string::npos ? "" : in.substr(nl + 1);
  std::string full = !topic.empty() && topic[0] == '/' ? topic.substr(1) : s.ctl().mqttPrefix() + "/" + topic;
  (void)s.ctl().mqttOnMessage(full, payload);
  s.tick();
  (void)s.ctl().mqttPoll();
  uint32_t seq = 0;
  (void)mqttStamp(full, payload, "1a2b3c4d", seq);
  (void)mqttStamp(s.ctl().mqttPrefix() + "/state", payload, "1a2b3c4d", seq);
  (void)mqttStamp(s.ctl().mqttPrefix() + "/event", payload, "1a2b3c4d", seq);
  return 0;
}

// A profile (alias table) loaded with set_profile, then every alias used: config get and set over MQTT with
// awkward numbers, controls, status values, get_params; and the table round-trips through its JSON.
inline int profile(const uint8_t* d, size_t n) {
  Sim& s = board();
  std::string data((const char*)d, n);
  JsonDocument doc;
  if (deserializeJson(doc, data)) return 0;
  Profile p;
  std::string why;
  if (parseProfile(doc.as<JsonVariantConst>(), p, why)) {
    JsonDocument out;
    p.toJson(out.to<JsonObject>());
    Profile q;
    if (!parseProfile(out.as<JsonVariantConst>(), q, why) || q.aliases.size() != p.aliases.size() || q.topic_prefix != p.topic_prefix) abort();
  }
  std::string req = "{\"op\":\"set_profile\",\"data\":" + data + "}";
  (void)s.call(req.data(), req.size());
  std::string pre = s.ctl().mqttPrefix() + "/";
  static const char* const VALUES[] = {"1", "0", "-1", "true", "x", "9223372036854775807", "-9223372036854775808", "4294967296"};
  for (auto& a : p.aliases) {
    (void)s.ctl().mqttOnMessage(pre + "config/get/" + a.alias, "");
    for (const char* v : VALUES) (void)s.ctl().mqttOnMessage(pre + "config/set/" + a.alias, v);
    (void)s.ctl().mqttOnMessage(pre + "control/" + a.alias, "");
    (void)s.ctl().mqttOnMessage(pre + "status/get/" + a.alias, "");
    std::string set = "{\"op\":\"set_params\",\"values\":{\"" + a.alias + "\":" + VALUES[n % 8] + "}}";
    (void)s.call(set.data(), set.size());
  }
  for (const char* r : {R"({"op":"get_params"})", R"({"op":"get_profile"})", R"({"op":"state"})"}) (void)s.call(r, strlen(r));
  s.tick();
  (void)s.ctl().mqttPoll();
  return 0;
}

// The settings record's JSON (Settings::fromJson, as power-on reads it): what comes out reads back the same, and
// readRecord on the record in every version this firmware might meet (older, current, newer, none).
inline int settings(const uint8_t* d, size_t n) {
  std::string json((const char*)d, n);
  JsonDocument doc;
  if (!deserializeJson(doc, json)) {
    std::unique_ptr<Settings> a(new Settings());
    a->fromJson(doc.as<JsonVariantConst>());
    JsonDocument out;
    a->toJson(out.to<JsonObject>());
    std::unique_ptr<Settings> b(new Settings());
    b->fromJson(out.as<JsonVariantConst>());
    if (!(*a == *b)) abort();
    (void)a->values();
  }
  static const char* const VERSIONS[] = {"0", "1", "2", "4294967295", "-1", "1.5", "\"1\"", "null"};
  for (const char* v : VERSIONS) {
    std::string rec = std::string("{\"version\":") + v + ",\"settings\":" + json + ",\"shows\":[]}";
    JsonDocument r;
    uint32_t ver = 0;
    (void)readRecord(rec, STORED_VERSION, STORED_MIGRATIONS, STORED_MIGRATION_COUNT, r, ver);
  }
  return 0;
}

// The script record's compression: any stream unpacked to any length must fail cleanly or give that length; and
// the input itself packs and unpacks to the same bytes.
inline int lz(const uint8_t* d, size_t n) {
  if (n < 2) return 0;
  size_t want = (size_t)(d[0] | d[1] << 8);
  std::string out;
  if (lzUnpack(d + 2, n - 2, want, out) ? out.size() != want : !out.empty()) abort();
  std::string in((const char*)d, n), packed = lzPack(in);
  if (packed.size() > lzMaxPacked(n)) abort();
  std::string back;
  if (!lzUnpack((const uint8_t*)packed.data(), packed.size(), n, back) || back != in) abort();
  return 0;
}

// The Pico bootloader at power-on over a state sector (and the start of both slots) made of the input, on a small
// simulated flash that refuses every access outside itself; then the firmware's own calls on what is left.
struct BootFlash {
  std::vector<uint8_t> mem;
};
inline int bootRead(void* c, uint32_t off, uint8_t* buf, uint32_t n) {
  auto& m = ((BootFlash*)c)->mem;
  if ((uint64_t)off + n > m.size()) abort();
  memcpy(buf, &m[off], n);
  return 0;
}
inline int bootErase(void* c, uint32_t off, uint32_t n) {
  auto& m = ((BootFlash*)c)->mem;
  if ((uint64_t)off + n > m.size() || off % PICOBOOT_SECTOR || n % PICOBOOT_SECTOR) abort();
  memset(&m[off], 0xFF, n);
  return 0;
}
inline int bootProgram(void* c, uint32_t off, const uint8_t* buf, uint32_t n) {
  auto& m = ((BootFlash*)c)->mem;
  if ((uint64_t)off + n > m.size()) abort();
  for (uint32_t i = 0; i < n; i++) m[off + i] &= buf[i];
  return 0;
}
inline int picoboot(const uint8_t* d, size_t n) {
  const uint32_t active_len = 4 * PICOBOOT_SECTOR;
  BootFlash fl;
  fl.mem.assign(PICOBOOT_DFU_OFFSET(active_len) + PICOBOOT_DFU_LEN(active_len), 0xFF);
  picoboot_layout l = picoboot_layout_for(active_len);
  // the input: the state sector's first bytes, its last 8 (the page count), then the rest over both slots
  size_t head = std::min<size_t>(n, 64);
  memcpy(&fl.mem[l.state], d, head);
  if (n > head) {
    size_t tail = std::min<size_t>(n - head, 8);
    memcpy(&fl.mem[l.state + PICOBOOT_PAGES_AT], d + head, tail);
    for (size_t i = head + tail; i < n; i++) fl.mem[l.active + (i * 37) % (active_len * 2 + PICOBOOT_SECTOR)] = d[i];
  }
  picoboot_flash f = {&fl, bootRead, bootErase, bootProgram};
  std::vector<uint8_t> buf(l.page);
  picoboot_state st;
  (void)picoboot_read_state(&f, &l, &st);
  (void)picoboot_swap_pages(&f, &l);
  (void)picoboot_prepare(&f, &l, buf.data());
  (void)picoboot_may_write_dfu(&f, &l);
  (void)picoboot_mark_booted(&f, &l);
  (void)picoboot_mark_updated(&f, &l, (uint32_t)n * 97, (uint32_t)n * 31);
  (void)picoboot_prepare(&f, &l, buf.data());
  if (n >= 8) (void)picoboot_vectors_ok(d, pico_image::SLOT_BASE, PICOW_SLOT_BYTES, 0x20000000u, pico_image::RP2040_RAM_END);
  return 0;
}

// The serial text console (consoleLine: "key", "key=value", "trigger", ...), one line per input line, as USB
// serial hands them over with no password; the first line also as a UDP discovery probe.
inline int console(const uint8_t* d, size_t n) {
  Sim& s = board();
  std::string in((const char*)d, n);
  (void)isDiscover(in.data(), in.size());
  size_t at = 0;
  for (int k = 0; k < 32 && at <= in.size(); k++) {
    size_t nl = in.find('\n', at);
    std::string line = in.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    (void)s.ctl().consoleLine(line);
    s.tick();
    if (nl == std::string::npos) break;
    at = nl + 1;
  }
  return 0;
}

// A board with the page lock on (a page password set): the input's lines as requests over every way in (the
// JSON protocol, MQTT under the board's prefix as "topic payload", the serial console), most refused or gated.
inline Sim& lockedBoard() {
  static std::unique_ptr<Sim> sim;
  static unsigned uses = 0;
  if (!sim || ++uses % 512 == 0) {
    sim.reset(new Sim("l0l1l2l3l4l5"));
    (void)sim->request(R"({"op":"set_params","values":[{"key":"pin","value":"door-42"},{"key":"page_lock","value":true}]})");
  }
  return *sim;
}
inline int locked(const uint8_t* d, size_t n) {
  Sim& s = lockedBoard();
  std::string in((const char*)d, n);
  size_t at = 0;
  for (int k = 0; k < 16 && at <= in.size(); k++) {
    size_t nl = in.find('\n', at);
    std::string line = in.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    (void)s.call(line.data(), line.size());
    size_t sp = line.find(' ');
    (void)s.ctl().mqttOnMessage(s.ctl().mqttPrefix() + "/" + line.substr(0, sp), sp == std::string::npos ? "" : line.substr(sp + 1));
    (void)s.ctl().consoleLine(line);
    s.tick();
    if (nl == std::string::npos) break;
    at = nl + 1;
  }
  return 0;
}

// HTTP as http does, with the Pico W's smaller limits (3 connections, 2 WebSockets, 8 KB queues), the input
// spread over two connections.
inline int http_small(const uint8_t* d, size_t n) {
  Sim& s = board();
  HttpServer srv(s, HttpLimits::picoW());
  Pipe a, b;
  srv.add(new PipeConn(a), 0);
  srv.add(new PipeConn(b), 0);
  size_t half = n / 2, step = half / 3 + 1;
  for (size_t at = 0, k = 0; k < 8; k++) {
    if (at < half) {
      a.in.append((const char*)d + at, std::min(step, half - at));
      b.in.append((const char*)d + half + at, std::min(step, n - half - at));
      at += step;
    }
    srv.poll(k * 40);
  }
  srv.poll(100000);
  s.ctl().updateAbort();
  return 0;
}

}  // namespace fuzz
