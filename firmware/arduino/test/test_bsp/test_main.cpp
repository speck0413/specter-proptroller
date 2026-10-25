// BSP tests: inputs, debouncing, outputs, the DFPlayer protocol.
#include <unity.h>
#include <algorithm>
#include <vector>
#include "proptroller/board.h"
#include "proptroller/dfplayer.h"
#include "proptroller/input.h"
#include "proptroller/pins.h"
#include "proptroller/status.h"
#include "proptroller/ws2812.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static bool unique(const uint8_t* v, int n) {
  std::vector<uint8_t> s(v, v + n);
  std::sort(s.begin(), s.end());
  return std::adjacent_find(s.begin(), s.end()) == s.end();
}
static bool contains(const uint8_t* v, int n, uint8_t p) { return std::find(v, v + n, p) != v + n; }

void maps_have_no_duplicate_pins() {
  uint8_t a[22];
  allPins(ESP32_DEVKIT, a);
  TEST_ASSERT_TRUE(unique(a, 22));
  allPins(PICO, a);
  TEST_ASSERT_TRUE(unique(a, 22));
  for (auto p : PICO_ONLY_EXPANSION) TEST_ASSERT_FALSE(contains(a, 22, p));
}

void esp32_outputs_avoid_input_only_and_strapping_pins() {
  uint8_t o[11];
  outputPins(ESP32_DEVKIT, o);
  for (auto p : o) TEST_ASSERT_FALSE(contains(ESP32_INPUT_ONLY, 6, p));
  const uint8_t power[8] = {23, 22, 21, 19, 25, 26, 27, 32};
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(power[i], ESP32_DEVKIT.out[i]);
  for (auto p : power) TEST_ASSERT_FALSE(contains(ESP32_STRAPPING, 5, p));
}

void esp32_internal_pullups_only_where_possible() {
  for (int k = 0; k < 5; k++)
    if (!inputExternalPull(ESP32_DEVKIT, k)) TEST_ASSERT_FALSE(contains(ESP32_INPUT_ONLY, 6, inputPin(ESP32_DEVKIT, k)));
  TEST_ASSERT_TRUE(contains(ESP32_INPUT_ONLY, 6, ESP32_DEVKIT.trig[0]));
  TEST_ASSERT_TRUE(inputExternalPull(ESP32_DEVKIT, 0));
  TEST_ASSERT_TRUE(internalPullsPossible(ESP32_DEVKIT));
}

void readme_gpio_map() {
  TEST_ASSERT_EQUAL(23, ESP32_DEVKIT.out[0]);
  TEST_ASSERT_EQUAL(15, PICO.out[0]);
  TEST_ASSERT_EQUAL(17, ESP32_DEVKIT.df_tx);
  TEST_ASSERT_EQUAL(34, ESP32_DEVKIT.df_rx);
  TEST_ASSERT_EQUAL(8, PICO.df_tx);
  TEST_ASSERT_EQUAL(7, PICO.df_rx);
  TEST_ASSERT_EQUAL(2, ESP32_DEVKIT.status_led);
}

void names_round_trip() {
  for (auto c : ALL_CHANNELS) {
    Channel x;
    TEST_ASSERT_TRUE(parseChannel(name(c), x));
    TEST_ASSERT_TRUE(x == c);
  }
  for (auto i : ALL_INPUTS) {
    Input x;
    TEST_ASSERT_TRUE(parseInput(name(i), x));
    TEST_ASSERT_TRUE(x == i);
  }
  Channel x;
  TEST_ASSERT_TRUE(parseChannel("contact2", x) && x == Channel::Cc2);
  TEST_ASSERT_FALSE(parseChannel("out9", x));
  TEST_ASSERT_FALSE(parseChannel("averyveryverylongname", x));
  TEST_ASSERT_TRUE(isPwm(Channel::Out4));
  TEST_ASSERT_FALSE(isPwm(Channel::Rly1));
}

// ---------------------------------------------------------------- input
template <typename T, typename E, typename F>
static void runLevel(T& x, F f, Millis from, Millis to, bool level, std::vector<std::pair<Millis, E>>& out) {
  for (Millis t = from; t < to; t++) {
    E e = f(x, level, t);
    if (!(e == E())) out.push_back({t, e});
  }
}

void debounce_rejects_glitches() {
  Debouncer d(20);
  std::vector<std::pair<Millis, Edge>> ev;
  auto f = [](Debouncer& db, bool l, Millis t) { return db.update(l, t); };
  runLevel(d, f, 0, 100, false, ev);
  runLevel(d, f, 100, 110, true, ev);
  runLevel(d, f, 110, 200, false, ev);
  TEST_ASSERT_EQUAL(0, ev.size());
  runLevel(d, f, 200, 300, true, ev);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(220, ev[0].first);
  TEST_ASSERT_TRUE(ev[0].second == Edge::Rise);
}

void trigger_held_at_boot_never_triggers() {
  Trigger tr(20, TRIGGER_DEBOUNCE_MS);  // (a 20 ms debounce keeps the numbers short; the default is 100 ms)
  std::vector<std::pair<Millis, TriggerEvent>> ev;
  auto f = [](Trigger& x, bool l, Millis t) { return x.update(l, t); };
  runLevel(tr, f, 0, 5000, true, ev);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(20, ev[0].first);
  TEST_ASSERT_TRUE(ev[0].second == TriggerEvent::IgnoredNotArmed);
  TEST_ASSERT_TRUE(tr.waitingForRelease());
  runLevel(tr, f, 5000, 5070, false, ev);
  runLevel(tr, f, 5070, 5200, true, ev);
  TEST_ASSERT_EQUAL(5020, ev[1].first);
  TEST_ASSERT_TRUE(ev[1].second == TriggerEvent::Released);
  TEST_ASSERT_EQUAL(5090, ev[2].first);
  TEST_ASSERT_TRUE(ev[2].second == TriggerEvent::IgnoredNotArmed);
  runLevel(tr, f, 5200, 5400, false, ev);
  TEST_ASSERT_TRUE(tr.isReady());
  runLevel(tr, f, 5400, 5500, true, ev);
  TEST_ASSERT_EQUAL(5420, ev.back().first);
  TEST_ASSERT_TRUE(ev.back().second == TriggerEvent::Pressed);
}

void trigger_released_at_boot_arms_after_release_time() {
  Trigger tr;
  TEST_ASSERT_FALSE(tr.isReady());
  for (Millis t = 0; t < 99; t++) tr.update(false, t);
  TEST_ASSERT_FALSE(tr.isReady());
  tr.update(false, 100);
  TEST_ASSERT_TRUE(tr.isReady());
}

void trigger_one_press_one_event() {
  Trigger tr;
  std::vector<std::pair<Millis, TriggerEvent>> ev;
  auto f = [](Trigger& x, bool l, Millis t) { return x.update(l, t); };
  runLevel(tr, f, 0, 200, false, ev);
  runLevel(tr, f, 200, 3000, true, ev);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL_MESSAGE(200 + TRIGGER_DEBOUNCE_MS, ev[0].first, "the default debounce is 100 ms");
  TEST_ASSERT_TRUE(ev[0].second == TriggerEvent::Pressed);
}

struct BE {
  ButtonEvent e;
  bool operator==(const BE& o) const { return e.kind == o.e.kind && e.held_ms == o.e.held_ms; }
};

void button_click_and_long_press() {
  Button b;
  std::vector<std::pair<Millis, BE>> ev;
  auto f = [](Button& x, bool l, Millis t) { return BE{x.update(l, t)}; };
  runLevel(b, f, 0, 100, false, ev);
  runLevel(b, f, 100, 300, true, ev);
  runLevel(b, f, 300, 400, false, ev);
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL(130, ev[0].first);
  TEST_ASSERT_TRUE(ev[0].second.e == ButtonEventKind::Pressed);
  TEST_ASSERT_EQUAL(330, ev[1].first);
  TEST_ASSERT_TRUE(ev[1].second.e == ButtonEventKind::Click);
  ev.clear();
  runLevel(b, f, 400, 2000, true, ev);
  uint32_t held = 0;
  TEST_ASSERT_TRUE(b.heldMs(2000, held));
  TEST_ASSERT_EQUAL(1570, held);
  runLevel(b, f, 2000, 2100, false, ev);
  TEST_ASSERT_EQUAL(3, ev.size());
  TEST_ASSERT_EQUAL(430, ev[0].first);
  TEST_ASSERT_EQUAL(1430, ev[1].first);
  TEST_ASSERT_TRUE(ev[1].second.e == ButtonEventKind::LongPress);
  TEST_ASSERT_EQUAL(2030, ev[2].first);
  TEST_ASSERT_TRUE(ev[2].second.e == ButtonEventKind::Released);
  TEST_ASSERT_EQUAL(1600, ev[2].second.e.held_ms);
}

// ---------------------------------------------------------------- status
static int countRising(Pattern p, uint64_t from, uint64_t to) {
  bool last = p.level(from);
  int n = 0;
  for (uint64_t t = from + 1; t < to; t++) {
    bool l = p.level(t);
    if (l && !last) n++;
    last = l;
  }
  return n;
}

void status_frequencies() {
  TEST_ASSERT_EQUAL(39, countRising(Pattern(Pattern::Kind::FastBlink), 0, 10000));
  TEST_ASSERT_EQUAL(9, countRising(Pattern(Pattern::Kind::SlowBlink), 0, 10000));
  TEST_ASSERT_EQUAL(7, countRising(Pattern(Pattern::Kind::Flicker), 0, 1000));
  TEST_ASSERT_EQUAL(0, countRising(Pattern(Pattern::Kind::Steady), 0, 1000));
  TEST_ASSERT_FALSE(Pattern(Pattern::Kind::Off).level(123));
  TEST_ASSERT_TRUE(Pattern(Pattern::Kind::Steady).level(123));
}

void multi_flash_patterns() {
  TEST_ASSERT_EQUAL(2, countRising(Pattern(Pattern::Kind::DoubleBlink), 999, 1999));
  TEST_ASSERT_EQUAL(3, countRising(Pattern(Pattern::Kind::TripleBlink), 999, 1999));
  TEST_ASSERT_EQUAL(4, countRising(Pattern(Pattern::Kind::Count, 4), 2199, 2199 + 2200));
}

// ---------------------------------------------------------------- ws2812
void inverted_idle_and_bits() {
  const Encoder& e = PROPTROLLER_PIXEL;
  TEST_ASSERT_TRUE(e.idleLevel());
  Pulse one = e.bitPulse(true);
  TEST_ASSERT_FALSE(one.level0); TEST_ASSERT_EQUAL(800, one.ns0); TEST_ASSERT_TRUE(one.level1); TEST_ASSERT_EQUAL(450, one.ns1);
  Pulse zero = e.bitPulse(false);
  TEST_ASSERT_FALSE(zero.level0); TEST_ASSERT_EQUAL(400, zero.ns0); TEST_ASSERT_TRUE(zero.level1); TEST_ASSERT_EQUAL(850, zero.ns1);
  Pulse l = e.latch();
  TEST_ASSERT_TRUE(l.level0 && l.level1);
  TEST_ASSERT_TRUE(l.ns0 + l.ns1 >= 280000);
}

void non_inverted_for_reference() {
  Encoder e = {false, WS2812B};
  TEST_ASSERT_FALSE(e.idleLevel());
  TEST_ASSERT_TRUE(e.bitPulse(true).level0);
  TEST_ASSERT_FALSE(e.latch().level0);
}

void grb_order_and_round_trip() {
  const Encoder& e = PROPTROLLER_PIXEL;
  Rgb px[3] = {Rgb(0xFF, 0, 0), Rgb(0x12, 0x34, 0x56), Rgb(1, 2, 3)};
  std::vector<Pulse> p;
  e.pulses(px, 3, p);
  TEST_ASSERT_EQUAL(Encoder::pulseCount(3), p.size());
  for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL(400, p[i].ns0);
  for (int i = 8; i < 16; i++) TEST_ASSERT_EQUAL(800, p[i].ns0);
  std::vector<Rgb> out;
  TEST_ASSERT_TRUE(e.decode(p, out));
  TEST_ASSERT_EQUAL(3, out.size());
  for (int i = 0; i < 3; i++) TEST_ASSERT_TRUE(out[i] == px[i]);
  Encoder plain = {false, WS2812B};
  TEST_ASSERT_FALSE(plain.decode(p, out));
}

// ---------------------------------------------------------------- dfplayer
struct FakeUart : ByteStream {
  std::vector<uint8_t> tx, rx;
  int available() override { return (int)rx.size(); }
  int read() override {
    if (rx.empty()) return -1;
    int b = rx.front();
    rx.erase(rx.begin());
    return b;
  }
  size_t write(const uint8_t* b, size_t n) override { tx.insert(tx.end(), b, b + n); return n; }
};

static std::vector<uint8_t> reply(uint8_t c, uint16_t p) {
  uint8_t f[10];
  size_t n = df::frame(c, false, p, true, f);
  return std::vector<uint8_t>(f, f + n);
}
static void append(std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); }

void checksum_matches_reference_frames() {
  uint8_t f[10];
  size_t n = df::frame(df::cmd::VOLUME, false, 30, true, f);
  const uint8_t a[] = {0x7E, 0xFF, 0x06, 0x06, 0x00, 0x00, 0x1E, 0xFE, 0xD7, 0xEF};
  TEST_ASSERT_EQUAL(10, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(a, f, 10);
  n = df::frame(df::cmd::PLAY_FOLDER, true, 0x0101, true, f);
  const uint8_t b[] = {0x7E, 0xFF, 0x06, 0x0F, 0x01, 0x01, 0x01, 0xFE, 0xE9, 0xEF};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(b, f, 10);
  n = df::frame(df::cmd::STOP, false, 0, false, f);
  const uint8_t c[] = {0x7E, 0xFF, 0x06, 0x16, 0x00, 0x00, 0x00, 0xEF};
  TEST_ASSERT_EQUAL(8, n);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(c, f, 8);
}

void parser_checks_checksum_and_resyncs() {
  DfParser p;
  std::vector<uint8_t> good = reply(df::msg::FINISHED_SD, 7);
  std::vector<uint8_t> stream = {0x00, 0x13, 0x7E, 0x00};
  append(stream, good);
  std::vector<uint8_t> bad = good;
  bad[8] ^= 1;
  append(stream, bad);
  append(stream, good);
  int got = 0;
  for (uint8_t b : stream) {
    uint8_t c;
    uint16_t pa;
    if (p.push(b, c, pa)) { TEST_ASSERT_EQUAL(df::msg::FINISHED_SD, c); TEST_ASSERT_EQUAL(7, pa); got++; }
  }
  TEST_ASSERT_EQUAL(2, got);
  TEST_ASSERT_EQUAL(2, p.errors);
  uint8_t f[10];
  size_t n = df::frame(df::msg::ACK, false, 0, false, f);
  got = 0;
  for (size_t i = 0; i < n; i++) {
    uint8_t c;
    uint16_t pa;
    if (p.push(f[i], c, pa)) { TEST_ASSERT_EQUAL(df::msg::ACK, c); got++; }
  }
  TEST_ASSERT_EQUAL(1, got);
}

void waits_for_init_delay_and_gaps() {
  DfConfig cfg;
  cfg.use_ack = false;
  DfPlayer d(cfg);
  FakeUart u;
  d.command(SoundCommand::volume(20));
  d.command(SoundCommand::playFolder(2, 3));
  std::vector<DfEvent> ev;
  d.poll(u, 1000, ev);
  TEST_ASSERT_EQUAL(0, u.tx.size());
  d.poll(u, 1500, ev);
  TEST_ASSERT_EQUAL(10, u.tx.size());
  d.poll(u, 1510, ev);
  TEST_ASSERT_EQUAL(10, u.tx.size());
  d.poll(u, 1530, ev);
  TEST_ASSERT_EQUAL(20, u.tx.size());
  const uint8_t want[] = {0x7E, 0xFF, 0x06, 0x0F, 0x00, 0x02, 0x03};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, &u.tx[10], 7);
  TEST_ASSERT_TRUE(d.isPlaying());
}

void ack_timeout_retry_then_give_up() {
  DfPlayer d;
  FakeUart u;
  d.command(SoundCommand::simple(SoundCommand::Kind::Stop));
  d.command(SoundCommand::volume(5));
  std::vector<DfEvent> ev;
  d.poll(u, 2000, ev);
  TEST_ASSERT_EQUAL(10, u.tx.size());
  TEST_ASSERT_EQUAL(1, u.tx[4]);
  d.poll(u, 2100, ev);
  TEST_ASSERT_EQUAL(10, u.tx.size());
  d.poll(u, 2200, ev);
  TEST_ASSERT_EQUAL(20, u.tx.size());
  TEST_ASSERT_EQUAL(df::cmd::STOP, u.tx[13]);
  d.poll(u, 2400, ev);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_TRUE(ev[0].kind == DfEvent::Kind::Timeout && ev[0].value == df::cmd::STOP);
  TEST_ASSERT_EQUAL(30, u.tx.size());
  TEST_ASSERT_EQUAL(df::cmd::VOLUME, u.tx[23]);
  u.rx = reply(df::msg::ACK, 0);
  d.poll(u, 2450, ev);
  TEST_ASSERT_TRUE(ev.back().kind == DfEvent::Kind::Ack);
  TEST_ASSERT_EQUAL(0, d.queued());
}

void finished_dedup_and_events() {
  DfConfig cfg;
  cfg.use_ack = false;
  DfPlayer d(cfg);
  FakeUart u;
  d.command(SoundCommand::playIndex(4));
  TEST_ASSERT_TRUE(d.isPlaying());
  std::vector<DfEvent> ev;
  u.rx = reply(df::msg::FINISHED_SD, 4);
  append(u.rx, reply(df::msg::FINISHED_SD, 4));
  d.poll(u, 5000, ev);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_TRUE(ev[0].kind == DfEvent::Kind::Finished && ev[0].value == 4);
  TEST_ASSERT_FALSE(d.isPlaying());
  u.rx = reply(df::msg::ONLINE, 2);
  append(u.rx, reply(df::msg::ERROR, 6));
  append(u.rx, reply(0x43, 25));
  d.poll(u, 6000, ev);
  TEST_ASSERT_EQUAL(4, ev.size());
  TEST_ASSERT_TRUE(ev[1].kind == DfEvent::Kind::Online && ev[1].value == 2);
  TEST_ASSERT_TRUE(ev[2].kind == DfEvent::Kind::Error && ev[2].value == 6);
  TEST_ASSERT_TRUE(ev[3].kind == DfEvent::Kind::Reply && ev[3].value == 0x43 && ev[3].param == 25);
  TEST_ASSERT_TRUE(d.isOnline());
}

void stop_clears_queue_and_clamps() {
  DfPlayer d;
  for (int i = 0; i < 20; i++) d.command(SoundCommand::simple(SoundCommand::Kind::Next));
  TEST_ASSERT_EQUAL(16, d.queued());
  TEST_ASSERT_EQUAL(4, d.dropped);
  d.command(SoundCommand::simple(SoundCommand::Kind::Stop));
  TEST_ASSERT_EQUAL(1, d.queued());
  uint8_t c;
  uint16_t p;
  SoundCommand::volume(99).encode(c, p);
  TEST_ASSERT_EQUAL(df::cmd::VOLUME, c);
  TEST_ASSERT_EQUAL(30, p);
  SoundCommand::playFolder(0, 0).encode(c, p);
  TEST_ASSERT_EQUAL(df::cmd::PLAY_FOLDER, c);
  TEST_ASSERT_EQUAL(0x0101, p);
}

// ---------------------------------------------------------------- board
struct FakePins : BoardPins {
  uint16_t duty[4] = {1023, 1023, 1023, 1023};
  int writes[4] = {0, 0, 0, 0};
  bool dig[4] = {true, true, true, true};
  bool led = true;
  bool high[5] = {true, true, true, true, true};
  bool setOut(int i, Level l) override { duty[i] = (uint16_t)((1023u * l + 500) / 1000); writes[i]++; return true; }
  bool setDigital(int i, bool on) override { dig[i] = on; return true; }
  bool readLow(int i, bool& low) override { low = !high[i]; return true; }
  bool hasStatusLed() override { return true; }
  bool setStatusLed(bool on) override { led = on; return true; }
};

void boot_all_off() {
  FakePins p;
  Board b(p);
  for (auto d : p.duty) TEST_ASSERT_EQUAL(0, d);
  for (auto d : p.dig) TEST_ASSERT_FALSE(d);
  TEST_ASSERT_FALSE(p.led);
  for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL(0, b.levels()[i]);
}

void apply_levels_and_inputs() {
  FakePins p;
  Board b(p);
  Level req[8] = {1000, 500, 0, 0, 1, 0, 1000, 0};
  Level got[8];
  b.apply(0, req, got);
  Level want[8] = {1000, 500, 0, 0, 1000, 0, 1000, 0};
  TEST_ASSERT_EQUAL_UINT16_ARRAY(want, got, 8);
  TEST_ASSERT_EQUAL(1023, p.duty[0]);
  TEST_ASSERT_EQUAL(512, p.duty[1]);
  TEST_ASSERT_TRUE(p.dig[0]);
  TEST_ASSERT_FALSE(p.dig[1]);
  TEST_ASSERT_TRUE(p.dig[2]);
  int writes = p.writes[0];
  b.apply(10, req);
  TEST_ASSERT_EQUAL(writes, p.writes[0]);
  p.high[0] = false;
  bool in[5];
  b.readInputs(in);
  TEST_ASSERT_TRUE(in[0]);
  for (int i = 1; i < 5; i++) TEST_ASSERT_FALSE(in[i]);
  b.setStatusLed(true);
  TEST_ASSERT_TRUE(p.led);
  b.allOff();
  TEST_ASSERT_FALSE(p.led);
  TEST_ASSERT_EQUAL(0, p.duty[1]);
}

// The step calls apply() and setStatusLed() every tick: the same request again touches no pin, but a relay held
// off for its dwell still comes on when the dwell is over.
struct CountingPins : FakePins {
  int dig_writes[4] = {0, 0, 0, 0}, led_writes = 0;
  bool setDigital(int i, bool on) override { dig_writes[i]++; dig[i] = on; return true; }
  bool setStatusLed(bool on) override { led_writes++; led = on; return true; }
};
void the_same_request_again_touches_no_pin() {
  CountingPins p;
  Board b(p);
  Level on[8] = {500, 0, 0, 0, FULL, 0, 0, 0};
  b.apply(0, on);
  int w0 = p.writes[0], d0 = p.dig_writes[0];
  for (Millis t = 10; t < 1000; t += 10) b.apply(t, on);
  TEST_ASSERT_EQUAL(w0, p.writes[0]);
  TEST_ASSERT_EQUAL(d0, p.dig_writes[0]);
  // RLY1 off, then on again 20 ms later: held off for the dwell, on once it is over, with the same request each tick
  Level off[8] = {500, 0, 0, 0, 0, 0, 0, 0};
  b.apply(1000, off);
  TEST_ASSERT_FALSE(p.dig[0]);
  Millis on_at = 0;
  for (Millis t = 1020; t < 1300 && !on_at; t += 10) {
    b.apply(t, on);
    if (p.dig[0]) on_at = t;
  }
  TEST_ASSERT_EQUAL(1000 + RELAY_MIN_DWELL_MS, on_at);
  // the status LED: written when it changes only
  int l0 = p.led_writes;
  for (int i = 0; i < 10; i++) b.setStatusLed(true);
  TEST_ASSERT_EQUAL(l0 + 1, p.led_writes);
  b.setStatusLed(false);
  TEST_ASSERT_EQUAL(l0 + 2, p.led_writes);
}

// ---------------------------------------------------------------- outputs (SM-S3: no current checks)
void any_out_dims_at_any_level() {
  FakePins p;
  Board b(p);
  for (Level l : {Level(1), Level(300), Level(999), Level(1000)}) TEST_ASSERT_EQUAL(l, b.set(0, Channel::Out1, l));
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(700, b.set(0, ALL_CHANNELS[i], 700));
  TEST_ASSERT_EQUAL(1000, b.set(0, Channel::Out2, 5000));  // clamped to full
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(any_out_dims_at_any_level);
  RUN_TEST(maps_have_no_duplicate_pins);
  RUN_TEST(esp32_outputs_avoid_input_only_and_strapping_pins);
  RUN_TEST(esp32_internal_pullups_only_where_possible);
  RUN_TEST(readme_gpio_map);
  RUN_TEST(names_round_trip);
  RUN_TEST(debounce_rejects_glitches);
  RUN_TEST(trigger_held_at_boot_never_triggers);
  RUN_TEST(trigger_released_at_boot_arms_after_release_time);
  RUN_TEST(trigger_one_press_one_event);
  RUN_TEST(button_click_and_long_press);
  RUN_TEST(status_frequencies);
  RUN_TEST(multi_flash_patterns);
  RUN_TEST(inverted_idle_and_bits);
  RUN_TEST(non_inverted_for_reference);
  RUN_TEST(grb_order_and_round_trip);
  RUN_TEST(checksum_matches_reference_frames);
  RUN_TEST(parser_checks_checksum_and_resyncs);
  RUN_TEST(waits_for_init_delay_and_gaps);
  RUN_TEST(ack_timeout_retry_then_give_up);
  RUN_TEST(finished_dedup_and_events);
  RUN_TEST(stop_clears_queue_and_clamps);
  RUN_TEST(boot_all_off);
  RUN_TEST(apply_levels_and_inputs);
  RUN_TEST(the_same_request_again_touches_no_pin);
  return UNITY_END();
}
