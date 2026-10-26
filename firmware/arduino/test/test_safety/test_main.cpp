// Output safety rules: every source (shows, scripts, roles, the effect, manual tests) goes through one
// interlock / maximum on-time / pause pass; STOP and pause stop everything; what a phone turned on ends with it.
#include <unity.h>
#include <chrono>
#include "proptroller/sim/sim.h"
#include "proptroller/serial_line.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static Response setp(Sim& sim, const std::string& key, const std::string& v) {
  return sim.request(R"({"op":"set_params","values":[{"key":")" + key + R"(","value":)" + v + "}]}");
}
static void ok(const Response& r) { TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.errorMessage().c_str()); }

void role_pulses_keep_the_relay_interlock() {
  Sim sim("t");
  ok(setp(sim, "interlock_relays", "true"));
  ok(setp(sim, "rly1_role", "\"trigger_repeat\""));
  ok(setp(sim, "role_pulse_ms", "2000"));
  ok(sim.request(R"({"op":"put_show","data":{"id":1,"name":"r","triggers":[{"on":"input","input":"TRIG1"}],"tracks":[{"target":"RLY2","cues":[{"at":0,"do":"on"},{"at":3000,"do":"off"}]}]}})"));
  sim.step(2500);
  sim.press(Input::Trig1, 150);
  int both = 0, any = 0;
  for (int i = 0; i < 300; i++) {
    sim.step(10);
    both += sim.level(Channel::Rly1) && sim.level(Channel::Rly2);
    any += sim.level(Channel::Rly1) || sim.level(Channel::Rly2);
  }
  TEST_ASSERT_EQUAL(0, both);
  TEST_ASSERT_TRUE(any > 100);
}

void role_pulses_keep_the_max_on_time() {
  Sim sim("t");
  ok(setp(sim, "rly1_role", "\"trigger_repeat\""));
  ok(setp(sim, "role_pulse_ms", "2000"));
  ok(setp(sim, "rly1_max_on_ms", "100"));
  sim.step(2500);
  sim.press(Input::Trig1, 150);
  sim.step(20);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Rly1));
  sim.step(500);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Rly1));
}

void effect_outputs_keep_the_max_on_time() {
  Sim sim("t");
  sim.request(R"({"op":"delete_show","show":1})");
  ok(setp(sim, "effect", "\"pulse_and_recover\""));
  ok(setp(sim, "out1_on_ms", "5000"));
  ok(setp(sim, "out1_max_on_ms", "50"));
  sim.step(2500);
  sim.setInput(Input::Trig1, true);
  sim.step(300);
  sim.setInput(Input::Trig1, false);
  sim.step(200);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  bool was_on = false;
  for (auto& x : sim.trace) was_on |= x.channel == Channel::Out1 && x.level > 0;
  TEST_ASSERT_TRUE(was_on);
}

static Response script(Sim& sim, const std::string& text) {
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = text;
  std::string j;
  serializeJson(d, j);
  return sim.request(j);
}

void stop_all_and_pause_stop_script_effects_for_good() {
  Sim sim("t");
  Response r = script(sim, "effect hold:\n  wait for trig1\n  out1 = on\n  wait for trig2\n  out1 = off\n");
  TEST_ASSERT_TRUE(r.doc()["result"]["ok"] | false);
  sim.step(3000);
  sim.press(Input::Trig1, 200);
  sim.step(200);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out1));
  ok(sim.request(R"({"op":"stop_all"})"));
  sim.step(100);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  sim.press(Input::Trig1, 200);
  sim.step(200);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out1));  // it starts again from its first wait for
  ok(sim.request(R"({"op":"pause","on":true})"));
  sim.step(1000);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  ok(sim.request(R"({"op":"pause","on":false})"));
  sim.step(1000);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));  // releasing pause turns nothing back on
}

void stop_all_and_pause_end_the_effect_cycle_and_role_pulses() {
  Sim sim("t");
  sim.request(R"({"op":"delete_show","show":1})");
  ok(setp(sim, "effect", "\"pulse_and_recover\""));
  ok(setp(sim, "out1_on_ms", "5000"));
  ok(setp(sim, "trigger_out_pulse_ms", "5000"));
  for (const char* op : {R"({"op":"stop_all"})", R"({"op":"pause","on":true})"}) {
    sim.step(4000);
    sim.setInput(Input::Trig1, true);
    sim.step(300);
    sim.setInput(Input::Trig1, false);
    TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out1));
    TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Cc1));
    ok(sim.request(op));
    sim.step(20);
    ok(sim.request(R"({"op":"pause","on":false})"));
    sim.step(6000);
    for (auto c : ALL_CHANNELS) TEST_ASSERT_EQUAL_MESSAGE(0, sim.level(c), op);
  }
}

void releasing_pause_leaves_manual_outputs_off() {
  Sim sim("t");
  sim.step(2500);
  ok(sim.requestFrom(4, R"({"op":"set_output","channel":"OUT3","level":1000})"));
  ok(sim.request(R"({"op":"pause","on":true})"));
  sim.step(100);
  ok(sim.request(R"({"op":"pause","on":false})"));
  sim.step(100);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out3));
}

void stopping_needs_no_page_password() {
  Sim sim("t");
  sim.step(2500);
  ok(sim.requestFrom(9, R"({"op":"set_output","channel":"OUT3","level":1000})"));
  ok(sim.request(R"({"op":"set_params","values":[{"key":"pin","value":"123456"},{"key":"page_lock","value":true}]})"));
  for (const char* op : {R"({"op":"set_output","channel":"OUT3","level":null})", R"({"op":"set_output","channel":"OUT3","level":0})",
                         R"({"op":"stop_all"})", R"({"op":"stop","show":1})", R"({"op":"pause","on":true})"})
    ok(sim.requestFrom(9, op));
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out3));
  // starting and switching on still need it
  for (const char* op : {R"({"op":"pause","on":false})", R"({"op":"set_output","channel":"OUT3","level":1000})", R"({"op":"run","show":1})"})
    TEST_ASSERT_FALSE_MESSAGE(sim.requestFrom(9, op).isOk(), op);
}

void stop_all_then_update() {
  Sim sim("t");
  Response r = script(sim, "effect hold:\n  wait for trig1\n  out1 = on\n  wait for trig2\n  out1 = off\n");
  sim.step(3000);
  sim.press(Input::Trig1, 200);
  sim.step(200);
  UpdateRequest q;
  q.has_len = true;
  q.len = 1000;
  q.octet_stream = true;
  q.has_sha256 = true;
  q.sha256 = std::string(64, 'a');
  std::string a;
  TEST_ASSERT_FALSE(sim.updateBegin(q, a));
  ok(sim.request(R"({"op":"stop_all"})"));
  sim.step(20);
  TEST_ASSERT_TRUE_MESSAGE(sim.updateBegin(q, a), a.c_str());
  sim.updateAbort();
}

void manual_outputs_end_with_the_page_that_set_them() {
  Sim sim("t");
  sim.step(2500);
  ok(sim.requestFrom(7, R"({"op":"set_output","channel":"OUT1","level":1000})"));
  ok(sim.requestFrom(7, R"({"op":"set_output","channel":"RLY1","level":1000})"));
  ok(sim.requestFrom(8, R"({"op":"set_output","channel":"OUT3","level":1000})"));
  sim.step(20);
  sim.sessionClosed(7);
  sim.step(20);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Rly1));
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out3));  // another page's test stays
}

void http_and_mqtt_switch_outputs_on_only_as_pulses() {
  Sim sim("t");
  sim.step(2500);
  Response r = sim.request(R"({"op":"set_output","channel":"OUT1","level":1000})");
  TEST_ASSERT_EQUAL_STRING("refused", r.doc()["error"]["code"] | "");
  ok(sim.request(R"({"op":"set_output","channel":"OUT1","level":1000,"ms":500})"));
  sim.step(100);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out1));
  sim.step(500);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  ok(sim.request(R"({"op":"set_output","channel":"OUT1","level":0})"));  // off needs no pulse
  TEST_ASSERT_FALSE(sim.request(R"({"op":"set_output","channel":"OUT1","level":1000,"ms":600001})").isOk());
}

void flash_saves_wait_until_nothing_runs() {
  Sim sim("t");
  ok(sim.request(R"({"op":"put_show","data":{"id":2,"name":"long","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"on"},{"at":30000,"do":"off"}]}]}})"));
  sim.step(5000);
  int before = sim.flash().saves;
  sim.press(Input::Trig2, 150);
  ok(setp(sim, "role_pulse_ms", "300"));
  sim.step(10000);
  TEST_ASSERT_EQUAL_MESSAGE(before, sim.flash().saves, "no flash write while the show runs");
  sim.step(25000);
  TEST_ASSERT_EQUAL_MESSAGE(before + 1, sim.flash().saves, "saved once the show ended");
}

void factory_reset_erases_the_show_script() {
  Sim sim("t");
  Response r = script(sim, "effect a:\n  wait for trig1\n  out1 = on\n  sleep 2\n  out1 = off\n");
  TEST_ASSERT_TRUE(r.doc()["result"]["ok"] | false);
  sim.step(6000);
  ok(sim.request(R"({"op":"factory_reset"})"));
  sim.step(1000);
  sim.powerCycle();
  sim.step(4000);
  Response g = sim.request(R"({"op":"get_script"})");
  TEST_ASSERT_EQUAL_STRING("", g.doc()["result"]["text"] | "missing");
  TEST_ASSERT_NULL(sim.ctl().scriptProgram());
}

void a_button_held_at_power_on_starts_nothing() {
  Sim sim("t");
  ok(sim.request(R"({"op":"put_show","data":{"id":2,"name":"u","triggers":[{"on":"input","input":"USER1"}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"pulse","ms":2000}]}]}})"));
  sim.step(6000);
  sim.setInput(Input::User1, true);
  sim.powerCycle();
  bool came_on = false;
  for (int i = 0; i < 400; i++) { sim.step(10); came_on |= sim.level(Channel::Out2) > 0; }
  TEST_ASSERT_FALSE(came_on);
  sim.setInput(Input::User1, false);
  sim.step(500);
  sim.press(Input::User1, 150);  // released first: now it works
  sim.step(100);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out2));
}

void relays_never_chatter() {
  Sim sim("t");
  Response p = sim.request(R"({"op":"put_show","data":{"id":2,"name":"c","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"RLY1","cues":[{"at":0,"do":"blink","on_ms":10,"off_ms":10,"count":0}]}]}})");
  TEST_ASSERT_EQUAL_STRING("invalid", p.doc()["error"]["code"] | "");
  TEST_ASSERT_TRUE(p.errorMessage().find("at most every 100 ms") != std::string::npos);
  Response r = script(sim, "effect c:\n  wait for trig1\n  rly1 = on\n  sleep 0.05\n  rly1 = off\n");
  TEST_ASSERT_FALSE(r.doc()["result"]["ok"] | true);
  TEST_ASSERT_EQUAL_STRING("line 5: rly1 switches again after 50 ms. Relays and contacts switch at most every 100 ms: wait at least 0.1 between its changes.",
                           r.doc()["result"]["errors"][0]["message"] | "");
  // a show stepping on, off, on 20 ms apart: off at once, on again only 100 ms after it went off
  ok(sim.request(R"({"op":"put_show","data":{"id":3,"name":"d","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"RLY1","cues":[{"at":0,"do":"on"},{"at":20,"do":"off"},{"at":40,"do":"on"},{"at":500,"do":"off"}]}]}})"));
  sim.step(2500);
  sim.press(Input::Trig2, 150);
  sim.step(600);
  std::vector<std::pair<Millis, Level>> ch;
  for (auto& x : sim.trace) if (x.channel == Channel::Rly1) ch.push_back({x.t, x.level});
  TEST_ASSERT_EQUAL(4, ch.size());
  TEST_ASSERT_TRUE(ch[2].first - ch[1].first >= RELAY_MIN_DWELL_MS);
}

void off_is_not_a_manual_test_that_blocks_updates() {
  Sim sim("t");
  sim.step(2500);
  ok(sim.requestFrom(3, R"({"op":"set_output","channel":"OUT1","level":0})"));
  UpdateRequest q;
  q.has_len = true;
  q.len = 1000;
  q.octet_stream = true;
  q.has_sha256 = true;
  q.sha256 = std::string(64, 'a');
  std::string a;
  TEST_ASSERT_TRUE_MESSAGE(sim.updateBegin(q, a), a.c_str());
  sim.updateAbort();
}

void mqtt_is_read_only_while_the_page_is_locked() {
  Sim sim("t");
  ok(setp(sim, "effect", "\"pulse_and_recover\""));
  ok(sim.request(R"({"op":"set_params","values":[{"key":"pin","value":"123456"},{"key":"page_lock","value":true}]})"));
  std::string p = sim.ctl().mqttPrefix();
  for (const char* t : {"/control/recover", "/control/cancel", "/control/restart", "/cue"}) {
    auto out = sim.ctl().mqttOnMessage(p + t, "x");
    TEST_ASSERT_EQUAL_MESSAGE(1, out.size(), t);
    TEST_ASSERT_TRUE_MESSAGE(out[0].payload.find("locked") != std::string::npos, t);
  }
  TEST_ASSERT_FALSE(sim.ctl().rebootRequested());
}

void wrong_page_passwords_are_throttled() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  Response r = sim.request(R"({"op":"set_params","values":[{"key":"pin","value":"12345"}]})");
  TEST_ASSERT_EQUAL_STRING("the page password needs at least 6 characters", r.errorMessage().c_str());
  ok(sim.request(R"({"op":"set_params","values":[{"key":"pin","value":"123456"},{"key":"page_lock","value":true}]})"));
  const char* bad = R"({"op":"run","show":1,"pin":"000000"})";
  const char* good = R"({"op":"run","show":1,"pin":"123456"})";
  TEST_ASSERT_FALSE(sim.request(bad).isOk());
  r = sim.request(good);  // even the right one waits 1 s
  TEST_ASSERT_EQUAL_STRING("wrong page password: wait 1 s before the next try", r.errorMessage().c_str());
  sim.step(1000);
  TEST_ASSERT_FALSE(sim.request(bad).isOk());  // the wait doubles: 2 s
  sim.step(1000);
  TEST_ASSERT_FALSE(sim.request(good).isOk());
  sim.step(1000);
  ok(sim.request(good));
  ok(sim.request(R"({"op":"stop","show":1})"));
}

void a_failed_save_is_retried_and_reported() {
  Sim sim("t");
  sim.step(3000);
  sim.flash().fail_saves = true;
  ok(setp(sim, "name", "\"Porch\""));
  sim.step(3000);
  JsonDocument st;
  deserializeJson(st, sim.ctl().stateJson());
  TEST_ASSERT_EQUAL(1, st["store_errors"] | 0);
  sim.flash().fail_saves = false;
  int before = sim.flash().saves;
  sim.step(5000);
  TEST_ASSERT_EQUAL(before + 1, sim.flash().saves);
  std::string rec;
  TEST_ASSERT_TRUE(sim.flash().load(rec));
  TEST_ASSERT_TRUE(rec.find("Porch") != std::string::npos);
}

void an_unreadable_settings_record_is_kept() {
  Sim sim("t");
  Response backup = sim.request(R"({"op":"get_backup"})");
  std::string data;
  serializeJson(backup.doc()["result"]["data"], data);
  TEST_ASSERT_TRUE(sim.flash().save("not json"));
  sim.powerCycle();
  sim.step(3000);
  ok(setp(sim, "name", "\"Porch\""));
  sim.step(5000);
  std::string rec;
  TEST_ASSERT_TRUE(sim.flash().load(rec));
  TEST_ASSERT_EQUAL_STRING("not json", rec.c_str());
  JsonDocument st;
  deserializeJson(st, sim.ctl().stateJson());
  TEST_ASSERT_TRUE(std::string(st["store"] | "").find("could not be read and are kept") != std::string::npos);
  Response r = sim.request(R"({"op":"put_backup","data":)" + data + "}");
  TEST_ASSERT_FALSE(r.isOk());  // never "restored" when it cannot be saved
  ok(sim.request(R"({"op":"factory_reset"})"));
  sim.step(100);
  sim.powerCycle();
  sim.step(3000);
  ok(setp(sim, "name", "\"Porch\""));
  sim.step(5000);
  TEST_ASSERT_TRUE(sim.flash().load(rec));
  TEST_ASSERT_TRUE(rec.find("Porch") != std::string::npos);
}

void stored_numbers_out_of_range_get_their_defaults() {
  Sim sim("t");
  TEST_ASSERT_TRUE(sim.flash().save(R"({"version":1,"settings":{"name":"Porch","role_pulse_ms":0,"trig_debounce_ms":4000000000,"pixel_count":999999},"shows":[]})"));
  sim.powerCycle();
  Settings def;
  TEST_ASSERT_EQUAL_STRING("Porch", sim.ctl().settings().name.c_str());  // the record was read
  TEST_ASSERT_EQUAL(def.role_pulse_ms, sim.ctl().settings().role_pulse_ms);
  TEST_ASSERT_EQUAL(def.trig_debounce_ms, sim.ctl().settings().trig_debounce_ms);
  TEST_ASSERT_EQUAL(def.pixel_count, sim.ctl().settings().pixel_count);
}

static size_t g_heap = 0;
void a_request_too_big_for_the_heap_is_refused_in_plain_words() {
  Sim sim("t");
  g_heap = 40 * 1024;
  sim.ctl().setHeapProbe([]() { return g_heap; });
  std::string big = R"({"op":"check_script","text":")" + std::string(6000, '#') + "\"}";
  std::string out = sim.ctl().call(big.data(), big.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("not enough memory for a request this big (40 KB free, about 71 KB needed)") != std::string::npos, out.c_str());
  g_heap = 200 * 1024;
  out = sim.ctl().call(big.data(), big.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.c_str());
  // the largest block (the ESP32): compared with the largest single allocation, not with the whole need, so a
  // 20 KB body passes with 200 KB free in a 110 KB block (it was refused against 4 x body + 48 KB)
  static size_t block = 0;
  sim.ctl().setBlockProbe([]() { return block; });
  std::string body20 = R"({"op":"check_script","text":")" + std::string(20000, '#') + "\"}";
  block = 110 * 1024;
  out = sim.ctl().call(body20.data(), body20.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.c_str());
  block = 30 * 1024;
  out = sim.ctl().call(body20.data(), body20.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("the largest free block is 30 KB, about 43 KB needed in one piece") != std::string::npos, out.c_str());
  sim.ctl().setBlockProbe(nullptr);
  sim.ctl().setHeapProbe(nullptr);
}

// A long answer (a script check with about a hundred messages, 15 KB) is measured before it is written and
// refused in plain words when the heap cannot hold it (hardware 2026-10-23: a 15 KB answer restarted an ESP32);
// a text over 8192 B gets the size message alone (a 20 KB text restarted it in the checker's line copies).
static std::string manyErrors() {
  std::string t;
  for (int e = 0; e < 8; e++) {
    t += "effect effect number " + std::to_string(e) + " with a long name:\n  wait for user" + std::to_string(e % 3 + 1) + "\n";
    for (int i = 0; i < 24; i++) t += "  fade out" + std::to_string(e % 4 + 5) + " to 37.5% over 1.25 s # c\n";  // no such output
  }
  t = t.substr(0, 8100);
  return t.substr(0, t.rfind('\n') + 1);
}
void a_long_answer_too_big_for_the_heap_is_refused_and_a_huge_script_gets_the_size_message() {
  Sim sim("t");
  std::string text = manyErrors();
  std::string body = R"({"v":1,"id":5,"op":"check_script","text":")";
  for (char c : text) body += c == '\n' ? std::string("\\n") : std::string(1, c);
  body += "\"}";
  std::string out = sim.ctl().call(body.data(), body.size());
  TEST_ASSERT_TRUE_MESSAGE(out.size() > 12000 && out.find("\"ok\":true") != std::string::npos, out.substr(0, 200).c_str());
  size_t answer = out.size();
  // the heap shrinks while the request runs (other sessions, the checker's own work): the request's check sees
  // 400 KB, the answer's 20 KB, less than its 15 KB plus the 32 KB spare
  static int probes = 0;
  probes = 0;
  sim.ctl().setHeapProbe([]() -> size_t { return probes++ == 0 ? 400 * 1024 : 20 * 1024; });
  out = sim.ctl().call(body.data(), body.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("the answer is too big for the memory left (20 KB free, about ") != std::string::npos &&
                               out.find("\"id\":5") != std::string::npos && out.find("\"refused\"") != std::string::npos, out.c_str());
  // the largest block likewise
  sim.ctl().setHeapProbe(nullptr);
  static int bprobes = 0;
  bprobes = 0;
  sim.ctl().setBlockProbe([]() -> size_t { return bprobes++ == 0 ? 110 * 1024 : 10 * 1024; });
  out = sim.ctl().call(body.data(), body.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("the answer is too big for the memory left (the largest free block is 10 KB") != std::string::npos, out.c_str());
  sim.ctl().setBlockProbe(nullptr);
  // a short answer is never measured against the heap
  sim.ctl().setHeapProbe([]() -> size_t { return 1024; });
  std::string small = R"({"v":1,"op":"info"})";
  out = sim.ctl().call(small.data(), small.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.c_str());
  sim.ctl().setHeapProbe(nullptr);
  (void)answer;
  // 8193 B: the size message alone, nothing else
  std::string t8193(8193, '#');
  std::string b2 = R"({"v":1,"op":"check_script","text":")" + t8193 + "\"}";
  out = sim.ctl().call(b2.data(), b2.size());
  TEST_ASSERT_TRUE_MESSAGE(out.find("\"errors\":[{\"line\":0,\"message\":\"the script is 8193 bytes. A board takes at most 8192.\"}]") != std::string::npos, out.c_str());
}

// The USB serial line (found on an ESP32: a 50 KB line after earlier large requests doubled the line's
// std::string past 32 KB, the allocation failed and the board aborted; and the capacity stayed held after it).
static SerialLine::Result feed(SerialLine& l, const std::string& s, int* lines = nullptr) {
  SerialLine::Result last = SerialLine::NONE;
  for (char ch : s) {
    SerialLine::Result r = l.push((uint8_t)ch);
    if (r != SerialLine::NONE) last = r;
    if (r == SerialLine::LINE) {
      if (lines) (*lines)++;
      l.done();
    }
  }
  return last;
}

void an_over_long_serial_line_is_dropped_and_answered_once() {
  TEST_ASSERT_EQUAL(42952, SERIAL_LINE_MAX);
  SerialLine l;
  // the largest line allowed is taken whole, in a buffer never larger than the limit
  std::string max = "{" + std::string(SERIAL_LINE_MAX - 1, 'a');
  for (char ch : max) TEST_ASSERT_EQUAL(SerialLine::NONE, l.push((uint8_t)ch));
  TEST_ASSERT_TRUE(l.capacity() <= SERIAL_LINE_MAX);
  TEST_ASSERT_EQUAL(SerialLine::LINE, l.push('\n'));
  TEST_ASSERT_EQUAL(SERIAL_LINE_MAX, l.size());
  TEST_ASSERT_EQUAL_MEMORY(max.data(), l.data(), max.size());
  l.done();
  TEST_ASSERT_FALSE(l.receiving());
  // 44 KB and 50 KB lines: dropped as they come in (the memory goes back at once), one answer each at the end
  // ("\r\n" is one end), never a shorter request
  for (size_t len : {44 * 1024, 50 * 1024}) {
    std::string big = "{" + std::string(len - 1, 'b');
    size_t i = 0;
    for (; i <= SERIAL_LINE_MAX; i++) TEST_ASSERT_EQUAL(SerialLine::NONE, l.push((uint8_t)big[i]));
    TEST_ASSERT_EQUAL(0, l.capacity());
    TEST_ASSERT_TRUE(l.receiving());  // the state stream stays quiet until the line ends
    for (; i < big.size(); i++) TEST_ASSERT_EQUAL(SerialLine::NONE, l.push((uint8_t)big[i]));
    TEST_ASSERT_EQUAL(0, l.capacity());
    TEST_ASSERT_EQUAL(SerialLine::TOO_LONG, l.push('\r'));
    TEST_ASSERT_EQUAL(SerialLine::NONE, l.push('\n'));
    TEST_ASSERT_FALSE(l.receiving());
  }
  // the next line is handled as usual
  TEST_ASSERT_EQUAL(SerialLine::NONE, feed(l, "mem"));
  TEST_ASSERT_EQUAL(SerialLine::LINE, l.push('\n'));
  TEST_ASSERT_TRUE(l.equals("mem"));
  TEST_ASSERT_FALSE(l.equals("me"));
  l.done();
  // empty lines answer nothing
  TEST_ASSERT_EQUAL(SerialLine::NONE, feed(l, "\r\n\n\r"));
  // one byte over the limit is enough: no line comes out of it
  int lines = 0;
  TEST_ASSERT_EQUAL(SerialLine::TOO_LONG, feed(l, std::string(SERIAL_LINE_MAX + 1, ' ') + "\n", &lines));
  TEST_ASSERT_EQUAL(0, lines);
}

void a_long_serial_line_gives_its_memory_back() {
  SerialLine l;
  // a 21 KB request (put_backup of a full board) grows the buffer; done() gives it back
  std::string req = "{" + std::string(21 * 1024, 'x') + "}";
  for (char ch : req) l.push((uint8_t)ch);
  TEST_ASSERT_TRUE(l.capacity() >= req.size() && l.capacity() <= SERIAL_LINE_MAX);
  TEST_ASSERT_EQUAL(SerialLine::LINE, l.push('\n'));
  l.done();
  TEST_ASSERT_TRUE_MESSAGE(l.capacity() <= SERIAL_LINE_KEEP, std::to_string(l.capacity()).c_str());
  // short lines keep their small buffer (no allocation per console command)
  int lines = 0;
  feed(l, "status\nname\n", &lines);
  TEST_ASSERT_EQUAL(2, lines);
  TEST_ASSERT_EQUAL(SERIAL_LINE_KEEP, l.capacity());
}

static size_t g_block = 0;
void a_serial_line_the_heap_cannot_hold_is_refused() {
  SerialLine l;
  g_block = 20 * 1024;  // the largest free block: a line may grow to 12 KB (the margin stays free)
  l.setBlockProbe([]() { return g_block; });
  std::string req = "{" + std::string(15000, 'y');
  for (char ch : req) TEST_ASSERT_EQUAL(SerialLine::NONE, l.push((uint8_t)ch));
  TEST_ASSERT_EQUAL(0, l.capacity());
  TEST_ASSERT_EQUAL(SerialLine::NO_MEMORY, l.push('\n'));
  TEST_ASSERT_FALSE(l.receiving());
  // with the heap back the same line is taken
  g_block = 110 * 1024;
  TEST_ASSERT_EQUAL(SerialLine::NONE, feed(l, req));
  TEST_ASSERT_EQUAL(SerialLine::LINE, l.push('\n'));
  TEST_ASSERT_EQUAL(req.size(), l.size());
  l.done();
}

// Stopping never needs the page password, on any path; going again does. And a held output from a phone page (or
// the desktop app) is let go when its session ends, also on a locked page.
void stopping_is_never_locked_and_a_dropped_page_lets_go() {
  Sim sim("t");
  sim.step(2500);
  ok(sim.request(R"({"op":"set_params","values":[{"key":"pin","value":"654321"}]})"));
  ok(sim.request(R"({"op":"set_params","values":[{"key":"page_lock","value":true},{"key":"effect","value":"pulse_and_recover"}],"pin":"654321"})"));
  TEST_ASSERT_EQUAL_STRING("pin", sim.request(R"({"op":"run","show":1})").doc()["error"]["code"].as<const char*>());
  ok(sim.request(R"({"op":"stop_all"})"));
  ok(sim.request(R"({"op":"pause","on":true})"));
  TEST_ASSERT_EQUAL_STRING("pin", sim.request(R"({"op":"pause","on":false})").doc()["error"]["code"].as<const char*>());
  ok(sim.request(R"({"op":"pause","on":false,"pin":"654321"})"));
  ok(sim.request(R"({"op":"set_output","channel":"OUT3","level":0})"));
  // over MQTT: stop_all through rpc needs no password either (the control topics stay read-only:
  // mqtt_is_read_only_while_the_page_is_locked)
  std::string p = sim.ctl().mqttPrefix();
  auto stop = sim.ctl().mqttOnMessage(p + "/rpc", R"({"v":1,"id":3,"op":"stop_all"})");
  TEST_ASSERT_EQUAL(1, stop.size());
  TEST_ASSERT_TRUE_MESSAGE(stop[0].payload.find("\"ok\":true") != std::string::npos, stop[0].payload.c_str());
  // a page holding OUT3 and RLY1 (with the password) drops: both let go
  ok(sim.requestFrom(9, R"({"op":"set_output","channel":"OUT3","level":1000,"pin":"654321"})"));
  ok(sim.requestFrom(9, R"({"op":"set_output","channel":"RLY1","level":1000,"pin":"654321"})"));
  sim.step(20);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out3));
  sim.sessionClosed(9);  // a WebSocket close or the 5 s heartbeat timeout (test_http) ends here
  sim.step(20);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out3));
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Rly1));
}

// The controller lock (the step takes it every 10 ms) is given back while a request does its long work (the
// parse, the script check): the longest stretch a check_script holds it is a small part of the whole request.
static std::string largestScript() {
  std::string t;
  for (int e = 0; e < SCRIPT_MAX_EFFECTS; e++) {
    t += "effect effect number " + std::to_string(e) + " with a long name:\n  wait for trig1\n";
    for (int i = 0; i < 26; i++) t += "  fade dmx" + std::to_string(e + 1) + " to 37.5% over 1.25 s # c\n";
  }
  return t;
}
using Clock = std::chrono::steady_clock;
static bool g_held = false;
static int g_releases = 0;
static Clock::time_point g_since;
static double g_longest_ms = 0;
static void heldEnds() {
  double ms = std::chrono::duration<double, std::milli>(Clock::now() - g_since).count();
  if (ms > g_longest_ms) g_longest_ms = ms;
}
static int fakeRelease(void*) {
  if (!g_held) return 0;
  g_releases++;
  heldEnds();
  g_held = false;
  return 1;
}
static void fakeRetake(void*, int) { g_held = true; g_since = Clock::now(); }

void long_requests_give_the_lock_back() {
  Sim sim("t");
  ok(setp(sim, "dmx_port", "\"s4\""));
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = largestScript();
  std::string set, check;
  serializeJson(d, set);
  d["op"] = "check_script";
  serializeJson(d, check);
  ok(sim.request(set));
  // 14 shows beside the script: a backup of about 20 KB (QEMU.md finding 1's shape)
  for (int i = 0; i < 14; i++)
    ok(sim.request(std::string(R"({"op":"put_show","data":{"id":)") + std::to_string(20 + i) +
                   R"(,"name":"a show with a longer name","enabled":false,"triggers":[{"on":"every","period_ms":1500}],"tracks":[{"target":"OUT1","cues":[{"at":0,"do":"on"},{"at":300,"do":"off"}]},{"target":"OUT2","cues":[{"at":0,"do":"ramp","to":1000,"ms":400},{"at":900,"do":"off"}]},{"target":"RLY1","cues":[{"at":100,"do":"pulse","ms":200}]},{"target":"PIXEL","cues":[{"at":0,"do":"flicker","color":"#ff6000"},{"at":1000,"do":"off"}]},{"target":"SOUND","cues":[{"at":0,"do":"play","folder":1,"track":1}]}]}})"));
  Response b = sim.request(R"({"op":"get_backup"})");
  JsonDocument p;
  p["op"] = "put_backup";
  p["data"] = b.doc()["result"]["data"];
  std::string backup;
  serializeJson(p, backup);
  sim.ctl().setLockHooks(fakeRelease, fakeRetake, nullptr);
  std::string params = R"({"op":"get_params"})", get_backup = R"({"op":"get_backup"})";
  for (const std::string* body : {&check, &set, &backup, &params, &get_backup}) {
    g_releases = 0;
    g_longest_ms = 0;
    g_held = true;
    Clock::time_point t0 = Clock::now();
    g_since = t0;
    std::string out = sim.ctl().call(body->data(), body->size());
    heldEnds();
    g_held = false;
    double total = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.substr(0, 300).c_str());
    TEST_ASSERT_TRUE(g_releases >= 2);  // the parse and the script check (or building the answer) run without the lock
    // check_script, get_params, get_backup: the work is building and writing out the answer, with the lock given back
    // (on the ESP32 get_params held the step up 32-41 ms, get_backup 21-24 ms: first hardware run)
    if (body == &check || body == &params || body == &get_backup) TEST_ASSERT_TRUE_MESSAGE(g_longest_ms < total / 2, body->c_str());
    char m[160];
    snprintf(m, sizeof m, "%zu B body: longest hold %.2f ms of %.2f ms", body->size(), g_longest_ms, total);
    TEST_MESSAGE(m);
  }
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
}

// pause = show stop (PROTOCOL.md): an output asked for during a pause is refused, and nothing set meanwhile comes on
// at the release (first hardware run, 2026-10-22: it was held "blocked":"paused" and came on at the release)
void nothing_set_during_a_pause_comes_on_at_its_release() {
  Sim sim("t");
  sim.step(1000);
  ok(sim.request(R"({"op":"pause","on":true})"));
  Response r = sim.request(R"({"op":"set_output","channel":"OUT1","level":1000,"ms":5000})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("paused") != std::string::npos, r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_output","channel":"OUT2","level":1000})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_FALSE(sim.request(R"({"op":"pixel","action":{"do":"fill","color":"#ffffff"}})").isOk());
  ok(sim.request(R"({"op":"set_output","channel":"OUT1","level":0})"));  // letting go always works
  ok(sim.request(R"({"op":"set_output","channel":"OUT1","level":null})"));
  sim.ctl().engine().setManual(Channel::Out3, true, FULL);  // a sketch's turnOn during the pause
  sim.step(200);
  ok(sim.request(R"({"op":"pause","on":false})"));
  sim.step(500);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out2));
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out3));
  ok(sim.request(R"({"op":"set_output","channel":"OUT1","level":1000,"ms":300})"));  // after the release: as before
  sim.step(100);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out1));
}

// set_script with "bytes": a text that lost bytes on the way (an overrun serial buffer) is refused, never stored
void a_script_that_lost_bytes_is_not_stored() {
  Sim sim("t");
  const std::string text = "effect a:\n  wait for trig1\n  out3 = on\n  sleep 1\n  out3 = off\n";
  std::string n = std::to_string(text.size());
  Response r = sim.request(R"({"op":"set_script","bytes":)" + std::to_string(text.size() + 40) + R"(,"text":"effect a:\n  wait for trig1\n  out3 = on\n  sleep 1\n  out3 = off\n"})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("nothing was stored") != std::string::npos, r.errorMessage().c_str());
  Response g = sim.request(R"({"op":"get_script"})");
  TEST_ASSERT_EQUAL_STRING("", g.doc()["result"]["text"].as<const char*>());
  ok(sim.request(R"({"op":"set_script","bytes":)" + n + R"(,"text":"effect a:\n  wait for trig1\n  out3 = on\n  sleep 1\n  out3 = off\n"})"));
  g = sim.request(R"({"op":"get_script"})");
  TEST_ASSERT_EQUAL_STRING(text.c_str(), g.doc()["result"]["text"].as<const char*>());
  TEST_ASSERT_FALSE(sim.request(R"({"op":"set_script","bytes":-1,"text":""})").isOk());
}

// the state stream's change test leaves uptime_ms out: an idle board's state is "unchanged"
void the_state_hash_leaves_out_the_uptime() {
  Sim sim("t");
  sim.step(3000);  // the inputs armed
  std::string a = stateEvent(sim.ctl().stateJson());
  sim.step(1234);
  std::string b = stateEvent(sim.ctl().stateJson());
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_TRUE(stateHash(a) == stateHash(b));
  ok(sim.request(R"({"op":"set_output","channel":"OUT3","level":1000,"ms":5000})"));
  sim.step(20);
  TEST_ASSERT_TRUE(stateHash(stateEvent(sim.ctl().stateJson())) != stateHash(b));
}

// trig1_input = sensor_high: a powered sensor whose signal goes high (idle low = not pressed)
void a_sensor_high_trigger_counts_once_and_a_type_change_fires_nothing() {
  Sim sim("t");
  sim.addExample();  // a fresh board has no shows
  bool* pin = sim.world().input_high;  // TRIG1's pin level
  sim.step(3000);
  ok(setp(sim, "trig1_input", "\"sensor_high\""));  // the pin is high (pulled up, nothing plugged in): reads pressed
  sim.step(3000);
  TEST_ASSERT_EQUAL(0, sim.ctl().presses[0]);  // the change fired nothing: a release must come first
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  pin[0] = false;  // the sensor is plugged in, idle low
  sim.step(1000);
  TEST_ASSERT_EQUAL(0, sim.ctl().presses[0]);
  pin[0] = true;  // it sees something
  sim.step(200);
  pin[0] = false;
  sim.step(20);
  TEST_ASSERT_EQUAL(1, sim.ctl().presses[0]);
  TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out1));  // the example show runs
  sim.step(3000);
  ok(setp(sim, "trig1_input", "\"switch\""));  // back: the idle-low pin now reads pressed, and fires nothing
  sim.step(3000);
  TEST_ASSERT_EQUAL(1, sim.ctl().presses[0]);
  // a show script waits for it the same way
  Sim s2("t");
  ok(setp(s2, "trig2_input", "\"sensor_high\""));
  s2.world().input_high[1] = false;  // the sensor idle
  Response r = script(s2, "effect a:\n  wait for trig2\n  out3 = on\n  sleep 1\n  out3 = off\n");
  TEST_ASSERT_TRUE(r.doc()["result"]["ok"] | false);
  s2.step(3000);
  TEST_ASSERT_EQUAL(0, s2.level(Channel::Out3));
  s2.world().input_high[1] = true;
  s2.step(300);
  TEST_ASSERT_EQUAL(FULL, s2.level(Channel::Out3));
}

// finishing an update (the image read back from flash: seconds on a board) gives the controller lock back
static int g_finish_releases = 0;
void finishing_an_update_does_not_hold_the_lock() {
  Sim sim("t");
  sim.step(2500);
  std::string img = sim_image::build("0.3.0", "abc1234", true, 50 * 1024);
  uint8_t d[32];
  sha256((const uint8_t*)img.data(), img.size(), d);
  UpdateRequest q;
  q.has_len = true;
  q.len = img.size();
  q.octet_stream = true;
  q.has_sha256 = true;
  q.sha256 = toHex(d, 32);
  std::string a;
  TEST_ASSERT_TRUE_MESSAGE(sim.ctl().updateBegin(q, a), a.c_str());
  TEST_ASSERT_TRUE_MESSAGE(sim.ctl().updateWrite((const uint8_t*)img.data(), img.size(), a), a.c_str());
  sim.ctl().setLockHooks([](void*) { g_finish_releases++; return 1; }, [](void*, int) {}, nullptr);
  std::string out = sim.ctl().updateFinish();
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.c_str());
  TEST_ASSERT_EQUAL(1, g_finish_releases);
}

// the step never writes the flash (a write stalls it): the save it decides on is done by background(), from the
// loop, which gives the controller lock back while the record is written
static int g_save_releases = 0;
void the_step_never_writes_the_flash() {
  Sim sim("t");
  sim.step(2500);
  int saves = sim.flash().saves;
  ok(sim.request(R"({"op":"put_show","data":{"id":9,"name":"loop","triggers":[{"on":"every","period_ms":1500}],"tracks":[{"target":"OUT1","cues":[{"at":0,"do":"on"},{"at":300,"do":"off"}]}]}})"));
  // only the step, idle and with the show running, past the longest a save waits (60 s)
  Millis t = sim.uptime();
  for (Millis i = 10; i <= 70000; i += 10) sim.ctl().tick(t + i);
  TEST_ASSERT_EQUAL(saves, sim.flash().saves);
  TEST_ASSERT_EQUAL(0, sim.runsFlash().saves);  // the timer show's runs are counted, not written, in the step
  TEST_ASSERT_TRUE(sim.ctl().runs().sincePowerOn() > 0);
  sim.ctl().setLockHooks([](void*) { g_save_releases++; return 1; }, [](void*, int) {}, nullptr);
  sim.ctl().background();
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL(saves + 1, sim.flash().saves);
  TEST_ASSERT_EQUAL(1, sim.runsFlash().saves);  // the lifetime run count (runs.h): waited past its longest delay
  TEST_ASSERT_EQUAL(2, g_save_releases);        // each written with the lock given back
  sim.ctl().background();  // nothing more to write
  TEST_ASSERT_EQUAL(saves + 1, sim.flash().saves);
}

// an MQTT connect's hello (every setting: about 100 messages, several ms on a board) is built without the lock
static int g_hello_releases = 0;
void the_mqtt_hello_is_built_without_the_lock() {
  Sim sim("t");
  sim.step(2500);
  sim.ctl().setLockHooks([](void*) { g_hello_releases++; return 1; }, [](void*, int) {}, nullptr);
  std::vector<MqttMsg> hello = sim.ctl().mqttOnConnect();
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_TRUE(hello.size() > 50);
  TEST_ASSERT_EQUAL(2, g_hello_releases);  // the settings, then the state's JSON, each in a window
}

// a setting change compares the new settings with the stored ones (both written as JSON: 9 ms on an ESP32, a step
// stall on every change on the bench of 2026-10-25) with the lock given back; another board's change, never saved,
// does not compare them at all. The comparison still decides the save.
static int g_cmp_releases = 0;
void a_setting_change_compares_without_the_lock() {
  Sim sim("t");
  sim.step(2500);
  sim.ctl().setLockHooks([](void*) { g_cmp_releases++; return 1; }, [](void*, int) {}, nullptr);
  sim.serial("pixel_speed=120");
  int local = g_cmp_releases;
  g_cmp_releases = 0;
  LinkIn in;
  in.set = true;
  in.from = "front-door";
  in.what = "pixel_speed";
  in.value = "130";
  in.via = "Wi-Fi";
  LinkReply r = sim.ctl().linkIn(in);
  int tell = g_cmp_releases;
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL(1, local);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.text.c_str());
  TEST_ASSERT_EQUAL(0, tell);
  // a change here is saved; the same value again is not
  sim.step(70000);
  sim.ctl().background();
  int saves = sim.flash().saves;
  sim.serial("pixel_speed=120");
  sim.step(70000);
  sim.ctl().background();
  TEST_ASSERT_EQUAL(saves, sim.flash().saves);
  sim.serial("pixel_speed=125");
  sim.step(70000);
  sim.ctl().background();
  TEST_ASSERT_EQUAL(saves + 1, sim.flash().saves);
}

// the state's JSON (a page, the serial port, MQTT) is written in a window: only the copy of what the step changes
// is made with the lock
static int g_state_releases = 0;
void the_state_is_written_without_the_lock() {
  Sim sim("t");
  sim.addExample();
  sim.step(2500);
  std::string before = sim.ctl().stateJson();
  sim.ctl().setLockHooks([](void*) { g_state_releases++; return 1; }, [](void*, int) {}, nullptr);
  std::string st = sim.ctl().stateJson();
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  TEST_ASSERT_EQUAL(1, g_state_releases);
  TEST_ASSERT_EQUAL_STRING(before.c_str(), st.c_str());
}

// set_script and put_backup write the script record with the lock given back: the step (and the 1 ms samples)
// keep running during the flash write, with the old script's outputs let go and no freed table left behind
static Sim* g_wsim = nullptr;
static Millis g_wt = 0;
static int g_writes_at_release = 0, g_unlocked_writes = 0, g_window_ticks = 0;
static bool g_out1_in_window = false, g_out1_now = false, g_refused_now = false;
static int g_refused_windows = 0;
static int scriptWrites() { return g_wsim->scriptFlash().saves + g_wsim->scriptFlash().wipes; }
static int writeRelease(void*) {
  if (!g_held) return 0;
  g_held = false;
  g_writes_at_release = scriptWrites();
  for (int i = 0; i < 5; i++) {  // the step runs meanwhile
    for (int k = 1; k < 10; k++) g_wsim->ctl().sample(g_wt + k);
    g_wt += 10;
    g_wsim->ctl().tick(g_wt);
    g_window_ticks++;
  }
  g_out1_now = g_wsim->level(Channel::Out1) > 0;
  // a call from the step meanwhile (other tasks wait in the lock): a change is refused, stopping is not
  const std::string set = R"({"op":"set_params","values":[{"key":"name","value":"x"}]})", stop = R"({"op":"stop_all"})";
  g_refused_now = g_wsim->ctl().call(set.data(), set.size()).find("busy") != std::string::npos;
  TEST_ASSERT_TRUE(g_wsim->ctl().call(stop.data(), stop.size()).find("\"ok\":true") != std::string::npos);
  return 1;
}
static void writeRetake(void*, int) {
  g_held = true;
  int w = scriptWrites() - g_writes_at_release;
  g_unlocked_writes += w;
  if (w && g_out1_now) g_out1_in_window = true;  // a write window (not a script check's)
  if (w && g_refused_now) g_refused_windows++;
}
void the_step_never_waits_on_a_script_write() {
  const char* a = "effect hold:\n  wait for trig1\n  out1 = on\n  wait for trig2\n  out1 = off\n";
  std::string set_a = std::string(R"({"op":"set_script","text":")") + a + "\"}";
  std::string backup;
  {
    Sim from("t");
    ok(from.request(set_a));
    Response b = from.request(R"({"op":"get_backup"})");
    JsonDocument p;
    p["op"] = "put_backup";
    p["data"] = b.doc()["result"]["data"];
    serializeJson(p, backup);
  }
  std::string set_b = R"({"op":"set_script","text":"effect other:\n  wait for trig2\n  out2 = on\n"})";
  std::string set_none = R"({"op":"set_script","text":""})";
  for (const std::string* body : {&set_b, &backup, &set_none}) {
    Sim sim("t");
    g_wsim = &sim;
    // the old script running, OUT1 on
    ok(sim.request(set_a));
    sim.step(3000);
    sim.press(Input::Trig1, 200);
    sim.step(200);
    TEST_ASSERT_EQUAL(FULL, sim.level(Channel::Out1));
    g_wt = sim.uptime();
    g_unlocked_writes = 0;
    g_refused_windows = 0;
    g_window_ticks = 0;
    g_out1_in_window = false;
    int before = scriptWrites();
    sim.ctl().setLockHooks(writeRelease, writeRetake, nullptr);
    g_held = true;
    std::string out = sim.ctl().call(body->data(), body->size());
    g_held = false;
    sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
    TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.substr(0, 300).c_str());
    int writes = scriptWrites() - before;
    TEST_ASSERT_TRUE(writes >= 1);
    TEST_ASSERT_EQUAL_MESSAGE(writes, g_unlocked_writes, body->substr(0, 40).c_str());  // every one without the lock
    TEST_ASSERT_TRUE(g_window_ticks > 0);
    TEST_ASSERT_FALSE(g_out1_in_window);  // the old script's output let go while the new record is written
    TEST_ASSERT_EQUAL(writes, g_refused_windows);
    g_wsim = nullptr;
  }
}

// a new image's self-check verdict (otadata / the Pico bootloader's state: a flash write) is marked once by
// background(), from the loop, with the lock given back; the step only says it is due
static int g_mark_releases = 0;
void the_step_never_marks_the_new_image() {
  for (bool passes : {true, false}) {
    Sim sim("t");
    sim.slots().slots[1] = sim.slots().slots[0];
    sim.slots().slots[1].fw_build = "next";
    sim.slots().boot_next = 1;
    sim.powerCycle();
    TEST_ASSERT_TRUE(sim.slots().state == BootState::Pending);
    sim.slots().fail_self_check = !passes;
    Millis t = sim.uptime();
    for (; t <= UPDATE_SELF_CHECK_MS + 1000; t += 10) sim.ctl().tick(t);
    TEST_ASSERT_TRUE(sim.slots().state == BootState::Pending);  // only the step ran: nothing marked
    TEST_ASSERT_FALSE(sim.ctl().rebootRequested());
    g_mark_releases = 0;
    sim.ctl().setLockHooks([](void*) { g_mark_releases++; return 1; }, [](void*, int) {}, nullptr);
    sim.ctl().background();
    int releases = g_mark_releases;
    TEST_ASSERT_TRUE(releases >= 1);
    TEST_ASSERT_TRUE(sim.slots().state == (passes ? BootState::Valid : BootState::Pending));
    TEST_ASSERT_EQUAL(!passes, sim.ctl().rebootRequested());
    for (int i = 0; i < 100; i++) sim.ctl().tick(t += 10);
    sim.ctl().background();  // once only
    sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
    TEST_ASSERT_EQUAL(releases, g_mark_releases);
  }
}

// On a board the step itself copies the state every 50 ms while a page or the serial port asks for it (a triple
// buffer: neither side waits), and stateJson only takes that copy: the same JSON as one made on the spot
static int g_snap_releases = 0;
void the_state_comes_from_the_steps_copy() {
  Sim sim("t");
  sim.addExample();
  sim.step(2500);
  sim.ctl().setLockHooks([](void*) { g_snap_releases++; return 1; }, [](void*, int) {}, nullptr);
  (void)sim.ctl().stateJson();  // asks: the step starts copying
  sim.press(Input::Trig1, 150);
  sim.step(200);
  for (int i = 0; i < 4; i++) sim.tick();  // the copy is made on the 5th tick after the last one
  std::string from_step = sim.ctl().stateJson();
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  std::string here = sim.ctl().stateJson();  // no lock: made on the spot, at the same step
  JsonDocument a, b;
  deserializeJson(a, from_step);
  deserializeJson(b, here);
  TEST_ASSERT_TRUE(a["uptime_ms"].as<uint64_t>() <= b["uptime_ms"].as<uint64_t>());
  TEST_ASSERT_TRUE(b["uptime_ms"].as<uint64_t>() - a["uptime_ms"].as<uint64_t>() <= 100);
  TEST_ASSERT_EQUAL_STRING(b["outputs"][0]["driver"] | "", a["outputs"][0]["driver"] | "");
  TEST_ASSERT_EQUAL(b["outputs"][0]["level"].as<int>(), a["outputs"][0]["level"].as<int>());
  TEST_ASSERT_TRUE(a["shows"].size() == b["shows"].size());
  // asked for no more (3 s): the step stops copying, and an old copy is never used
  sim.step(3500);
  sim.ctl().setLockHooks([](void*) { g_snap_releases++; return 1; }, [](void*, int) {}, nullptr);
  std::string late = sim.ctl().stateJson();
  sim.ctl().setLockHooks(nullptr, nullptr, nullptr);
  JsonDocument c;
  deserializeJson(c, late);
  TEST_ASSERT_EQUAL(sim.ctl().now(), c["uptime_ms"].as<uint64_t>());  // made on the spot
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(role_pulses_keep_the_relay_interlock);
  RUN_TEST(role_pulses_keep_the_max_on_time);
  RUN_TEST(effect_outputs_keep_the_max_on_time);
  RUN_TEST(stop_all_and_pause_stop_script_effects_for_good);
  RUN_TEST(stop_all_and_pause_end_the_effect_cycle_and_role_pulses);
  RUN_TEST(releasing_pause_leaves_manual_outputs_off);
  RUN_TEST(stopping_needs_no_page_password);
  RUN_TEST(stop_all_then_update);
  RUN_TEST(manual_outputs_end_with_the_page_that_set_them);
  RUN_TEST(http_and_mqtt_switch_outputs_on_only_as_pulses);
  RUN_TEST(flash_saves_wait_until_nothing_runs);
  RUN_TEST(factory_reset_erases_the_show_script);
  RUN_TEST(a_button_held_at_power_on_starts_nothing);
  RUN_TEST(relays_never_chatter);
  RUN_TEST(off_is_not_a_manual_test_that_blocks_updates);
  RUN_TEST(mqtt_is_read_only_while_the_page_is_locked);
  RUN_TEST(wrong_page_passwords_are_throttled);
  RUN_TEST(a_failed_save_is_retried_and_reported);
  RUN_TEST(an_unreadable_settings_record_is_kept);
  RUN_TEST(stored_numbers_out_of_range_get_their_defaults);
  RUN_TEST(a_request_too_big_for_the_heap_is_refused_in_plain_words);
  RUN_TEST(a_long_answer_too_big_for_the_heap_is_refused_and_a_huge_script_gets_the_size_message);
  RUN_TEST(an_over_long_serial_line_is_dropped_and_answered_once);
  RUN_TEST(a_long_serial_line_gives_its_memory_back);
  RUN_TEST(a_serial_line_the_heap_cannot_hold_is_refused);
  RUN_TEST(stopping_is_never_locked_and_a_dropped_page_lets_go);
  RUN_TEST(long_requests_give_the_lock_back);
  RUN_TEST(nothing_set_during_a_pause_comes_on_at_its_release);
  RUN_TEST(a_script_that_lost_bytes_is_not_stored);
  RUN_TEST(the_state_hash_leaves_out_the_uptime);
  RUN_TEST(a_sensor_high_trigger_counts_once_and_a_type_change_fires_nothing);
  RUN_TEST(finishing_an_update_does_not_hold_the_lock);
  RUN_TEST(the_step_never_writes_the_flash);
  RUN_TEST(the_mqtt_hello_is_built_without_the_lock);
  RUN_TEST(a_setting_change_compares_without_the_lock);
  RUN_TEST(the_state_is_written_without_the_lock);
  RUN_TEST(the_state_comes_from_the_steps_copy);
  RUN_TEST(the_step_never_waits_on_a_script_write);
  RUN_TEST(the_step_never_marks_the_new_image);
  return UNITY_END();
}
