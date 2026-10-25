// What the network layer (HTTP + WebSocket server, UDP discovery, mDNS, MQTT, setup access point) needs from
// a firmware. The generic controller implements it; a dedicated firmware implements its own. The network
// code never knows which one it serves.
#pragma once
#include "proto.h"
#include "web_assets.h"
#include "update.h"
#include "wifiscan.h"
#include <string>
#include <vector>

namespace pt {

struct MqttMsg {
  std::string topic;
  std::string payload;
  bool retain = false;
  MqttMsg() {}
  MqttMsg(const std::string& t, const std::string& p, bool r) : topic(t), payload(p), retain(r) {}
};

struct NetConfig {
  std::string sta_ssid, sta_pass;  // empty ssid = access point only
  std::string ap_ssid, ap_pass;
  std::string hostname;  // mDNS label (hostLabel of the board's name)
  std::string hostname_taken;  // the label to use when another device already answers for hostname (mdnsLabel)
};

// The board link (`tell` lines between boards, PROTOCOL.md "Board link"): what the network loop's peer-to-peer
// side (link.h) needs from the firmware.
struct LinkConfig {
  bool listen = false;  // take tells from other boards peer to peer
  bool send = false;    // this board's tells go out peer to peer (else MQTT, or nowhere)
  std::string group, key, id, name;  // key: link_key (set-only; never logged); name: the board's network name
};
struct LinkOut {        // one of this board's tell lines
  uint32_t seq = 0;
  bool set = false;     // a setting (else an effect)
  std::string to, what, value;
};
struct LinkIn {         // another board's tell
  bool set = false;
  std::string id, from, what, value, via;  // via: "Wi-Fi", "ESP-NOW", "MQTT"
};
struct LinkReply {
  bool ok = false;
  std::string text;
};

class NetApp {
 public:
  virtual ~NetApp() {}
  // One request envelope (JSON) in, one response (JSON) out.
  virtual std::string call(const char* body, size_t len) = 0;
  // The same from a WebSocket session (session > 0; 0 = HTTP, MQTT, serial). A firmware can tie state to
  // the session, for example a permission that ends when the phone goes away.
  virtual std::string callFrom(uint32_t session, const char* body, size_t len) { (void)session; return call(body, len); }
  // A WebSocket session closed, or missed its heartbeat.
  virtual void sessionClosed(uint32_t session) { (void)session; }
  // A line of the USB serial port that is not a JSON request ("key=value", "key", a control): the answer,
  // or "" when the firmware has no console.
  virtual std::string consoleLine(const std::string& line) { (void)line; return ""; }
  // A JSON request from the USB serial port (a permission given over it is a serial one).
  virtual std::string callSerial(const char* body, size_t len) { return call(body, len); }
  // The server pings every WebSocket every 2 s and closes one that sends nothing (not even a pong) for this
  // long, ms (0 = never). Browsers answer pings by themselves.
  virtual uint32_t wsHeartbeatMs() { return 5000; }
  virtual std::string infoJson() = 0;
  virtual std::string stateJson() = 0;
  // Serialized events newer than seq; returns the newest sequence number.
  virtual uint32_t eventsSince(uint32_t seq, std::vector<std::string>& out) = 0;
  // The newest event's number, without copying any (a new page or serial client starts from here)
  virtual uint32_t eventsSeq() { std::vector<std::string> skip; return eventsSince(0, skip); }
  // A static file by path ("/index.html"); default: the embedded phone UI.
  virtual const WebAsset* asset(const char* path);
  virtual std::string announceJson(uint16_t port) = 0;
  virtual std::vector<std::pair<std::string, std::string>> txt() = 0;
  virtual void netChanged(const NetState& n) = 0;
  virtual NetConfig netConfig() = 0;
  // Named cues to broadcast to other controllers (ESP-NOW), and cues that arrived.
  virtual std::vector<std::string> takeCuesOut() { return {}; }
  virtual void cueIn(const std::string&) {}
  // Board link (all optional). linkEpoch and linkWork are read without the lock (atomics): the epoch changes with
  // the settings linkConfig reads; linkWork says tell lines or answers wait, so linkTake is worth the lock.
  virtual uint32_t linkEpoch() { return 0; }
  virtual bool linkWork() { return false; }
  virtual bool linkConfig(LinkConfig& c) { (void)c; return false; }
  virtual std::vector<LinkOut> linkTake() { return {}; }
  virtual LinkReply linkIn(const LinkIn& in) { (void)in; LinkReply r; r.text = "this board takes no tells"; return r; }
  virtual void linkAnswer(uint32_t seq, const std::string& from, bool ok, const std::string& text) { (void)seq; (void)from; (void)ok; (void)text; }
  // Wi-Fi scan (PROTOCOL.md "wifi_scan"; all optional): the network layer says it can scan (scan_now: a first
  // list right away), asks whether a scan is due (true: it starts one now) and hands in what it found
  // (ok = false: the scan failed). The scan itself runs in the network loop, never in the step.
  virtual void wifiScanReady(bool scan_now) { (void)scan_now; }
  virtual bool wifiScanWanted() { return false; }
  virtual void wifiScanDone(const std::vector<WifiNetwork>& found, bool ok) { (void)found; (void)ok; }
  // MQTT (all optional).
  virtual bool mqttTarget(std::string& host, uint16_t& port) { (void)host; (void)port; return false; }
  virtual uint32_t mqttEpoch() { return 0; }
  virtual std::string mqttClientId() { return "proptroller"; }
  virtual MqttMsg mqttWill() { return MqttMsg(); }
  virtual std::vector<MqttMsg> mqttOnConnect() { return {}; }
  virtual std::vector<std::string> mqttSubscriptions() { return {}; }
  virtual std::vector<MqttMsg> mqttOnMessage(const std::string& topic, const std::string& payload) {
    (void)topic; (void)payload;
    return {};
  }
  virtual std::vector<MqttMsg> mqttPoll() { return {}; }
  // Over-the-air updates (POST /api/v1/update, PROTOCOL.md "Firmware updates"). The default: none on this board.
  [[nodiscard]] virtual bool updateBegin(const UpdateRequest& req, std::string& answer);
  [[nodiscard]] virtual bool updateWrite(const uint8_t* data, size_t n, std::string& answer);
  virtual std::string updateFinish();
  virtual void updateAbort() {}
};

// Identity::board and Info::board exist (a firmware built on this library can test for them with #ifdef)
#define PROPTROLLER_HAS_BOARD_ID 1

struct Identity {
  std::string id;          // unique id (MAC hex)
  std::string mcu;         // "esp32-devkit", "pico", "pico-w", "pico2-w", "sim"
  // the board package (BSP) the firmware was built for: "proptroller" (the Proptroller board's own map), or the
  // PROPTROLLER_BOARD_ID of a board support package (Proptroller.cpp)
  std::string board = "proptroller";
  std::string fw_version = PROPTROLLER_FW_VERSION_STR;  // the release (firmware/VERSION)
  std::string fw_build = PROPTROLLER_FW_BUILD;          // the source revision (git short hash)
  std::vector<std::string> extra_caps;  // "sim"
};

// The analog input on expansion S3 (12-bit raw reading).
class AnalogIn {
 public:
  virtual ~AnalogIn() {}
  virtual bool readRaw(uint16_t& out) = 0;
};

class BoardPins;
struct BoardBoot;
struct BuiltinBoard;
struct ChipRules;
class ByteStream;
class PixelOut;
class Storage;

// Everything a firmware gets from the platform (Proptroller::begin builds it for the board it runs on).
struct BoardParts {
  BoardPins* pins = nullptr;
  ByteStream* uart = nullptr;  // the DFPlayer port
  PixelOut* pixels = nullptr;
  Storage* store = nullptr;
  Storage* script_store = nullptr;  // the show script's own record (SHOW-SCRIPT.md)
  Storage* run_store = nullptr;     // the lifetime run count's own record (runs.h)
  class DmxOut* dmx = nullptr;      // the DMX port on expansion S4
  AnalogIn* analog = nullptr;
  UpdateTarget* update = nullptr;   // over-the-air updates (ESP32, Pico W, Pico 2 W); null: none
  // board packages (board_package.h): the package's own record, what the board runs with since power-on, the
  // chip's pin rules and the image's built-in boards (all null: none)
  Storage* board_store = nullptr;
  const BoardBoot* board_boot = nullptr;
  const ChipRules* chip = nullptr;
  const std::vector<BuiltinBoard>* builtins = nullptr;
  Identity identity;
};

// A whole firmware: the generic Controller is one, a dedicated prop firmware is another. Proptroller runs
// it (tick every 10 ms, the network, the USB serial port) without knowing which one it is.
class Firmware : public NetApp {
 public:
  virtual void tick(Millis now) = 0;
  virtual bool rebootRequested() const = 0;
  // How often tick() runs, ms (the generic controller: 10; a firmware that samples inputs faster asks for less).
  virtual uint32_t tickPeriodMs() const { return 10; }
  // Called every 1 ms between ticks: sample inputs that must not miss a bounce (the generic controller samples
  // TRIG1 / TRIG2 here, SM-T1).
  virtual void sample(Millis now) { (void)now; }
  // Bytes from the USB serial port, for a firmware with its own console (return false to let Proptroller
  // speak the JSON-lines protocol instead). `out` is written back.
  virtual bool serialIn(const uint8_t* data, size_t n, std::string& out) { (void)data; (void)n; (void)out; return false; }
};

inline const WebAsset* findWebAsset(const char* path) {
  for (size_t i = 0; i < WEB_ASSET_COUNT; i++) {
    const char* a = WEB_ASSETS[i].path;
    const char* b = path;
    while (*a && *a == *b) { a++; b++; }
    if (*a == 0 && *b == 0) return &WEB_ASSETS[i];
  }
  return nullptr;
}

inline const WebAsset* NetApp::asset(const char* path) { return findWebAsset(path); }
inline bool NetApp::updateBegin(const UpdateRequest&, std::string& answer) { answer = updateUnsupportedJson(); return false; }
inline bool NetApp::updateWrite(const uint8_t*, size_t, std::string& answer) { answer = updateUnsupportedJson(); return false; }
inline std::string NetApp::updateFinish() { return updateUnsupportedJson(); }

}  // namespace pt
