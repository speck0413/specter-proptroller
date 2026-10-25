// The generic MQTT topic map (proptroller/<name>/...), the cases of proptroller-sim/tests/mqtt.rs run on
// the topic layer directly (no broker): what a connected client would publish and answer.
#include <unity.h>
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static const MqttMsg* find(const std::vector<MqttMsg>& v, const std::string& topic) {
  for (auto& m : v)
    if (m.topic == topic) return &m;
  return nullptr;
}
// the message, or the test fails here
static const MqttMsg& msg(const std::vector<MqttMsg>& v, const std::string& topic) {
  const MqttMsg* m = find(v, topic);
  if (!m) TEST_FAIL_MESSAGE(("no message on " + topic).c_str());
  return *m;
}

static Sim* board(const char* name) {
  Sim* s = new Sim("mq");
  JsonDocument d;
  d["op"] = "set_params";
  d["values"][0]["key"] = "name";
  d["values"][0]["value"] = name;
  d["values"][1]["key"] = "mqtt_broker";
  d["values"][1]["value"] = "127.0.0.1";
  std::string j;
  serializeJson(d, j);
  TEST_ASSERT_TRUE(s->request(j).isOk());
  return s;
}

void generic_firmware_over_mqtt() {
  Sim* sim = board("Front Door");
  sim->addExample();  // a fresh board has no shows
  Controller& c = sim->ctl();
  std::string host;
  uint16_t port;
  TEST_ASSERT_TRUE(c.mqttTarget(host, port) && host == "127.0.0.1" && port == 1883);
  TEST_ASSERT_EQUAL_STRING("proptroller/front-door/online", c.mqttWill().topic.c_str());
  TEST_ASSERT_EQUAL_STRING("false", c.mqttWill().payload.c_str());
  auto subs = c.mqttSubscriptions();
  TEST_ASSERT_EQUAL(11, subs.size());  // and status/get/+, script/set, script/check, config/now/+, link/answer (board link)
  TEST_ASSERT_EQUAL_STRING("proptroller/front-door/config/now/+", subs[9].c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller/front-door/link/answer", subs[10].c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller/broadcast/getName", subs[0].c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller/front-door/config/set/+", subs[3].c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller/front-door/control/+", subs[5].c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller/front-door/status/get/+", subs[6].c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller/front-door/script/set", subs[7].c_str());
  const std::string p = "proptroller/front-door";
  auto hello = c.mqttOnConnect();
  TEST_ASSERT_EQUAL_STRING("true", msg(hello, p + "/online").payload.c_str());
  TEST_ASSERT_TRUE(msg(hello, p + "/online").retain);
  TEST_ASSERT_EQUAL_STRING("15", msg(hello, p + "/config/ambient_volume").payload.c_str());
  TEST_ASSERT_NOT_NULL(find(hello, p + "/state"));
  TEST_ASSERT_NULL_MESSAGE(find(hello, p + "/config/wifi_pass"), "secrets never published");
  auto a = c.mqttOnMessage("proptroller/broadcast/getName", "");
  TEST_ASSERT_TRUE(a.size() == 1 && a[0].topic == "proptroller/broadcast/name" && a[0].payload == "front-door");
  TEST_ASSERT_TRUE(c.mqttOnMessage("proptroller/discover", "").empty());
  auto r = c.mqttOnMessage(p + "/config/set/ambient_volume", "20");
  TEST_ASSERT_TRUE(r.size() == 1 && r[0].topic == p + "/config/ambient_volume" && r[0].payload == "20");
  TEST_ASSERT_EQUAL(20, c.settings().ambient_volume);
  r = c.mqttOnMessage(p + "/config/set/ambient_volume", "99");
  TEST_ASSERT_TRUE(r[0].topic == p + "/error" && r[0].payload.find("ambient_volume") != std::string::npos);
  r = c.mqttOnMessage(p + "/config/set/ambient", "yes");
  TEST_ASSERT_TRUE(r[0].payload.find("true or false") != std::string::npos);
  r = c.mqttOnMessage(p + "/config/get/trigger_volume", "");
  TEST_ASSERT_EQUAL_STRING("25", r[0].payload.c_str());
  r = c.mqttOnMessage(p + "/rpc", R"({"v":1,"id":5,"op":"run","show":1})");
  TEST_ASSERT_EQUAL_STRING(R"({"v":1,"id":5,"ok":true,"result":{"kind":"done"}})", r[0].payload.c_str());
  for (int i = 0; i < 120; i++) sim->tick();
  auto polled = c.mqttPoll();
  bool started = false, running = false;
  for (auto& m : polled) {
    started |= m.topic == p + "/event" && m.payload.find("started") != std::string::npos;
    running |= m.topic == p + "/state" && m.payload.find("\"state\":\"running\"") != std::string::npos;
  }
  TEST_ASSERT_TRUE(started && running);
  r = c.mqttOnMessage(p + "/config/set/wifi_pass", "s3cret!");
  TEST_ASSERT_TRUE(r.empty());
  r = c.mqttOnMessage(p + "/config/get/wifi_pass", "");
  TEST_ASSERT_TRUE(r.empty());
  for (auto& m : c.mqttPoll()) TEST_ASSERT_TRUE(m.payload.find("s3cret!") == std::string::npos);
  sim->request(R"({"op":"put_show","data":{"id": 7, "name": "On cue", "triggers": [{"on":"cue","name":"lights"}], "tracks": [{"target": "OUT3", "cues": [{"at": 0, "do": "pulse", "ms": 2000}]}]}})");
  c.mqttOnMessage(p + "/cue", "lights\n");
  sim->tick();
  TEST_ASSERT_EQUAL(1000, sim->level(Channel::Out3));
  sim->request(R"({"op":"set_params","values":[{"key":"pin","value":"123456"},{"key":"page_lock","value":true}]})");
  r = c.mqttOnMessage(p + "/config/set/ambient_volume", "15");
  TEST_ASSERT_TRUE(r[0].topic == p + "/error" && r[0].payload.find("page is locked") != std::string::npos);
  r = c.mqttOnMessage(p + "/control/restart", "");
  TEST_ASSERT_TRUE(r[0].payload.find("page is locked") != std::string::npos && !c.rebootRequested());
  sim->request(R"({"op":"set_params","pin":"123456","values":[{"key":"page_lock","value":false}]})");
  TEST_ASSERT_TRUE(c.mqttOnMessage(p + "/control/restart", "").empty() && c.rebootRequested());
  delete sim;
}

void state_at_most_once_a_second() {
  Sim* sim = board("Bench");
  Controller& c = sim->ctl();
  c.mqttSubscriptions();
  c.mqttOnConnect();
  int states = 0;
  for (int i = 0; i < 300; i++) {
    sim->tick();
    for (auto& m : c.mqttPoll()) states += m.topic == "proptroller/bench/state";
  }
  TEST_ASSERT_TRUE_MESSAGE(states <= 3, "an idle board does not republish its state (uptime aside)");
  delete sim;
}

void rename_moves_the_topics() {
  Sim* sim = board("Old Name");
  Controller& c = sim->ctl();
  c.mqttSubscriptions();
  c.mqttOnConnect();
  uint32_t e0 = c.mqttEpoch();
  c.mqttOnMessage("proptroller/old-name/config/set/name", "New Name");
  TEST_ASSERT_TRUE_MESSAGE(c.mqttPoll().empty(), "nothing more on the old connection");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(e0, c.mqttEpoch(), "the session ends");
  TEST_ASSERT_EQUAL_STRING("proptroller/new-name/online", c.mqttWill().topic.c_str());
  c.mqttSubscriptions();
  auto r = c.mqttOnMessage("proptroller/new-name/config/get/ambient_volume", "");
  TEST_ASSERT_EQUAL_STRING("proptroller/new-name/config/ambient_volume", r[0].topic.c_str());
  TEST_ASSERT_TRUE(c.mqttOnMessage("proptroller/old-name/config/get/ambient_volume", "").empty());
  delete sim;
}

// A minimal broker: MQTT topic filters (+ and #), routing between in-process clients.
static bool matches(const std::string& filter, const std::string& topic) {
  size_t f = 0, t = 0;
  while (f < filter.size()) {
    size_t fe = filter.find('/', f), te = topic.find('/', t);
    std::string fl = filter.substr(f, fe == std::string::npos ? std::string::npos : fe - f);
    if (fl == "#") return true;
    if (t > topic.size()) return false;
    std::string tl = topic.substr(t, te == std::string::npos ? std::string::npos : te - t);
    if (fl != "+" && fl != tl) return false;
    if (fe == std::string::npos) return te == std::string::npos;
    if (te == std::string::npos) return false;
    f = fe + 1;
    t = te + 1;
  }
  return false;
}

struct Broker {
  std::vector<std::pair<Controller*, std::vector<std::string>>> clients;
  std::vector<MqttMsg> log;
  void connect(Controller& c) {
    clients.push_back({&c, c.mqttSubscriptions()});
    for (auto& m : c.mqttOnConnect()) publish(m);
  }
  void publish(const MqttMsg& m) {
    log.push_back(m);
    for (auto& cl : clients)
      for (auto& f : cl.second)
        if (matches(f, m.topic)) {
          for (auto& r : cl.first->mqttOnMessage(m.topic, m.payload)) publish(r);
          break;
        }
  }
  size_t count(const std::string& topic) const {
    size_t n = 0;
    for (auto& m : log) n += m.topic == topic;
    return n;
  }
};

void two_devices_one_broker() {
  Sim* a = board("Door");
  Sim* b = board("Window");
  Broker br;
  br.connect(a->ctl());
  br.connect(b->ctl());
  br.publish(MqttMsg("proptroller/broadcast/getName", "", false));
  TEST_ASSERT_EQUAL(2, br.count("proptroller/broadcast/name"));
  // a setting for one device reaches that device only
  br.publish(MqttMsg("proptroller/door/config/set/ambient_volume", "20", false));
  TEST_ASSERT_EQUAL(20, a->ctl().settings().ambient_volume);
  TEST_ASSERT_EQUAL(15, b->ctl().settings().ambient_volume);
  // nothing is accepted on the broadcast prefix
  br.publish(MqttMsg("proptroller/broadcast/config/set/ambient_volume", "5", false));
  TEST_ASSERT_EQUAL(20, a->ctl().settings().ambient_volume);
  TEST_ASSERT_EQUAL(15, b->ctl().settings().ambient_volume);
  TEST_ASSERT_TRUE(matches("proptroller/+/config/set/+", "proptroller/door/config/set/x") && !matches("proptroller/door/cue", "proptroller/window/cue"));
  // no board can be named broadcast, or carry a wildcard (SM-N)
  Sim* c = board("Attic");
  for (const char* bad : {"Broadcast", "a+b", "a#", "a/b"}) {
    std::string j = std::string(R"({"op":"set_params","values":[{"key":"name","value":")") + bad + "\"}]}";
    Response r = c->request(j);
    TEST_ASSERT_FALSE_MESSAGE(r.isOk(), bad);
    TEST_ASSERT_TRUE(r.errorMessage().find("cannot be used") != std::string::npos);
  }
  // three boards: a set reaches one, a control reaches one
  br.connect(c->ctl());
  br.publish(MqttMsg("proptroller/broadcast/getName", "", false));
  TEST_ASSERT_EQUAL(5, br.count("proptroller/broadcast/name"));
  br.publish(MqttMsg("proptroller/attic/config/set/ambient_volume", "10", false));
  br.publish(MqttMsg("proptroller/+/control/restart", "", false));
  br.publish(MqttMsg("proptroller/broadcast/control/restart", "", false));
  TEST_ASSERT_TRUE(!a->ctl().rebootRequested() && !b->ctl().rebootRequested() && !c->ctl().rebootRequested());
  TEST_ASSERT_TRUE(a->ctl().settings().ambient_volume == 20 && b->ctl().settings().ambient_volume == 15 && c->ctl().settings().ambient_volume == 10);
  delete a;
  delete b;
  delete c;
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(generic_firmware_over_mqtt);
  RUN_TEST(state_at_most_once_a_second);
  RUN_TEST(rename_moves_the_topics);
  RUN_TEST(two_devices_one_broker);
  return UNITY_END();
}
