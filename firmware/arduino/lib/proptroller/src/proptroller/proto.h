// The control protocol (firmware/PROTOCOL.md): every Proptroller firmware, the simulator, the phone
// web UI and the desktop app speak it. JSON over HTTP / WebSocket, UDP discovery, mDNS.
//
// This file has the shared pieces: constants, setting descriptions and values, request parsing, response
// and event builders, and the JSON writers for the parts of Info / State every firmware uses.
#pragma once
#include "engine.h"
#include "status.h"
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace pt {

static const uint16_t PROTOCOL_VERSION = 1;
static const char* const MDNS_SERVICE = "_proptroller._tcp";
static const uint16_t DISCOVERY_PORT = 47800;
static const uint16_t HTTP_PORT = 80;
// set_output "ms": a manual pulse, at most this long (plain HTTP and MQTT switch an output on only as a pulse)
static const uint32_t MANUAL_PULSE_MAX_MS = 600000;
// test_input: a test press is let go by itself after "ms", and never later than this (a press held from a page that
// stopped answering, a lost let-go): also the longest "ms" taken
static const uint32_t TEST_PRESS_MAX_MS = 30000;

// `{"proptroller":"discover","v":1}`: a UDP discovery probe?
bool isDiscover(const char* data, size_t len);

// Turn a name into a DNS label for the mDNS host name ("Front Door" -> "front-door").
std::string hostLabel(const std::string& name, const std::string& id);
// The board's id in short: its last four hex digits, lower case ("ac2c"), as in the setup hotspot's name.
std::string shortId(const std::string& id);
// The mDNS label a board answers to: `label` itself, or label-<short id> when another device on the network
// already answers for `label` (two boards both still named "Proptroller": the second is proptroller-ac2c.local).
std::string mdnsLabel(const std::string& label, const std::string& id, bool taken);

// ------------------------------------------------------------------ settings

struct Value {
  enum class Type : uint8_t { Bool, Int, Text };
  Type type = Type::Int;
  bool b = false;
  int64_t i = 0;
  std::string s;
  static Value boolean(bool v) { Value x; x.type = Type::Bool; x.b = v; return x; }
  static Value integer(int64_t v) { Value x; x.type = Type::Int; x.i = v; return x; }
  static Value text(const std::string& v) { Value x; x.type = Type::Text; x.s = v; return x; }
  bool operator==(const Value& o) const;
  bool operator!=(const Value& o) const { return !(*this == o); }
  // "true" / "false", a number, or the text (MQTT payloads, serial console).
  std::string plain() const;
  void toJson(JsonObject o, const char* key) const;
  [[nodiscard]] static bool fromJson(JsonVariantConst v, Value& out);
};

struct ParamDesc {
  enum class Kind : uint8_t { Int, Bool, Text, Choice };
  std::string key, label, group;
  Kind kind = Kind::Int;
  int64_t min = 0, max = 0;
  std::string unit;
  uint16_t max_len = 0;
  bool secret = false;
  std::vector<std::string> options;
  std::string help;
  bool reboot = false;

  static ParamDesc integer(const std::string& key, const std::string& label, const std::string& group, int64_t min, int64_t max, const std::string& unit);
  static ParamDesc boolean(const std::string& key, const std::string& label, const std::string& group);
  static ParamDesc text(const std::string& key, const std::string& label, const std::string& group, uint16_t max_len, bool secret);
  static ParamDesc choice(const std::string& key, const std::string& label, const std::string& group, std::initializer_list<const char*> options);
  ParamDesc& withHelp(const std::string& h) { help = h; return *this; }
  ParamDesc& needsReboot() { reboot = true; return *this; }
  // nullptr if fine, otherwise "out of range" / "too long" / "not an option" / "wrong type".
  [[nodiscard]] const char* check(const Value& v) const;
  // Plain words for a value check() refuses ("Supply voltage is set to 30000 mV but the board allows
  // 9000-24000 mV; lower it in Settings → Power"), or "" when it is fine.
  std::string explain(const Value& v) const;
  bool isSecret() const { return kind == Kind::Text && secret; }
  void toJson(JsonObject o) const;
  // Parse an MQTT / serial payload for this kind of setting.
  [[nodiscard]] bool parsePlain(const std::string& payload, Value& out, std::string& err) const;
};

struct ParamValue {
  std::string key;
  Value value;
};

// ------------------------------------------------------------------ device description

struct ActionDesc {
  std::string key, label;
  bool confirm = false;
  bool momentary = false;
  bool hold = false;  // start it with a press held for the page's hold time (setting start_buttons = hold)
};

struct Terminal {
  enum class Kind : uint8_t { InputT, OutputT, SoundT, PixelT, AnalogT };
  std::string id;
  Kind kind;
  std::string label;
  bool has_role = false;
  std::string role;
  bool shows = false;
  bool disabled = false;
  std::string alias;  // a project's name for it from the profile ("REFILL"), shown as "REFILL (TRIG2)"
  std::string display() const { return alias.empty() ? id : alias + " (" + id + ")"; }
  Terminal(const std::string& id_, Kind k, const std::string& label_) : id(id_), kind(k), label(label_) {}
  void setRole(const std::string& r) { has_role = true; role = r; }
};

struct Info {
  std::string id, name, fw, fw_version, fw_impl, fw_build, mcu;
  std::string board = "proptroller";  // the board package (Identity::board)
  // over-the-air updates (present with the `update` capability)
  bool has_update = false;
  std::string update_slot;
  uint32_t update_max_bytes = 0;
  bool update_pending = false, update_rolled_back = false;
  uint32_t update_install_max_s = 0;  // left out when 0
  std::vector<std::string> capabilities;
  std::vector<ActionDesc> actions;
  std::vector<Terminal> terminals;
  bool stop_button = true;  // the page shows STOP (setting stop_button)
  bool has(const char* cap) const;
  void toJson(JsonObject o) const;
  // UDP discovery answer and mDNS TXT records.
  void announceJson(uint16_t port, JsonObject o) const;
  std::string announce(uint16_t port) const;
  std::vector<std::pair<std::string, std::string>> txt() const;
};

struct NetState {
  std::string mode = "down";  // "sta", "ap", "usb", "sim", "down"
  std::string ip, ssid;
  std::string host;  // the mDNS name the board answers to ("proptroller.local"); empty while it has none
  std::string hotspot;  // mode "sta": the board's own Wi-Fi, up at the same time at 192.168.4.1 (empty: none)
  bool has_rssi = false;
  int32_t rssi = 0;
  void toJson(JsonObject o) const;
};

// ------------------------------------------------------------------ requests

enum class ErrorCode : uint8_t { BadRequest, Version, Invalid, NotFound, Refused, Unsupported, Pin, Internal };
const char* errorCodeName(ErrorCode c);

// A parsed and type-checked request envelope ({"v":1,"id":7,"pin":"1234","op":"run","show":1}).
struct Request {
  uint16_t v = 1;
  bool has_id = false;
  uint32_t id = 0;
  bool has_pin = false;
  std::string pin;
  std::string op;
  JsonDocument doc;  // the whole envelope: read the op's fields from here
  bool custom = false;  // an op this library does not know (only with allow_custom_ops): a firmware hook may answer it
  JsonVariantConst operator[](const char* k) const { return doc[k]; }
  // Requests that change something need the PIN when the device has one.
  bool mutates() const;
};

// A response under construction: {"v":1,"id":7,"ok":true,"result":{"kind":"done"}}.
class Response {
 public:
  static Response done(const Request& r) { return Response(r.has_id, r.id, "done"); }
  static Response done(bool has_id, uint32_t id) { return Response(has_id, id, "done"); }
  static Response ok(const Request& r, const char* kind) { return Response(r.has_id, r.id, kind); }
  static Response err(bool has_id, uint32_t id, ErrorCode c, const std::string& message);
  static Response err(const Request& r, ErrorCode c, const std::string& message) { return err(r.has_id, r.id, c, message); }
  static Response invalid(const Request& r, const ShowError& e);
  // The result object (add fields after "kind").
  JsonObject result() { return doc_["result"]; }
  bool isOk() const { return doc_["ok"].as<bool>(); }
  std::string errorMessage() const { return doc_["error"]["message"].as<std::string>(); }
  std::string json() const;
  size_t jsonSize() const;  // the length of json(), without writing it
  JsonDocument& doc() { return doc_; }

 private:
  Response(bool has_id, uint32_t id, const char* kind);
  Response() {}
  JsonDocument doc_;
};

// Parse and type-check a request; on failure `error` holds the response to send back. With
// allow_custom_ops an unknown op is accepted with Request::custom set (the firmware's own messages).
[[nodiscard]] bool parseRequest(const char* body, size_t len, Request& out, Response*& error, bool allow_custom_ops = false);
// The error for a custom op nobody answered (the same as without allow_custom_ops).
Response unknownOp(const Request& r);
// The request's top-level "id" (an unsigned integer), found without parsing the body: for an answer that refuses
// a request before it is parsed (too big for the memory left, a bad alias, broken JSON), so the client can still
// match it. False when there is none.
bool peekRequestId(const char* body, size_t len, uint32_t& id);
// An error answer to a request that was not parsed, with its id when it has one.
Response errorFor(const char* body, size_t len, ErrorCode c, const std::string& message);

// Field readers for request handlers (return false and set `err` when the field is missing or wrong).
[[nodiscard]] bool reqChannel(const Request& r, const char* key, Channel& out);
[[nodiscard]] bool reqInput(const Request& r, const char* key, Input& out);
[[nodiscard]] bool reqTarget(const Request& r, const char* key, Target& out);
[[nodiscard]] bool reqSound(const Request& r, SoundCommand& out);

// ------------------------------------------------------------------ events

// Events kept for WebSocket / MQTT / serial clients: the last 64, already serialized.
class EventLog {
 public:
  static const size_t CAPACITY = 64;
  // {"v":1,"seq":n,"event":<event>} plus "data" when given (raw JSON).
  void push(const char* event, const std::string& data_json = std::string());
  void notice(const Notice& n);
  void log(const std::string& line);
  // Events after `seq` (signed wrap-around compare); returns the newest sequence number.
  uint32_t since(uint32_t seq, std::vector<std::string>& out) const;
  uint32_t seq() const { return seq_; }

 private:
  std::deque<std::pair<uint32_t, std::string>> events_;
  uint32_t seq_ = 0;
};

void noticeToJson(const Notice& n, JsonObject o);
// {"v":1,"seq":0,"event":"state","data":<state>}
std::string stateEvent(const std::string& state_json);
// A hash of a state (or state event) that leaves out what changes on its own (uptime_ms): equal for two states
// that differ only in time, so an idle board pushes nothing new (the WebSocket and the USB serial state stream)
uint64_t stateHash(const std::string& st);

// ------------------------------------------------------------------ state pieces

void patternToJson(const Pattern& p, JsonObject parent, const char* key);
void showStatusToJson(const ShowStatus& s, JsonObject o);
const char* terminalKindName(Terminal::Kind k);

}  // namespace pt
