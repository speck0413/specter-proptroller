// Wi-Fi setup: the setup hotspot's default password, the network list for the phone page (op wifi_scan), the
// mDNS name a board answers to, when a join counts as failed, and error answers that carry the request's id.
#include <unity.h>
#include "proptroller/password.h"
#include "proptroller/sim/hardware.h"
#include "proptroller/sim/sim.h"
#include "proptroller/scanrun.h"
#include "proptroller/wifijoin.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static JsonDocument result(Response r) {
  JsonDocument d;
  deserializeJson(d, r.json());
  return d;
}
static JsonDocument infoOf(Sim& sim) {
  JsonDocument d;
  deserializeJson(d, sim.infoJson());
  return d;
}
static bool hasCap(JsonDocument& info, const char* cap) {
  for (JsonVariant c : info["capabilities"].as<JsonArray>())
    if (c == cap) return true;
  return false;
}

// ---------------------------------------------------------------- the setup hotspot's password

void hotspot_password_defaults_to_password() {
  Settings s;
  TEST_ASSERT_EQUAL_STRING("password", s.ap_pass.c_str());
  TEST_ASSERT_EQUAL_STRING("password", AP_PASS_DEFAULT);
  // long enough for WPA2: the hotspot is secured out of the box (net.cpp opens it open below 8)
  TEST_ASSERT_TRUE(s.ap_pass.size() >= AP_PASS_MIN_LEN);
  TEST_ASSERT_EQUAL(8, AP_PASS_MIN_LEN);
  Sim sim("a0b1c2d3e4f5");
  NetConfig n = sim.netConfig();
  TEST_ASSERT_EQUAL_STRING("password", n.ap_pass.c_str());
  TEST_ASSERT_EQUAL_STRING("Proptroller-e4f5", n.ap_ssid.c_str());
}

void hotspot_password_keeps_the_wpa2_rule_and_factory_reset_restores_it() {
  Sim sim;
  Response r = sim.request(R"({"op":"set_params","values":[{"key":"ap_pass","value":"seven77"}]})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("password", sim.netConfig().ap_pass.c_str());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"ap_pass","value":"eight888"}]})").isOk());
  TEST_ASSERT_EQUAL_STRING("eight888", sim.netConfig().ap_pass.c_str());
  sim.request(R"({"op":"factory_reset"})");
  sim.step(500);
  TEST_ASSERT_EQUAL_STRING("password", sim.netConfig().ap_pass.c_str());
}

void hotspot_password_help_says_it_is_a_simple_default() {
  Sim sim;
  JsonDocument d = result(sim.request(R"({"op":"get_params"})"));
  std::string help;
  JsonArray params = d["result"]["params"].as<JsonArray>();
  for (JsonVariant p : params)
    if (p["key"] == "ap_pass") help = p["help"].as<std::string>();
  TEST_ASSERT_TRUE_MESSAGE(help.find("\"password\"") != std::string::npos, help.c_str());
  TEST_ASSERT_TRUE_MESSAGE(help.find("8-63 characters") != std::string::npos, help.c_str());
}

// ---------------------------------------------------------------- the network list

void networks_are_one_per_name_strongest_first_and_capped() {
  std::vector<WifiNetwork> raw = {{"B", -70, true}, {"A", -50, true}, {"B", -40, false}, {"", -30, true},
                                  {std::string("bad\x01name"), -20, true}, {"C", -50, false}, {std::string(33, 'x'), -10, true}};
  auto v = tidyNetworks(raw);
  TEST_ASSERT_EQUAL(3, v.size());
  TEST_ASSERT_EQUAL_STRING("B", v[0].ssid.c_str());  // the strongest of the two "B"s, and its secured flag
  TEST_ASSERT_EQUAL(-40, v[0].rssi);
  TEST_ASSERT_FALSE(v[0].secured);
  TEST_ASSERT_EQUAL_STRING("A", v[1].ssid.c_str());  // equal signal: by name
  TEST_ASSERT_EQUAL_STRING("C", v[2].ssid.c_str());
  std::vector<WifiNetwork> many;
  for (int i = 0; i < 40; i++) many.push_back(WifiNetwork("net" + std::to_string(i), -90 + i, true));
  auto m = tidyNetworks(many);
  TEST_ASSERT_EQUAL(WIFI_SCAN_MAX_NETWORKS, m.size());
  TEST_ASSERT_EQUAL_STRING("net39", m[0].ssid.c_str());
}

void scans_are_rate_limited_and_reused_while_fresh() {
  WifiScan s;
  TEST_ASSERT_EQUAL(0, s.ask(0, false));
  TEST_ASSERT_FALSE_MESSAGE(s.takeStart(0), "no scan before the network layer says it can scan");
  s.setAvailable(false, 1000);
  TEST_ASSERT_FALSE(s.takeStart(1000));
  TEST_ASSERT_EQUAL(0, s.ask(1000, false));  // no list yet: a scan is due
  TEST_ASSERT_TRUE(s.scanning(1000));
  TEST_ASSERT_TRUE(s.takeStart(1010));
  TEST_ASSERT_FALSE(s.takeStart(1020));  // one at a time
  s.finish(3000, {{"Office", -50, true}}, true);
  TEST_ASSERT_FALSE(s.scanning(3000));
  TEST_ASSERT_EQUAL(1, s.networks().size());
  // a plain ask while the list is fresh: no new scan
  TEST_ASSERT_EQUAL(0, s.ask(5000, false));
  TEST_ASSERT_FALSE(s.scanning(5000));
  // rescan sooner than WIFI_SCAN_MIN_GAP_MS after the last start: refused, with the time left
  TEST_ASSERT_EQUAL(WIFI_SCAN_MIN_GAP_MS - 4000 + 10, s.ask(5000, true));
  TEST_ASSERT_FALSE(s.scanning(5000));
  JsonDocument d;
  s.toJson(d.to<JsonObject>(), 5000, WIFI_SCAN_MIN_GAP_MS - 3990);
  TEST_ASSERT_EQUAL(2000, d["age_ms"].as<int>());
  TEST_ASSERT_EQUAL(6010, d["retry_ms"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Office", d["networks"][0]["ssid"]);
  TEST_ASSERT_TRUE(d["networks"][0]["secured"].as<bool>());
  TEST_ASSERT_FALSE(d["error"].is<const char*>());
  // after the gap: rescan starts one; the old list is kept until the new one is in
  TEST_ASSERT_EQUAL(0, s.ask(11010, true));
  TEST_ASSERT_TRUE(s.takeStart(11020));
  TEST_ASSERT_EQUAL(1, s.networks().size());
  // a scan that never reports times out as failed; the next ask may start another
  TEST_ASSERT_TRUE(s.scanning(11020 + WIFI_SCAN_TIMEOUT_MS - 1));
  TEST_ASSERT_FALSE(s.scanning(11020 + WIFI_SCAN_TIMEOUT_MS));
  JsonDocument e;
  s.toJson(e.to<JsonObject>(), 11020 + WIFI_SCAN_TIMEOUT_MS, 0);
  TEST_ASSERT_TRUE(e["error"].is<const char*>());
  TEST_ASSERT_EQUAL(1, e["networks"].size());
  TEST_ASSERT_EQUAL(0, s.ask(11020 + WIFI_SCAN_TIMEOUT_MS, false));
  TEST_ASSERT_TRUE(s.takeStart(11020 + WIFI_SCAN_TIMEOUT_MS));
  s.finish(11020 + WIFI_SCAN_TIMEOUT_MS + 5, {}, false);  // the radio said no
  TEST_ASSERT_FALSE(s.scanning(11020 + WIFI_SCAN_TIMEOUT_MS + 5));
}

void the_hotspot_asks_for_a_list_when_it_opens() {
  WifiScan s;
  s.setAvailable(true, 0);
  TEST_ASSERT_TRUE(s.takeStart(10));
}

void wifi_scan_op_on_the_simulated_board() {
  Sim sim;
  JsonDocument info = infoOf(sim);
  TEST_ASSERT_TRUE(hasCap(info, "wifi_scan"));
  JsonDocument a = result(sim.request(R"({"v":1,"id":3,"op":"wifi_scan"})"));
  TEST_ASSERT_TRUE(a["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("wifi_scan", a["result"]["kind"]);
  TEST_ASSERT_TRUE(a["result"]["scanning"].as<bool>());
  TEST_ASSERT_EQUAL(0, a["result"]["networks"].size());
  TEST_ASSERT_FALSE(a["result"]["age_ms"].is<int>());
  sim.step(SIM_SCAN_MS + 20);
  JsonDocument b = result(sim.request(R"({"op":"wifi_scan"})"));
  TEST_ASSERT_FALSE(b["result"]["scanning"].as<bool>());
  JsonArray n = b["result"]["networks"].as<JsonArray>();
  TEST_ASSERT_EQUAL(4, n.size());
  TEST_ASSERT_EQUAL_STRING("Office", n[0]["ssid"]);
  TEST_ASSERT_EQUAL(-48, n[0]["rssi"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Guest Wi-Fi", n[2]["ssid"]);
  TEST_ASSERT_FALSE(n[2]["secured"].as<bool>());
  // the show step was never held up: the board kept stepping through the scan
  TEST_ASSERT_TRUE(b["result"]["age_ms"].as<int>() < 100);
  // rescan too soon: the list as it is, and when it can be asked again
  JsonDocument c = result(sim.request(R"({"op":"wifi_scan","rescan":true})"));
  TEST_ASSERT_FALSE(c["result"]["scanning"].as<bool>());
  TEST_ASSERT_TRUE(c["result"]["retry_ms"].as<int>() > 0);
  TEST_ASSERT_EQUAL(4, c["result"]["networks"].size());
  // a mistyped field is a bad request
  JsonDocument e = result(sim.request(R"({"op":"wifi_scan","rescan":"yes"})"));
  TEST_ASSERT_FALSE(e["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("bad_request", e["error"]["code"]);
}

void wifi_scan_is_a_read_under_the_page_lock() {
  Sim sim;
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"pin","value":"123456"},{"key":"page_lock","value":true}]})").isOk());
  // a change needs the password now, the network list (like the settings) does not
  TEST_ASSERT_FALSE(sim.request(R"({"op":"set_params","values":[{"key":"ambient","value":false}]})").isOk());
  JsonDocument d = result(sim.request(R"({"op":"wifi_scan"})"));
  TEST_ASSERT_TRUE(d["ok"].as<bool>());
}

void a_board_without_wifi_has_no_wifi_scan() {
  SimWorld w;
  SimPins pins(w);
  SimUart uart(w);
  SimPixels px(w);
  Identity id;
  id.id = "e6614103e7";
  id.mcu = "pico";
  Controller c(pins, uart, px, nullptr, id);  // the plain Pico: no network layer ever says it can scan
  JsonDocument info;
  deserializeJson(info, c.infoJson());
  TEST_ASSERT_FALSE(hasCap(info, "wifi_scan"));
  std::string body = R"({"op":"wifi_scan"})";
  JsonDocument d;
  deserializeJson(d, c.call(body.data(), body.size()));
  TEST_ASSERT_FALSE(d["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("unsupported", d["error"]["code"]);
}

// ---------------------------------------------------------------- mDNS name

void mdns_name_takes_the_short_id_only_when_taken() {
  TEST_ASSERT_EQUAL_STRING("ac2c", shortId("a0b1c2d3ac2c").c_str());
  TEST_ASSERT_EQUAL_STRING("ac2c", shortId("A0B1C2D3AC2C").c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller", mdnsLabel("proptroller", "a0b1c2d3ac2c", false).c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller-ac2c", mdnsLabel("proptroller", "a0b1c2d3ac2c", true).c_str());
  TEST_ASSERT_EQUAL_STRING("front-door-ac2c", mdnsLabel("front-door", "a0b1c2d3ac2c", true).c_str());
  // a nameless board's label already carries the id
  TEST_ASSERT_EQUAL_STRING("proptroller-ac2c", mdnsLabel(hostLabel("  ", "a0b1c2d3ac2c"), "a0b1c2d3ac2c", true).c_str());
}

void the_default_name_answers_to_proptroller_local_and_a_rename_changes_it() {
  Sim sim("a0b1c2d3ac2c");
  NetConfig n = sim.netConfig();
  TEST_ASSERT_EQUAL_STRING("proptroller", n.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING("proptroller-ac2c", n.hostname_taken.c_str());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"name","value":"Front Door!"}]})").isOk());
  n = sim.netConfig();
  TEST_ASSERT_EQUAL_STRING("front-door", n.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING("front-door-ac2c", n.hostname_taken.c_str());
}

void the_mdns_name_reaches_the_page_in_state_net() {
  Sim sim;
  NetState n;
  n.mode = "sta";
  n.ip = "192.168.1.23";
  n.ssid = "Office";
  n.host = "proptroller-ac2c.local";
  sim.netChanged(n);
  JsonDocument d;
  deserializeJson(d, sim.ctl().stateJson());
  TEST_ASSERT_EQUAL_STRING("proptroller-ac2c.local", d["net"]["host"]);
  n.host.clear();
  sim.netChanged(n);
  JsonDocument e;
  deserializeJson(e, sim.ctl().stateJson());
  TEST_ASSERT_FALSE_MESSAGE(e["net"]["host"].is<const char*>(), "no host field while the board has no mDNS name");
  TEST_ASSERT_FALSE_MESSAGE(e["net"]["hotspot"].is<const char*>(), "no hotspot field when none is up");
  // the hotspot stays up while the board is on the Wi-Fi: the page shows both addresses
  n.hotspot = "Proptroller-AC2C";
  sim.netChanged(n);
  JsonDocument f;
  deserializeJson(f, sim.ctl().stateJson());
  TEST_ASSERT_EQUAL_STRING("sta", f["net"]["mode"]);
  TEST_ASSERT_EQUAL_STRING("Proptroller-AC2C", f["net"]["hotspot"]);
}

// ---------------------------------------------------------------- joining the network (bench problem 12)

void a_dropped_network_is_joined_again_without_counting_a_failure() {
  WifiJoin j;
  TEST_ASSERT_TRUE(j.poll(0, false, false, false, false) == WifiJoin::Step::Join);
  TEST_ASSERT_TRUE(j.poll(5000, false, false, false, false) == WifiJoin::Step::Wait);
  TEST_ASSERT_TRUE(j.poll(6000, true, false, false, false) == WifiJoin::Step::Wait);
  // the router restarts long after the first join: joined again at once, no failure
  TEST_ASSERT_TRUE(j.poll(600000, false, false, false, false) == WifiJoin::Step::Rejoin);
  TEST_ASSERT_EQUAL(0, j.failures());
  // that join does not work either: now it is a failure, after the retry time
  TEST_ASSERT_TRUE(j.poll(600000 + WIFI_RETRY_MS - 1, false, false, false, false) == WifiJoin::Step::Wait);
  TEST_ASSERT_TRUE(j.poll(600000 + WIFI_RETRY_MS, false, false, false, false) == WifiJoin::Step::Retry);
  TEST_ASSERT_EQUAL(1, j.failures());
  TEST_ASSERT_TRUE(j.poll(700000, true, false, false, false) == WifiJoin::Step::Wait);
  TEST_ASSERT_EQUAL(0, j.failures());
}

void a_restart_taking_the_wifi_down_is_no_failed_join() {
  WifiJoin j;
  (void)j.poll(0, false, false, false, false);
  (void)j.poll(3000, true, false, false, false);
  // the board restarts (reboot, an update): net.stop() first, then the Wi-Fi goes down; nothing is joined or counted
  for (Millis t = 600000; t < 700000; t += 10) TEST_ASSERT_TRUE(j.poll(t, false, false, true, false) == WifiJoin::Step::Wait);
  TEST_ASSERT_EQUAL(0, j.failures());
}

void failed_joins_retry_slowly_while_the_hotspot_is_up() {
  // the hotspot is always up: a failed join is tried again WIFI_RETRY_AP_MS later (a retry moves the radio's
  // channel and drops a phone on the hotspot); a running scan holds it off
  WifiJoin j;
  Millis t = 0;
  TEST_ASSERT_TRUE(j.poll(t, false, true, false, false) == WifiJoin::Step::Join);
  for (int k = 1; k <= 3; k++) {
    TEST_ASSERT_TRUE(j.poll(t + WIFI_RETRY_AP_MS - 1, false, true, false, false) == WifiJoin::Step::Wait);
    TEST_ASSERT_TRUE(j.poll(t + WIFI_RETRY_AP_MS, false, true, false, true) == WifiJoin::Step::Wait);
    t += WIFI_RETRY_AP_MS + 1;
    TEST_ASSERT_TRUE(j.poll(t, false, true, false, false) == WifiJoin::Step::Retry);
    TEST_ASSERT_EQUAL(k, j.failures());
  }
  // joined: the count starts again; a drop is joined again at once
  TEST_ASSERT_TRUE(j.poll(t + 1, true, true, false, false) == WifiJoin::Step::Wait);
  TEST_ASSERT_EQUAL(0, j.failures());
  TEST_ASSERT_TRUE(j.poll(t + 2, false, true, false, false) == WifiJoin::Step::Rejoin);
}

void failed_joins_slow_down_without_a_hotspot() {
  WifiJoin j;
  Millis t = 0;
  TEST_ASSERT_TRUE(j.poll(t, false, false, false, false) == WifiJoin::Step::Join);
  for (int k = 1; k <= WIFI_SLOW_AFTER_FAILURES; k++) {
    t += WIFI_RETRY_MS;
    TEST_ASSERT_TRUE(j.poll(t, false, false, false, false) == WifiJoin::Step::Retry);
  }
  TEST_ASSERT_TRUE(j.poll(t + WIFI_RETRY_SLOW_MS - 1, false, false, false, false) == WifiJoin::Step::Wait);
  TEST_ASSERT_TRUE(j.poll(t + WIFI_RETRY_SLOW_MS, false, false, false, false) == WifiJoin::Step::Retry);
}

// ---------------------------------------------------------------- errors carry the request's id (bench problem 11)

static JsonDocument callSim(Sim& sim, const std::string& body) {
  JsonDocument d;
  deserializeJson(d, sim.call(body.data(), body.size()));
  return d;
}
static void errorWithId(Sim& sim, const std::string& body, uint32_t id, const char* code) {
  JsonDocument d = callSim(sim, body);
  TEST_ASSERT_FALSE_MESSAGE(d["ok"].as<bool>(), body.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(code, d["error"]["code"] | "", body.c_str());
  TEST_ASSERT_TRUE_MESSAGE(d["id"].is<uint32_t>(), (body + " -> no id").c_str());
  TEST_ASSERT_EQUAL_MESSAGE(id, d["id"].as<uint32_t>(), body.c_str());
}

void every_error_answer_echoes_the_request_id() {
  Sim sim;
  errorWithId(sim, R"({"v":1,"id":7,"op":"restart"})", 7, "bad_request");  // the op is reboot
  errorWithId(sim, R"({"id":8,"op":"get_show","show":"one"})", 8, "bad_request");
  errorWithId(sim, R"({"id":9,"op":"wifi_scan","rescan":"yes"})", 9, "bad_request");
  errorWithId(sim, R"({"id":10,"v":"one","op":"info"})", 10, "bad_request");
  errorWithId(sim, R"({"id":11,"pin":5,"op":"info"})", 11, "bad_request");
  errorWithId(sim, R"({"id":12})", 12, "bad_request");
  errorWithId(sim, R"({"id":13,"v":2,"op":"info"})", 13, "version");
  errorWithId(sim, R"({"id":14,"op":"run","show":99})", 14, "not_found");
  // broken JSON: the id is found in the text
  errorWithId(sim, R"({"op":"put_show","id":15,"data":{"id":4,"name":"x")", 15, "bad_request");
  // the page lock
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"pin","value":"123456"},{"key":"page_lock","value":true}]})").isOk());
  errorWithId(sim, R"({"id":16,"op":"set_params","values":[{"key":"ambient","value":false}]})", 16, "pin");
  // an answer that has no id to echo has none
  JsonDocument d = callSim(sim, R"({"op":"launch"})");
  TEST_ASSERT_FALSE(d["id"].is<uint32_t>());
}

void the_id_of_a_request_refused_before_parsing() {
  // too big for the memory left: refused before it is parsed, still with its id
  Sim sim;
  sim.ctl().setHeapProbe([]() -> size_t { return 1024; });
  std::string big = R"({"id":21,"op":"check_script","text":")" + std::string(4000, 'x') + "\"}";
  JsonDocument d;
  deserializeJson(d, sim.ctl().call(big.data(), big.size()));  // the board's own path (Controller::call)
  TEST_ASSERT_EQUAL_STRING("refused", d["error"]["code"]);
  TEST_ASSERT_EQUAL(21, d["id"].as<int>());
}

void request_ids_are_found_only_at_the_top() {
  uint32_t id = 0;
  TEST_ASSERT_TRUE(peekRequestId(R"({"v":1,"id": 42,"op":"x"})", 25, id));
  TEST_ASSERT_EQUAL(42, id);
  const char* nested = R"({"op":"put_show","data":{"id":4,"name":"a \"id\": 9"},"x":[{"id":3}]})";
  TEST_ASSERT_FALSE(peekRequestId(nested, strlen(nested), id));
  const char* after = R"({"data":{"id":4},"name":"{\"id\":5","id":6})";
  TEST_ASSERT_TRUE(peekRequestId(after, strlen(after), id));
  TEST_ASSERT_EQUAL(6, id);
  const char* bad[] = {R"({"id":"7"})", R"({"id":-1})", R"({"id":1.5})", R"({"id":99999999999})", R"([{"id":1}])", "id:1"};
  for (auto b : bad) TEST_ASSERT_FALSE_MESSAGE(peekRequestId(b, strlen(b), id), b);
}

// ---------------------------------------------------------------- a new network without a password is open

static void setOk(Sim& sim, const std::string& values) {
  Response r = sim.request(R"({"op":"set_params","values":)" + values + "}");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), (values + ": " + r.errorMessage()).c_str());
}

void a_new_network_without_a_password_drops_the_old_one() {
  Sim sim;
  setOk(sim, R"([{"key":"wifi_ssid","value":"Home"},{"key":"wifi_pass","value":"home-secret"}])");
  TEST_ASSERT_EQUAL_STRING("home-secret", sim.ctl().settings().wifi_pass.c_str());
  // another setting, or the same network again with the password left empty: the password stays
  setOk(sim, R"([{"key":"ambient","value":false}])");
  setOk(sim, R"([{"key":"wifi_ssid","value":"Home"},{"key":"wifi_pass","value":""}])");
  TEST_ASSERT_EQUAL_STRING("home-secret", sim.ctl().settings().wifi_pass.c_str());
  // a new network with no password (an open one, picked on the page): the old password is not kept for it
  setOk(sim, R"([{"key":"wifi_ssid","value":"Guest Wi-Fi"},{"key":"wifi_pass","value":""}])");
  TEST_ASSERT_EQUAL_STRING("Guest Wi-Fi", sim.ctl().settings().wifi_ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("", sim.ctl().settings().wifi_pass.c_str());
  setOk(sim, R"([{"key":"wifi_pass","value":"later-pass"}])");
  setOk(sim, R"([{"key":"wifi_ssid","value":"Workshop"}])");  // the field left out entirely: the same
  TEST_ASSERT_EQUAL_STRING("", sim.ctl().settings().wifi_pass.c_str());
  // a new network with its password: that password
  setOk(sim, R"([{"key":"wifi_pass","value":"shop-secret"},{"key":"wifi_ssid","value":"Office"}])");
  TEST_ASSERT_EQUAL_STRING("Office", sim.ctl().settings().wifi_ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("shop-secret", sim.ctl().settings().wifi_pass.c_str());
  // a refused change changes nothing
  TEST_ASSERT_FALSE(sim.request(R"({"op":"set_params","values":[{"key":"wifi_ssid","value":"Elsewhere"},{"key":"trig_debounce_ms","value":5000}]})").isOk());
  TEST_ASSERT_EQUAL_STRING("shop-secret", sim.ctl().settings().wifi_pass.c_str());
}

// ---------------------------------------------------------------- one scan on the radio (ScanRun, arduino/net.cpp)

// A radio that ends its scan end_after ms after it started (never: 0), or refuses to start.
struct FakeRadio : ScanRadio {
  Millis* clock;
  Millis end_after = 0, started_at = 0;
  bool refuse = false, ok = true;
  int starts = 0, aborts = 0, collects = 0;
  std::vector<WifiNetwork> nets{{"Office", -50, true}, {"Cafe", -70, false}, {"Office", -60, true}};
  explicit FakeRadio(Millis* c) : clock(c) {}
  bool start() override {
    starts++;
    if (refuse) return false;
    started_at = *clock;
    return true;
  }
  bool finished() override { return end_after && *clock - started_at >= end_after; }
  bool collect(std::vector<WifiNetwork>& out) override {
    collects++;
    if (ok) out = nets;
    return ok;
  }
  void abort() override { aborts++; }
};
// The controller's side: a real WifiScan, as device.h wires it.
struct ScanApp {
  WifiScan s;
  Millis* clock;
  int done = 0;
  explicit ScanApp(Millis* c) : clock(c) {}
  bool wifiScanWanted() { return s.takeStart(*clock); }
  void wifiScanDone(const std::vector<WifiNetwork>& f, bool ok) {
    done++;
    s.finish(*clock, f, ok);
  }
  JsonDocument json() {
    JsonDocument d;
    s.toJson(d.to<JsonObject>(), *clock, 0);
    return d;
  }
};

// The bug on the ESP32 DevKit: arduino-esp32 called a scan failed 2.4 s after it started while the radio, going
// back to the hotspot's channel between channels, was still scanning; every answer was 0 networks and an error.
void a_scan_longer_than_the_library_limit_still_returns_its_networks() {
  Millis now = 0;
  FakeRadio radio(&now);
  radio.end_after = 4200;  // AP+STA on a real board: well past the old 2400 ms
  ScanApp app(&now);
  ScanRun run;
  app.s.setAvailable(true, now);
  for (; now <= 6000; now += 10) {
    ScanRun::End e = run.poll(now, false, radio, app);
    if (e != ScanRun::End::None) {
      TEST_ASSERT_EQUAL(ScanRun::End::Found, e);
      break;
    }
    if (now > 0) TEST_ASSERT_TRUE_MESSAGE(app.json()["scanning"].as<bool>(), "the page sees one steady scan");
  }
  TEST_ASSERT_EQUAL_MESSAGE(1, radio.starts, "never restarted while running (a restart cancels the scan)");
  TEST_ASSERT_EQUAL(1, radio.collects);
  TEST_ASSERT_EQUAL(0, radio.aborts);
  TEST_ASSERT_EQUAL(1, app.done);
  TEST_ASSERT_EQUAL(4200, run.tookMs());
  TEST_ASSERT_EQUAL(3, run.found());
  TEST_ASSERT_FALSE(run.running());
  JsonDocument d = app.json();
  TEST_ASSERT_FALSE(d["scanning"].as<bool>());
  TEST_ASSERT_FALSE(d["error"].is<const char*>());
  TEST_ASSERT_EQUAL(2, d["networks"].size());
  TEST_ASSERT_EQUAL_STRING("Office", d["networks"][0]["ssid"]);
}

void a_refused_start_is_one_failure_and_no_spin() {
  Millis now = 1000;
  FakeRadio radio(&now);
  radio.refuse = true;
  ScanApp app(&now);
  ScanRun run;
  app.s.setAvailable(true, now);
  TEST_ASSERT_EQUAL(ScanRun::End::Refused, run.poll(now, false, radio, app));
  for (now = 1010; now < 5000; now += 10) TEST_ASSERT_EQUAL(ScanRun::End::None, run.poll(now, false, radio, app));
  TEST_ASSERT_EQUAL(1, radio.starts);
  TEST_ASSERT_EQUAL(1, app.done);
  TEST_ASSERT_FALSE(run.running());
  JsonDocument d = app.json();
  TEST_ASSERT_FALSE(d["scanning"].as<bool>());
  TEST_ASSERT_TRUE(d["error"].is<const char*>());
  // a rescan after the gap tries again, and a radio that can scan now gives the list
  radio.refuse = false;
  radio.end_after = 3000;
  now = 1000 + WIFI_SCAN_MIN_GAP_MS;
  TEST_ASSERT_EQUAL(0, app.s.ask(now, true));
  TEST_ASSERT_EQUAL(ScanRun::End::None, run.poll(now, false, radio, app));
  now += 3000;
  TEST_ASSERT_EQUAL(ScanRun::End::Found, run.poll(now, false, radio, app));
  TEST_ASSERT_FALSE(app.json()["error"].is<const char*>());
}

void no_scan_starts_while_a_join_is_under_way() {
  Millis now = 0;
  FakeRadio radio(&now);
  radio.end_after = 2000;
  ScanApp app(&now);
  ScanRun run;
  app.s.setAvailable(true, now);
  for (; now < 8000; now += 100) TEST_ASSERT_EQUAL(ScanRun::End::None, run.poll(now, true, radio, app));
  TEST_ASSERT_EQUAL(0, radio.starts);
  TEST_ASSERT_TRUE_MESSAGE(app.json()["scanning"].as<bool>(), "still asked for: it starts once the join is done");
  TEST_ASSERT_EQUAL(ScanRun::End::None, run.poll(now, false, radio, app));
  TEST_ASSERT_TRUE(run.running());
  // a join waits for the running scan (WifiJoin's busy), and a join_busy flag mid-scan does not stop it
  now += 1000;
  TEST_ASSERT_EQUAL(ScanRun::End::None, run.poll(now, true, radio, app));
  now += 1000;
  TEST_ASSERT_EQUAL(ScanRun::End::Found, run.poll(now, true, radio, app));
  TEST_ASSERT_EQUAL(1, radio.starts);
}

void a_scan_that_never_ends_is_stopped_at_the_time_limit() {
  Millis now = 0;
  FakeRadio radio(&now);  // never finishes
  ScanApp app(&now);
  ScanRun run;
  app.s.setAvailable(true, now);
  for (; now < WIFI_SCAN_TIMEOUT_MS; now += 10) TEST_ASSERT_EQUAL(ScanRun::End::None, run.poll(now, false, radio, app));
  TEST_ASSERT_EQUAL(0, radio.aborts);
  TEST_ASSERT_EQUAL(ScanRun::End::TimedOut, run.poll(now, false, radio, app));
  TEST_ASSERT_EQUAL_MESSAGE(1, radio.aborts, "stopped, so the radio can start the next one");
  TEST_ASSERT_EQUAL(0, radio.collects);
  TEST_ASSERT_EQUAL(1, app.done);
  TEST_ASSERT_TRUE(app.json()["error"].is<const char*>());
  // the late end of the stopped scan is never read: nothing is running, nothing is collected
  radio.end_after = 1;
  now += 10;
  TEST_ASSERT_EQUAL(ScanRun::End::None, run.poll(now, false, radio, app));
  TEST_ASSERT_EQUAL(0, radio.collects);
  TEST_ASSERT_EQUAL(1, radio.starts);
}

void a_scan_the_radio_reports_failed_keeps_the_old_list() {
  Millis now = 0;
  FakeRadio radio(&now);
  radio.end_after = 2500;
  ScanApp app(&now);
  ScanRun run;
  app.s.setAvailable(true, now);
  (void)run.poll(now, false, radio, app);
  now = 2500;
  TEST_ASSERT_EQUAL(ScanRun::End::Found, run.poll(now, false, radio, app));
  radio.ok = false;
  now = WIFI_SCAN_MIN_GAP_MS;
  TEST_ASSERT_EQUAL(0, app.s.ask(now, true));
  (void)run.poll(now, false, radio, app);
  now += 2500;
  TEST_ASSERT_EQUAL(ScanRun::End::Failed, run.poll(now, false, radio, app));
  JsonDocument d = app.json();
  TEST_ASSERT_TRUE(d["error"].is<const char*>());
  TEST_ASSERT_EQUAL(2, d["networks"].size());
}

// Found on the DevKit: the join was stamped after the loop read its clock, so now - joined_at wrapped around and
// the boot scan started in the middle of the first join (refused by the radio: an error on the page at power-on).
void a_scan_waits_for_a_join_even_one_stamped_after_now() {
  TEST_ASSERT_FALSE(scanJoinBusy(1000, false, false, 900));  // no network wanted
  TEST_ASSERT_FALSE(scanJoinBusy(1000, true, true, 900));    // on it
  TEST_ASSERT_FALSE(scanJoinBusy(1000, true, false, 0));     // never asked to join
  TEST_ASSERT_TRUE(scanJoinBusy(1000, true, false, 1003));   // stamped a few ms after now
  TEST_ASSERT_TRUE(scanJoinBusy(1000 + WIFI_JOIN_BUSY_MS - 1, true, false, 1000));
  TEST_ASSERT_FALSE(scanJoinBusy(1000 + WIFI_JOIN_BUSY_MS, true, false, 1000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(hotspot_password_defaults_to_password);
  RUN_TEST(hotspot_password_keeps_the_wpa2_rule_and_factory_reset_restores_it);
  RUN_TEST(hotspot_password_help_says_it_is_a_simple_default);
  RUN_TEST(networks_are_one_per_name_strongest_first_and_capped);
  RUN_TEST(scans_are_rate_limited_and_reused_while_fresh);
  RUN_TEST(the_hotspot_asks_for_a_list_when_it_opens);
  RUN_TEST(a_scan_longer_than_the_library_limit_still_returns_its_networks);
  RUN_TEST(a_refused_start_is_one_failure_and_no_spin);
  RUN_TEST(no_scan_starts_while_a_join_is_under_way);
  RUN_TEST(a_scan_waits_for_a_join_even_one_stamped_after_now);
  RUN_TEST(a_scan_that_never_ends_is_stopped_at_the_time_limit);
  RUN_TEST(a_scan_the_radio_reports_failed_keeps_the_old_list);
  RUN_TEST(wifi_scan_op_on_the_simulated_board);
  RUN_TEST(wifi_scan_is_a_read_under_the_page_lock);
  RUN_TEST(a_board_without_wifi_has_no_wifi_scan);
  RUN_TEST(mdns_name_takes_the_short_id_only_when_taken);
  RUN_TEST(the_default_name_answers_to_proptroller_local_and_a_rename_changes_it);
  RUN_TEST(the_mdns_name_reaches_the_page_in_state_net);
  RUN_TEST(a_dropped_network_is_joined_again_without_counting_a_failure);
  RUN_TEST(a_restart_taking_the_wifi_down_is_no_failed_join);
  RUN_TEST(failed_joins_retry_slowly_while_the_hotspot_is_up);
  RUN_TEST(failed_joins_slow_down_without_a_hotspot);
  RUN_TEST(every_error_answer_echoes_the_request_id);
  RUN_TEST(the_id_of_a_request_refused_before_parsing);
  RUN_TEST(request_ids_are_found_only_at_the_top);
  RUN_TEST(a_new_network_without_a_password_drops_the_old_one);
  return UNITY_END();
}
