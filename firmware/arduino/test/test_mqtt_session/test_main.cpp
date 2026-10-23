// The MQTT session (net/mqtt.h: the ESP32's and the simulator's) against a fake client with espMqttClient's API:
// reconnect backoff with jitter, a connect that never finishes, the will on a forced end, a publish the client
// cannot take ends the session, a rename ends it, messages from the client's task are answered in poll(),
// boot_id / mqtt_seq on state and events.
#include <unity.h>
#include <functional>
#include "proptroller/net/mqtt.h"
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

struct FakeClient {
  typedef std::function<void(int, const char*, const uint8_t*, size_t, size_t, size_t)> OnMessage;
  OnMessage on_message;
  bool up = false, connecting = false, take = true;
  int connects = 0, forced = 0;
  size_t queue = 0;
  std::string host, id, will_topic, will_payload;
  bool will_retain = false;
  std::vector<std::string> subs;
  std::vector<MqttMsg> sent;
  void onMessage(OnMessage f) { on_message = f; }
  bool connected() const { return up; }
  bool disconnected() const { return !up && !connecting; }
  bool disconnect(bool force) { forced += force; up = connecting = false; return true; }
  uint16_t publish(const char* t, uint8_t, bool r, const uint8_t* p, size_t n) {
    if (!take) return 0;
    sent.emplace_back(t, std::string((const char*)p, n), r);
    return 1;
  }
  size_t queueSize() { return queue; }
  void setServer(const char* h, uint16_t) { host = h; }
  void setClientId(const char* i) { id = i; }
  void setWill(const char* t, uint8_t, bool r, const char* p) { will_topic = t; will_payload = p; will_retain = r; }
  void setKeepAlive(uint16_t) {}
  void setCleanSession(bool) {}
  bool connect() { connects++; connecting = true; return true; }
  uint16_t subscribe(const char* t, uint8_t) { subs.push_back(t); return 1; }
  // the broker answers (in the client's task)
  void accept() { connecting = false; up = true; }
  void fail() { connecting = false; up = false; }
};

static uint32_t g_rand = 0;
static uint32_t fakeRandom() { return g_rand; }

struct Rig {
  Sim sim{"m"};
  MqttSession<FakeClient> s{sim, fakeRandom};
  Rig() {
    s.client.onMessage([this](int, const char* t, const uint8_t* p, size_t n, size_t i, size_t total) { s.piece(t, p, n, i, total); });
    TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"name","value":"Door"},{"key":"mqtt_broker","value":"broker.local"}]})").isOk());
  }
  FakeClient& c() { return s.client; }
};

static bool sentOn(FakeClient& c, const std::string& topic, std::string* payload = nullptr) {
  for (auto& m : c.sent)
    if (m.topic == topic) { if (payload) *payload = m.payload; return true; }
  return false;
}

void connects_with_will_and_publishes_the_session() {
  Rig r;
  r.s.poll(0, true);
  TEST_ASSERT_EQUAL(1, r.c().connects);
  TEST_ASSERT_EQUAL_STRING("broker.local", r.c().host.c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller/door/online", r.c().will_topic.c_str());
  TEST_ASSERT_EQUAL_STRING("false", r.c().will_payload.c_str());
  TEST_ASSERT_TRUE(r.c().will_retain);
  r.c().accept();
  r.s.poll(10, true);
  TEST_ASSERT_TRUE(r.c().subs.size() > 3);
  std::string st, online;
  TEST_ASSERT_TRUE(sentOn(r.c(), "proptroller/door/online", &online));
  TEST_ASSERT_EQUAL_STRING("true", online.c_str());
  TEST_ASSERT_TRUE(sentOn(r.c(), "proptroller/door/state", &st));
  TEST_ASSERT_EQUAL(0, st.find("{\"boot_id\":\"" + r.s.boot_id + "\",\"mqtt_seq\":"));
  // a message from the client's task is answered in poll(), behind the app
  std::string q = R"({"v":1,"id":3,"op":"info"})";
  r.c().on_message(0, "proptroller/door/rpc", (const uint8_t*)q.data(), 10, 0, q.size());  // in two pieces
  r.c().on_message(0, "proptroller/door/rpc", (const uint8_t*)q.data() + 10, q.size() - 10, 10, q.size());
  r.c().sent.clear();
  r.s.poll(20, true);
  std::string rep;
  TEST_ASSERT_TRUE(sentOn(r.c(), "proptroller/door/rpc/reply", &rep));
  TEST_ASSERT_TRUE(rep.find("\"id\":3,\"ok\":true") != std::string::npos);
}

void backoff_5_to_60_s_with_jitter() {
  Rig r;
  g_rand = 0;
  Millis t = 0;
  std::vector<Millis> at;
  for (; t < 300000; t += 100) {
    int before = r.c().connects;
    r.s.poll(t, true);
    if (r.c().connects != before) { at.push_back(t); r.c().fail(); }  // refused at once
  }
  // 0, then 5, 10, 20, 40, 60, 60 ... s apart
  const Millis gaps[] = {5000, 10000, 20000, 40000, 60000, 60000};
  for (size_t i = 0; i < 6; i++) TEST_ASSERT_EQUAL((long)gaps[i], (long)(at[i + 1] - at[i]));
  // jitter: up to a quarter more
  Rig j;
  g_rand = 0xFFFFFFFF;
  j.s.poll(0, true);
  j.c().fail();
  Millis next = 0;
  for (t = 100; t < 20000 && !next; t += 100) {
    j.s.poll(t, true);
    if (j.c().connects == 2) next = t;
  }
  TEST_ASSERT_TRUE(next > 5000 && next <= 5000 + 1250 + 100);
  g_rand = 0;
}

void connect_that_never_finishes_is_dropped() {
  Rig r;
  r.s.poll(0, true);  // connecting, the broker never answers
  for (Millis t = 100; t <= 10000; t += 100) r.s.poll(t, true);
  TEST_ASSERT_EQUAL(0, r.c().forced);
  r.s.poll(10200, true);
  TEST_ASSERT_EQUAL(1, r.c().forced);
}

void failed_publish_or_backlog_ends_the_session() {
  Rig r;
  r.s.poll(0, true);
  r.c().accept();
  r.s.poll(10, true);
  TEST_ASSERT_TRUE(r.s.connected());
  r.c().take = false;
  TEST_ASSERT_FALSE(r.s.publish(MqttMsg("proptroller/door/event", "{}", false)));
  TEST_ASSERT_EQUAL(1, r.c().forced);  // no DISCONNECT: the broker sends the will
  TEST_ASSERT_FALSE(r.s.connected());
  // a backlog the broker does not take
  r.c().take = true;
  for (Millis t = 100; t < 6000 && !r.c().connecting; t += 100) r.s.poll(t, true);
  r.c().accept();
  r.s.poll(6000, true);
  r.c().queue = MQTT_QUEUE_MAX + 1;  // a backlog: fine for a while (a session opens with ~100 messages)
  for (Millis t = 6100; t <= 6000 + MQTT_STALL_MS; t += 100) r.s.poll(t, true);
  TEST_ASSERT_EQUAL(1, r.c().forced);
  r.s.poll(6000 + MQTT_STALL_MS + 200, true);  // not drained for MQTT_STALL_MS
  TEST_ASSERT_EQUAL(2, r.c().forced);
  r.c().queue = 0;
  for (Millis t = 12000; t < 30000 && !r.c().connecting; t += 100) r.s.poll(t, true);
  r.c().accept();
  r.s.poll(30000, true);
  r.c().queue = MQTT_QUEUE_HARD + 1;  // far too many at once
  TEST_ASSERT_FALSE(r.s.publish(MqttMsg("x/y", "1", false)));
  TEST_ASSERT_EQUAL(3, r.c().forced);
}

void rename_ends_the_session_and_no_network_drops_it() {
  Rig r;
  r.s.poll(0, true);
  r.c().accept();
  r.s.poll(10, true);
  for (int i = 0; i < 5; i++) { r.sim.tick(); r.s.poll(20 + i * 100, true); }
  TEST_ASSERT_TRUE(r.sim.request(R"({"op":"set_params","values":[{"key":"name","value":"Porch"}]})").isOk());
  for (int i = 0; i < 5; i++) { r.sim.tick(); r.s.poll(1000 + i * 100, true); }
  TEST_ASSERT_EQUAL(1, r.c().forced);
  TEST_ASSERT_FALSE(r.s.connected());
  Rig w;
  w.s.poll(0, true);
  w.c().accept();
  w.s.poll(10, true);
  w.s.poll(20, false);  // Wi-Fi gone
  TEST_ASSERT_EQUAL(1, w.c().forced);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(connects_with_will_and_publishes_the_session);
  RUN_TEST(backoff_5_to_60_s_with_jitter);
  RUN_TEST(connect_that_never_finishes_is_dropped);
  RUN_TEST(failed_publish_or_backlog_ends_the_session);
  RUN_TEST(rename_ends_the_session_and_no_network_drops_it);
  return UNITY_END();
}
