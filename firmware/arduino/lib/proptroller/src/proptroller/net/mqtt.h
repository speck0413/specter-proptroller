// The MQTT session, shared by the ESP32 (espMqttClientAsync: AsyncTCP's task resolves, connects, sends and
// receives) and the simulator (espMqttClient on POSIX sockets, driven by its own thread). C is a client with
// espMqttClient's API; the session never waits on the broker: connect() and publish() only queue, and what
// arrives is queued by the client's task and handled in poll(), behind the controller lock (app is a LockedApp).
//
// Reconnects 5 s after a lost session, then 10, 20, 40, at most 60 s apart (each with up to a quarter more at
// random); a connect that takes longer than MQTT_CONNECT_GIVE_UP_MS is dropped; a publish the client cannot take,
// or a backlog the broker does not drain (more than MQTT_QUEUE_MAX messages for MQTT_STALL_MS, or MQTT_QUEUE_HARD
// at once), ends the session without a clean disconnect, so the broker sends the will;
// state and events carry boot_id and mqtt_seq (mqttStamp).
#pragma once
#include <deque>
#include <mutex>
#include "http.h"

namespace pt {

static const Millis MQTT_BACKOFF_MIN_MS = 5000, MQTT_BACKOFF_MAX_MS = 60000;
static const Millis MQTT_CONNECT_GIVE_UP_MS = 10000;
static const size_t MQTT_BUFFER = 16384;     // the largest message in or out (an rpc reply: get_params)
static const size_t MQTT_QUEUE_MAX = 16;     // messages not yet sent: more for MQTT_STALL_MS means the broker stalls
static const size_t MQTT_QUEUE_HARD = 300;   // (a session opens with about 100: every setting, the state)
static const Millis MQTT_STALL_MS = 5000;
static const size_t MQTT_INBOX_BYTES = 24 * 1024;

// What other tasks hand to the network loop (the MQTT client's task: messages; the ESP32's Wi-Fi task: ESP-NOW
// cues, topic ""). Bounded: beyond MQTT_INBOX_BYTES a message is dropped.
struct Inbox {
  std::mutex m;
  std::deque<MqttMsg> q;
  size_t bytes = 0;
  void push(const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> g(m);
    if (bytes + topic.size() + payload.size() > MQTT_INBOX_BYTES) return;
    bytes += topic.size() + payload.size();
    q.emplace_back(topic, payload, false);
  }
  bool pop(MqttMsg& out) {
    std::lock_guard<std::mutex> g(m);
    if (q.empty()) return false;
    out = std::move(q.front());
    q.pop_front();
    bytes -= out.topic.size() + out.payload.size();
    return true;
  }
};

template <class C>
class MqttSession {
 public:
  C client;
  Inbox inbox;
  std::string boot_id;
  uint32_t seq = 0;  // mqtt_seq of the last stamped message

  MqttSession(NetApp& app, uint32_t (*random)()) : app_(app), random_(random) {
    char b[9];
    (void)snprintf(b, sizeof b, "%08lx", (unsigned long)random_());
    boot_id = b;
  }

  // From the client's onMessage, in its task: queue the message whole (a long one arrives in pieces).
  void piece(const char* topic, const uint8_t* payload, size_t len, size_t index, size_t total) {
    if (total > MQTT_BUFFER) return;
    if (index == 0) part_.clear();
    part_.append((const char*)payload, len);
    if (index + len >= total) {
      inbox.push(topic, part_);
      std::string().swap(part_);
    }
  }

  bool connected() { return client.connected(); }

  // End the session without a clean disconnect, so the broker publishes the will (online = false).
  void drop() {
    client.disconnect(true);
    trying_ = session_ = false;
  }

  // Publish on the session, if any; one the client cannot take ends the session (the state is latest-wins and
  // retained, so the next session publishes it again).
  bool publish(const MqttMsg& m, bool stamp = true) {
    if (!client.connected()) return false;
    std::string p = stamp ? mqttStamp(m.topic, m.payload, boot_id, seq) : m.payload;
    if (client.publish(m.topic.c_str(), 0, m.retain, (const uint8_t*)p.data(), p.size()) != 0 && client.queueSize() <= MQTT_QUEUE_HARD)
      return true;
    drop();
    return false;
  }

  // Call from the network loop. up: the network is there (Wi-Fi joined).
  void poll(Millis now, bool up) {
    std::string host;
    uint16_t port = 0;
    bool want = app_.mqttTarget(host, port);
    if (!want || !up) {
      if (client.connected() || trying_) drop();
      return;
    }
    if (client.connected() && app_.mqttEpoch() != epoch_) drop();  // renamed or moved: the will is sent
    if (!client.connected()) {
      session_ = false;
      if (trying_ && !client.disconnected()) {  // connecting in the client's task
        if (now - started_ > MQTT_CONNECT_GIVE_UP_MS) drop();
        return;
      }
      trying_ = false;
      if (now < next_try_) return;
      next_try_ = now + backoff_ + random_() % (backoff_ / 4 + 1);
      backoff_ = backoff_ * 2 > MQTT_BACKOFF_MAX_MS ? MQTT_BACKOFF_MAX_MS : backoff_ * 2;
      MqttMsg will = app_.mqttWill();
      host_ = host;
      id_ = app_.mqttClientId();
      epoch_ = app_.mqttEpoch();
      will_topic_ = will.topic;
      will_payload_ = will.payload;
      // the client keeps pointers to these strings
      client.setServer(host_.c_str(), port);
      client.setClientId(id_.c_str());
      client.setWill(will_topic_.c_str(), 0, will.retain, will_payload_.c_str());
      client.setKeepAlive(30);
      client.setCleanSession(true);
      started_ = now;
      trying_ = client.connect();
      return;
    }
    if (client.queueSize() <= MQTT_QUEUE_MAX) backlog_since_ = now;
    else if (now - backlog_since_ > MQTT_STALL_MS) { drop(); return; }
    if (!session_) {
      session_ = true;
      backlog_since_ = now;
      trying_ = false;
      backoff_ = MQTT_BACKOFF_MIN_MS;
      for (auto& s : app_.mqttSubscriptions()) client.subscribe(s.c_str(), 0);
      for (auto& m : app_.mqttOnConnect())
        if (!publish(m)) return;
    }
    MqttMsg in;
    while (client.connected() && inbox.pop(in)) {
      if (in.topic.empty()) { app_.cueIn(in.payload); continue; }
      for (auto& m : app_.mqttOnMessage(in.topic, in.payload))
        if (!publish(m)) break;
    }
    if (now >= next_poll_) {
      next_poll_ = now + 100;
      for (auto& m : app_.mqttPoll())
        if (!publish(m)) break;
    }
  }

 private:
  NetApp& app_;
  uint32_t (*random_)();
  std::string host_, id_, will_topic_, will_payload_, part_;
  uint32_t epoch_ = 0;
  Millis next_try_ = 0, next_poll_ = 0, started_ = 0, backlog_since_ = 0, backoff_ = MQTT_BACKOFF_MIN_MS;
  bool trying_ = false, session_ = false;
};

}  // namespace pt
