// The board link (SHOW-SCRIPT.md §11, PROTOCOL.md "Board link"): the frame and its tag, the numbers a receiver
// keeps, and simulated boards telling each other over an in-memory network (the peer-to-peer side, link.h) and
// over MQTT (messages handed from one controller to the other as a broker would): gating, page lock, settings
// that are used and not saved, challenges, replays, groups and keys.
#include <unity.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "proptroller/link.h"
#include "proptroller/sim/sim.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static std::string hex(const uint8_t* b, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (size_t i = 0; i < n; i++) { o += d[b[i] >> 4]; o += d[b[i] & 15]; }
  return o;
}

// RFC 4231 test cases 1, 2 and 6 (a key longer than the block)
void hmac_sha256_matches_rfc_4231() {
  uint8_t out[32];
  std::string k1(20, '\x0b'), d1 = "Hi There";
  hmacSha256((const uint8_t*)k1.data(), k1.size(), (const uint8_t*)d1.data(), d1.size(), out);
  TEST_ASSERT_EQUAL_STRING("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", hex(out, 32).c_str());
  std::string k2 = "Jefe", d2 = "what do ya want for nothing?";
  hmacSha256((const uint8_t*)k2.data(), k2.size(), (const uint8_t*)d2.data(), d2.size(), out);
  TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", hex(out, 32).c_str());
  std::string k6(131, '\xaa'), d6 = "Test Using Larger Than Block-Size Key - Hash Key First";
  hmacSha256((const uint8_t*)k6.data(), k6.size(), (const uint8_t*)d6.data(), d6.size(), out);
  TEST_ASSERT_EQUAL_STRING("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", hex(out, 32).c_str());
}

static LinkFrame sample() {
  LinkFrame f;
  f.kind = LinkFrame::Kind::Set;
  f.boot = 0x11223344;
  f.seq = 7;
  f.bound = 0x55667788;
  f.group = "haunt";
  f.id = "a0b1c2d3e4f5";
  f.name = "front-door";
  f.to = "porch";
  f.what = "pixel_speed";
  f.value = "150";
  return f;
}

void the_frame_round_trip_its_tag_and_its_limits() {
  LinkFrame f = sample(), g;
  std::string plain = linkEncode(f, "");
  TEST_ASSERT_EQUAL(LINK_HEAD + 6 + 5 + 12 + 10 + 5 + 11 + 3, plain.size());
  TEST_ASSERT_EQUAL((int)LinkRead::Ok, (int)linkDecode((const uint8_t*)plain.data(), plain.size(), "", g));
  TEST_ASSERT_EQUAL((int)LinkFrame::Kind::Set, (int)g.kind);
  TEST_ASSERT_EQUAL_HEX32(0x11223344, g.boot);
  TEST_ASSERT_EQUAL(7, g.seq);
  TEST_ASSERT_EQUAL_HEX32(0x55667788, g.bound);
  TEST_ASSERT_EQUAL_STRING("haunt", g.group.c_str());
  TEST_ASSERT_EQUAL_STRING("front-door", g.name.c_str());
  TEST_ASSERT_EQUAL_STRING("150", g.value.c_str());
  // signed: the tag is checked; a frame without one, or with a wrong one, or under another key is refused
  std::string sig = linkEncode(f, "s3cret");
  TEST_ASSERT_EQUAL(plain.size() + LINK_TAG, sig.size());
  TEST_ASSERT_EQUAL((int)LinkRead::Ok, (int)linkDecode((const uint8_t*)sig.data(), sig.size(), "s3cret", g));
  TEST_ASSERT_EQUAL((int)LinkRead::Unsigned, (int)linkDecode((const uint8_t*)plain.data(), plain.size(), "s3cret", g));
  TEST_ASSERT_EQUAL((int)LinkRead::Signed, (int)linkDecode((const uint8_t*)sig.data(), sig.size(), "", g));
  TEST_ASSERT_EQUAL((int)LinkRead::BadTag, (int)linkDecode((const uint8_t*)sig.data(), sig.size(), "other", g));
  std::string bent = sig;
  bent[LINK_HEAD + 1] ^= 1;  // one bit of the group
  TEST_ASSERT_EQUAL((int)LinkRead::BadTag, (int)linkDecode((const uint8_t*)bent.data(), bent.size(), "s3cret", g));
  bent = sig;
  bent[10] = 8;  // another number
  TEST_ASSERT_EQUAL((int)LinkRead::BadTag, (int)linkDecode((const uint8_t*)bent.data(), bent.size(), "s3cret", g));
  // not ours, cut short, a length past the end
  TEST_ASSERT_EQUAL((int)LinkRead::NotLink, (int)linkDecode((const uint8_t*)"{\"proptroller\":1}", 17, "", g));
  TEST_ASSERT_EQUAL((int)LinkRead::Bad, (int)linkDecode((const uint8_t*)plain.data(), 20, "", g));
  bent = plain;
  bent[LINK_HEAD] = 60;
  TEST_ASSERT_EQUAL((int)LinkRead::Bad, (int)linkDecode((const uint8_t*)bent.data(), bent.size(), "", g));
  // the largest frame fits ESP-NOW; a field past its limit is not sent
  f.group = std::string(LINK_GROUP_MAX, 'g');
  f.id = std::string(LINK_ID_MAX, 'i');
  f.name = std::string(LINK_NAME_MAX, 'n');
  f.to = std::string(LINK_NAME_MAX, 't');
  f.what = std::string(LINK_WHAT_MAX, 'w');
  f.value = std::string(LINK_VALUE_MAX, 'v');
  std::string big = linkEncode(f, "k");
  TEST_ASSERT_FALSE(big.empty());
  TEST_ASSERT_TRUE(big.size() <= LINK_FRAME_MAX);
  f.value += "v";
  TEST_ASSERT_TRUE(linkEncode(f, "k").empty());
}

void numbers_seen_run_once_and_late_ones_still_run() {
  LinkSeen s;
  TEST_ASSERT_EQUAL((int)LinkSeen::Fresh::New, (int)s.check("a", 1, 5));
  s.take("a", 1, 5, true, "ok", 0);
  TEST_ASSERT_EQUAL((int)LinkSeen::Fresh::Again, (int)s.check("a", 1, 5));
  TEST_ASSERT_EQUAL((int)LinkSeen::Fresh::New, (int)s.check("a", 1, 4));  // late: blackout after bright still runs
  s.take("a", 1, 4, false, "no", 1);
  TEST_ASSERT_EQUAL((int)LinkSeen::Fresh::Again, (int)s.check("a", 1, 4));
  s.take("a", 1, 40, true, "ok", 2);
  TEST_ASSERT_EQUAL((int)LinkSeen::Fresh::Old, (int)s.check("a", 1, 5));  // out of the window
  TEST_ASSERT_EQUAL((int)LinkSeen::Fresh::New, (int)s.check("a", 2, 5));  // another boot of the sender
  bool ok = false;
  std::string t;
  TEST_ASSERT_TRUE(s.answerOf("a", 1, 40, ok, t));
  TEST_ASSERT_FALSE(s.answerOf("a", 1, 4, ok, t));
  // full: the least recently heard is forgotten
  for (int i = 0; i < LINK_SENDERS; i++) s.take("b" + std::to_string(i), 1, 1, true, "", 10 + i);
  TEST_ASSERT_EQUAL((int)LinkSeen::Fresh::New, (int)s.check("a", 1, 40));
}

// ---------------------------------------------------------------- simulated boards on an in-memory network

static uint32_t rng_state = 1;
static uint32_t testRandom() {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

struct Net;
struct Node : LinkRadio {
  Net* net = nullptr;
  int me = 0;
  std::unique_ptr<Sim> sim;
  std::unique_ptr<LinkPeer> peer;
  std::vector<std::pair<LinkAddr, std::string>> inbox;
  void broadcast(const std::string& frame) override;
  void reply(const std::string& frame, const LinkAddr& to) override;
};
struct Net {
  std::vector<std::unique_ptr<Node>> nodes;
  std::vector<std::string> wire;  // every frame sent, for replays
  bool lose = false;              // drop every frame (an unreachable board)
  Node& add(const char* id, const char* name) {
    nodes.emplace_back(new Node());
    Node& n = *nodes.back();
    n.net = this;
    n.me = (int)nodes.size() - 1;
    n.sim.reset(new Sim(id));
    n.peer.reset(new LinkPeer(testRandom));
    n.sim->request(std::string(R"({"op":"set_params","values":[{"key":"name","value":")") + name + "\"}]}");
    return n;
  }
  void deliver(int from, int to, const std::string& f) {
    if (lose) return;
    LinkAddr a;
    a.port = (uint16_t)from;
    nodes[to]->inbox.push_back({a, f});
  }
  // time goes by in 1 ms steps for every board; frames are handled as they arrive
  void run(Millis ms) {
    for (Millis t = 0; t < ms; t++) {
      for (auto& n : nodes) n->sim->tick1();
      for (auto& n : nodes) {
        Controller& c = n->sim->ctl();
        std::vector<std::pair<LinkAddr, std::string>> in;
        in.swap(n->inbox);
        for (auto& f : in) n->peer->received((const uint8_t*)f.second.data(), f.second.size(), f.first, n->sim->now(), c, *n);
        n->peer->poll(n->sim->now(), c, *n);
      }
    }
  }
};
void Node::broadcast(const std::string& frame) {
  net->wire.push_back(frame);
  for (auto& n : net->nodes)
    if (n->me != me) net->deliver(me, n->me, frame);
}
void Node::reply(const std::string& frame, const LinkAddr& to) {
  net->wire.push_back(frame);
  net->deliver(me, to.port, frame);
}

static Response setScript(Sim& sim, const std::string& text) {
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = text;
  d["sounds"].to<JsonObject>();
  std::string j;
  serializeJson(d, j);
  return sim.request(j);
}
static std::string logs(Sim& s) {
  std::vector<std::string> ev;
  s.ctl().eventsSince(0, ev);
  std::string all;
  for (auto& e : ev) all += e + "\n";
  return all;
}
static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

static const char* const DOORBELL = "effect doorbell:\n  wait for trig1\n  tell porch blackout\n  wait 1\n";
static const char* const PORCH = "effect blackout:\n  wait for trig1\n  out1 = on\n  wait 2\n  out1 = off\n";

// two boards: A (front-door) tells B (porch); B's blackout is free to start remotely
static void pair(Net& net, bool free = true) {
  Node& a = net.add("a1", "Front Door");
  Node& b = net.add("b2", "Porch");
  TEST_ASSERT_TRUE(setScript(*a.sim, DOORBELL).isOk());
  TEST_ASSERT_TRUE(setScript(*b.sim, PORCH).isOk());
  if (free) b.sim->serial("remote_free_effects=blackout");
  net.run(2500);
}
static void ring(Net& net) {
  net.nodes[0]->sim->setInput(Input::Trig1, true);
  net.run(150);
  net.nodes[0]->sim->setInput(Input::Trig1, false);
  net.run(100);
}

void one_board_starts_an_effect_on_another_over_wifi() {
  Net net;
  pair(net);
  Sim& a = *net.nodes[0]->sim;
  Sim& b = *net.nodes[1]->sim;
  ring(net);
  TEST_ASSERT_EQUAL(1000, b.level(Channel::Out1));
  TEST_ASSERT_TRUE_MESSAGE(has(logs(a), "tell porch blackout: sent (Wi-Fi)"), logs(a).c_str());
  TEST_ASSERT_TRUE_MESSAGE(has(logs(b), "remote start by front-door: effect \\\"blackout\\\""), logs(b).c_str());
  net.run(2500);
  TEST_ASSERT_FALSE(has(logs(a), "no answer"));  // it answered
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
}

void a_refused_start_is_said_on_the_sender_and_no_answer_too() {
  Net net;
  pair(net, false);  // blackout needs remote_trigger_enable, which is off
  Sim& a = *net.nodes[0]->sim;
  Sim& b = *net.nodes[1]->sim;
  ring(net);
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
  TEST_ASSERT_TRUE_MESSAGE(has(logs(a), "tell porch blackout: porch refused: blackout: "), logs(a).c_str());
  // the same with remote_trigger_enable on (as the serial console turns it on)
  b.serial("remote_trigger_enable=true");
  net.run(1500);
  ring(net);
  TEST_ASSERT_EQUAL(1000, b.level(Channel::Out1));
  // a board that does not answer
  net.run(3000);
  net.lose = true;
  ring(net);
  net.run(2500);
  TEST_ASSERT_TRUE_MESSAGE(has(logs(a), "tell porch blackout: no answer from porch"), logs(a).c_str());
}

void board_link_off_sends_nothing_and_takes_nothing() {
  Net net;
  pair(net);
  Sim& a = *net.nodes[0]->sim;
  Sim& b = *net.nodes[1]->sim;
  b.serial("board_link=off");
  ring(net);
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
  b.serial("board_link=auto");
  a.serial("board_link=off");
  net.run(3000);
  ring(net);
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
  TEST_ASSERT_TRUE_MESSAGE(has(logs(a), "tell porch blackout: not sent (board_link is off)"), logs(a).c_str());
}

void another_group_is_not_heard() {
  Net net;
  pair(net);
  Sim& b = *net.nodes[1]->sim;
  b.serial("link_group=attic");
  ring(net);
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
  net.nodes[0]->sim->serial("link_group=attic");
  net.run(3000);
  ring(net);
  TEST_ASSERT_EQUAL(1000, b.level(Channel::Out1));
}

void settings_from_another_board_are_used_and_not_saved() {
  Net net;
  Node& a = net.add("a1", "Front Door");
  Node& b = net.add("b2", "Porch");
  TEST_ASSERT_TRUE(setScript(*a.sim, "effect x:\n  wait for trig1\n  tell porch pixel_speed = 150\n  tell porch ambient = off\n"
                                     "  tell porch pixel_brightness = 40%\n  wait 1\n").isOk());
  net.run(2500);
  b.sim->ctl().saveIfDirty();
  std::string before;
  TEST_ASSERT_EQUAL((int)Stored::Ok, (int)b.sim->flash().fetch(before));
  ring(net);
  const Settings& s = b.sim->ctl().settings();
  TEST_ASSERT_EQUAL(150, s.pixel_speed);
  TEST_ASSERT_FALSE(s.ambient);
  TEST_ASSERT_EQUAL(40, s.pixel_brightness);
  TEST_ASSERT_TRUE(has(logs(*b.sim), "front-door (board link, Wi-Fi) set pixel_speed = 150 (until a restart or a change here; not saved)"));
  TEST_ASSERT_EQUAL(3, (int)b.sim->ctl().linkKept().size());
  net.run(20000);  // well past any save delay
  b.sim->ctl().saveIfDirty();
  std::string after;
  TEST_ASSERT_EQUAL((int)Stored::Ok, (int)b.sim->flash().fetch(after));
  TEST_ASSERT_EQUAL_STRING(before.c_str(), after.c_str());  // nothing written
  // a change made here is saved, and with it the stored values of the others, not the told ones
  b.sim->serial("trigger_volume=20");
  b.sim->ctl().saveNow();
  TEST_ASSERT_EQUAL((int)Stored::Ok, (int)b.sim->flash().fetch(after));
  TEST_ASSERT_TRUE(has(after, "\"trigger_volume\":20"));
  TEST_ASSERT_TRUE(has(after, "\"pixel_speed\":100"));
  TEST_ASSERT_TRUE(has(after, "\"ambient\":true"));
  // a backup has the stored values too
  JsonDocument bk;
  b.sim->ctl().backupJson(bk.to<JsonObject>());
  std::string bks;
  serializeJson(bk, bks);
  TEST_ASSERT_TRUE(has(bks, "{\"key\":\"pixel_speed\",\"value\":100}"));
  // a change here of a told setting ends it
  b.sim->serial("pixel_speed=120");
  TEST_ASSERT_EQUAL(2, (int)b.sim->ctl().linkKept().size());
  // a restart: the told values are gone
  b.sim->ctl().saveNow();
  b.sim->powerCycle();
  TEST_ASSERT_EQUAL(120, b.sim->ctl().settings().pixel_speed);
  TEST_ASSERT_TRUE(b.sim->ctl().settings().ambient);
  TEST_ASSERT_EQUAL(100, b.sim->ctl().settings().pixel_brightness);
  // the receiver refuses what a board never takes, whatever the sender's firmware sends
  LinkIn in;
  in.set = true;
  in.from = "x";
  in.via = "Wi-Fi";
  for (const char* k : {"wifi_ssid", "remote_trigger_enable", "page_lock", "name", "mqtt_broker", "link_key", "effect"}) {
    in.what = k;
    in.value = "true";
    LinkReply r = b.sim->ctl().linkIn(in);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, k);
  }
  in.what = "pixel_speed";
  in.value = "900";  // out of range
  TEST_ASSERT_FALSE(b.sim->ctl().linkIn(in).ok);
}

void a_page_locked_board_takes_no_settings_and_gates_starts() {
  Net net;
  pair(net);
  Sim& b = *net.nodes[1]->sim;
  TEST_ASSERT_TRUE(b.request(R"({"op":"set_params","values":[{"key":"pin","value":"secret-1"},{"key":"page_lock","value":true}]})").isOk());
  LinkIn in;
  in.set = true;
  in.from = "front-door";
  in.via = "Wi-Fi";
  in.what = "pixel_speed";
  in.value = "150";
  LinkReply r = b.ctl().linkIn(in);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("the page is locked: use rpc with the page password", r.text.c_str());
  // starts as over MQTT: an effect free of the remote trigger has no gate of its own, so a locked board refuses it;
  // one that needs remote_trigger_enable starts while the enable is on
  ring(net);
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
  TEST_ASSERT_TRUE(has(logs(*net.nodes[0]->sim), "porch refused: the page is locked"));
  b.serial("remote_free_effects=");
  b.serial("remote_trigger_enable=true");
  net.run(1500);
  ring(net);
  TEST_ASSERT_EQUAL(1000, b.level(Channel::Out1));
  // restart is never taken from another board
  in.set = false;
  in.what = "restart";
  b.request(R"({"op":"set_params","pin":"secret-1","values":[{"key":"page_lock","value":false}]})");
  r = b.ctl().linkIn(in);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_FALSE(b.ctl().rebootRequested());
}

void with_a_key_a_challenge_then_it_runs_and_replays_are_dropped() {
  Net net;
  pair(net);
  Sim& a = *net.nodes[0]->sim;
  Sim& b = *net.nodes[1]->sim;
  a.serial("link_key=open-sesame");
  b.serial("link_key=open-sesame");
  net.run(100);
  ring(net);
  TEST_ASSERT_EQUAL(1000, b.level(Channel::Out1));
  TEST_ASSERT_TRUE(net.nodes[1]->peer->challenged >= 1);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", logs(a).find("refused") == std::string::npos ? "" : "refused", logs(a).c_str());
  // the second tell to a known board goes bound at once
  uint32_t ch = net.nodes[1]->peer->challenged;
  net.run(3000);
  ring(net);
  TEST_ASSERT_EQUAL(1000, b.level(Channel::Out1));
  TEST_ASSERT_EQUAL(ch, net.nodes[1]->peer->challenged);
  // every frame on the wire sent again: nothing runs again
  net.run(3000);
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
  uint32_t runs = b.ctl().scriptRunner()->runs(0);
  std::vector<std::string> wire = net.wire;
  for (auto& f : wire) net.deliver(0, 1, f);
  net.run(100);
  TEST_ASSERT_EQUAL(runs, b.ctl().scriptRunner()->runs(0));
  // nor after B restarts (a new nonce: the recorded frames are bound to the old one, or not bound at all)
  net.nodes[1]->peer.reset(new LinkPeer(testRandom));
  b.powerCycle();
  net.run(2500);
  runs = b.ctl().scriptRunner()->runs(0);
  for (auto& f : wire) net.deliver(0, 1, f);
  net.run(100);
  TEST_ASSERT_EQUAL(runs, b.ctl().scriptRunner()->runs(0));
  // a board with another key hears nothing, and the sender says so
  b.serial("link_key=wrong");
  net.run(3000);
  ring(net);
  net.run(2500);
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
  TEST_ASSERT_TRUE(has(logs(a), "no answer from porch"));
  // an unsigned board's frames are refused by a keyed one
  a.serial("link_key=");
  b.serial("link_key=open-sesame");
  net.run(3000);
  ring(net);
  TEST_ASSERT_EQUAL(0, b.level(Channel::Out1));
}

void tell_all_reaches_every_board_and_counts_the_answers() {
  Net net;
  Node& a = net.add("a1", "Front Door");
  Node& b = net.add("b2", "Porch");
  Node& c = net.add("c3", "Attic");
  TEST_ASSERT_TRUE(setScript(*a.sim, "effect x:\n  wait for trig1\n  tell all blackout\n  wait 1\n").isOk());
  for (Node* n : {&b, &c}) {
    TEST_ASSERT_TRUE(setScript(*n->sim, PORCH).isOk());
    n->sim->serial("remote_free_effects=blackout");
    n->sim->serial("link_key=k");
  }
  a.sim->serial("link_key=k");
  net.run(2500);
  ring(net);
  TEST_ASSERT_EQUAL(1000, b.sim->level(Channel::Out1));
  TEST_ASSERT_EQUAL(1000, c.sim->level(Channel::Out1));
  TEST_ASSERT_EQUAL(1, b.sim->ctl().scriptRunner()->runs(0));  // once, though sent again
  net.run(2500);
  TEST_ASSERT_TRUE_MESSAGE(has(logs(*a.sim), "tell all blackout: 2 boards answered"), logs(*a.sim).c_str());
}

// ---------------------------------------------------------------- over MQTT

void over_mqtt_the_effect_starts_and_the_answer_comes_back() {
  Sim a("a1"), b("b2");
  a.request(R"({"op":"set_params","values":[{"key":"name","value":"Front Door"},{"key":"mqtt_broker","value":"127.0.0.1"}]})");
  b.request(R"({"op":"set_params","values":[{"key":"name","value":"Porch"},{"key":"mqtt_broker","value":"127.0.0.1"}]})");
  TEST_ASSERT_TRUE(setScript(a, "effect x:\n  wait for trig1\n  tell porch blackout\n  tell porch pixel_speed = 150\n  tell porch nosuch\n  wait 1\n").isOk());
  TEST_ASSERT_TRUE(setScript(b, PORCH).isOk());
  b.serial("remote_free_effects=blackout");
  auto sa = a.ctl().mqttSubscriptions();
  b.ctl().mqttSubscriptions();
  TEST_ASSERT_TRUE(std::find(sa.begin(), sa.end(), "proptroller/front-door/link/answer") != sa.end());
  a.ctl().mqttOnConnect();
  b.ctl().mqttOnConnect();
  a.step(2500);
  b.step(2500);
  a.setInput(Input::Trig1, true);
  a.step(150);
  a.setInput(Input::Trig1, false);
  TEST_ASSERT_TRUE(a.ctl().linkTake().empty());  // over MQTT: nothing for the peer-to-peer side
  std::vector<MqttMsg> out = a.ctl().mqttPoll();
  std::vector<MqttMsg> tells;
  for (auto& m : out)
    if (m.topic.compare(0, 18, "proptroller/porch/") == 0) tells.push_back(m);
  TEST_ASSERT_EQUAL(3, (int)tells.size());
  TEST_ASSERT_EQUAL_STRING("proptroller/porch/control/blackout", tells[0].topic.c_str());
  TEST_ASSERT_FALSE(tells[0].retain);
  TEST_ASSERT_EQUAL_STRING("proptroller/porch/config/now/pixel_speed", tells[1].topic.c_str());
  std::vector<MqttMsg> answers;
  for (auto& m : tells)
    for (auto& r : b.ctl().mqttOnMessage(m.topic, m.payload)) answers.push_back(r);
  b.step(20);
  TEST_ASSERT_EQUAL(1000, b.level(Channel::Out1));
  TEST_ASSERT_EQUAL(150, b.ctl().settings().pixel_speed);
  TEST_ASSERT_EQUAL(1, (int)b.ctl().linkKept().size());  // not saved
  int n = 0;
  for (auto& m : answers)
    if (m.topic == "proptroller/front-door/link/answer") { n++; a.ctl().mqttOnMessage(m.topic, m.payload); }
  TEST_ASSERT_EQUAL(3, n);
  TEST_ASSERT_TRUE_MESSAGE(has(logs(a), "tell porch nosuch: porch refused: unknown control"), logs(a).c_str());
  a.step(2500);
  a.ctl().linkTake();
  TEST_ASSERT_FALSE(has(logs(a), "no answer"));
  // a plain control from another tool still works, and gets no answer
  b.step(3000);
  auto plain = b.ctl().mqttOnMessage("proptroller/porch/control/blackout", "");
  for (auto& m : plain) TEST_ASSERT_TRUE(m.topic.find("link/answer") == std::string::npos);
  // tell all over MQTT: the boards online on the broker
  TEST_ASSERT_TRUE(setScript(a, "effect x:\n  wait for trig1\n  tell all blackout\n  wait 1\n").isOk());
  a.ctl().mqttPoll();  // the session ends: it subscribes again with the online topics
  sa = a.ctl().mqttSubscriptions();
  TEST_ASSERT_TRUE(std::find(sa.begin(), sa.end(), "proptroller/+/online") != sa.end());
  a.ctl().mqttOnMessage("proptroller/porch/online", "true");
  a.ctl().mqttOnMessage("proptroller/attic/online", "true");
  a.ctl().mqttOnMessage("proptroller/attic/online", "false");
  a.ctl().mqttOnMessage("proptroller/front-door/online", "true");
  a.step(1500);
  a.setInput(Input::Trig1, true);
  a.step(150);
  a.setInput(Input::Trig1, false);
  a.ctl().linkTake();
  out = a.ctl().mqttPoll();
  int to_porch = 0, others = 0;
  for (auto& m : out) {
    if (m.topic == "proptroller/porch/control/blackout") to_porch++;
    else if (m.topic.find("/control/") != std::string::npos) others++;
  }
  TEST_ASSERT_EQUAL(1, to_porch);
  TEST_ASSERT_EQUAL(0, others);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(hmac_sha256_matches_rfc_4231);
  RUN_TEST(the_frame_round_trip_its_tag_and_its_limits);
  RUN_TEST(numbers_seen_run_once_and_late_ones_still_run);
  RUN_TEST(one_board_starts_an_effect_on_another_over_wifi);
  RUN_TEST(a_refused_start_is_said_on_the_sender_and_no_answer_too);
  RUN_TEST(board_link_off_sends_nothing_and_takes_nothing);
  RUN_TEST(another_group_is_not_heard);
  RUN_TEST(settings_from_another_board_are_used_and_not_saved);
  RUN_TEST(a_page_locked_board_takes_no_settings_and_gates_starts);
  RUN_TEST(with_a_key_a_challenge_then_it_runs_and_replays_are_dropped);
  RUN_TEST(tell_all_reaches_every_board_and_counts_the_answers);
  RUN_TEST(over_mqtt_the_effect_starts_and_the_answer_comes_back);
  return UNITY_END();
}
