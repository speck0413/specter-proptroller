// The show runs whatever the network does (owner rule, 2026-10-21). The boards' structure on threads: a step
// thread ticks the controller every 10 ms of wall time under the controller lock (the ESP32's step task, the
// RP2040's core 1), and network threads reach the controller only through LockedApp, as arduino/net.cpp does:
//  * a phone whose socket stalls for 20 s right after the server called the firmware (a write that blocks),
//  * a broker that never answers (a publish stalls for 20 s after mqttPoll()),
//  * no Wi-Fi at all (the network does nothing).
// Meanwhile a trigger starts the example show on time, stop_all stops it within a step, and a show script's
// effect runs with its exact timing; the step never falls behind wall time.
#include <unity.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "proptroller/net/http.h"
#include "proptroller/net/locked.h"
#include "proptroller/sim/sim.h"

using namespace pt;
using Clock = std::chrono::steady_clock;
void setUp() {}
void tearDown() {}

static const int STALL_MS = 20000;
typedef LockedApp<std::mutex, std::lock_guard<std::mutex>> Locked;

struct StallConn : Conn {
  std::string in = "GET /api/v1/state HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
  std::atomic<bool>& stalled;
  explicit StallConn(std::atomic<bool>& s) : stalled(s) {}
  int read(uint8_t* b, size_t n) override {
    size_t k = in.size() < n ? in.size() : n;
    memcpy(b, in.data(), k);
    in.erase(0, k);
    return (int)k;
  }
  int write(const uint8_t*, size_t n) override {
    if (!stalled.exchange(true)) std::this_thread::sleep_for(std::chrono::milliseconds(STALL_MS));
    return (int)n;
  }
  void close() override {}
};

struct Rig {
  Sim sim{"iso"};
  std::mutex lock;
  Locked locked{sim, lock};
  std::atomic<bool> stop{false}, http_stalled{false}, mqtt_stalled{false};
  std::atomic<long> max_gap_ms{0}, behind_ms{0};
  Clock::time_point t0 = Clock::now();
  Millis sim0 = 0;
  std::thread step;
  void start() {
    sim0 = sim.now();
    t0 = Clock::now();
    step = std::thread([this] {
      Clock::time_point next = t0, last = t0;
      while (!stop) {
        next += std::chrono::milliseconds(Sim::TICK_MS);
        std::this_thread::sleep_until(next);
        Clock::time_point now = Clock::now();
        long gap = (long)std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        last = now;
        if (gap > max_gap_ms) max_gap_ms = gap;
        std::lock_guard<std::mutex> g(lock);
        sim.tick();
        long wall = (long)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
        long behind = wall - (long)(sim.now() - sim0);
        if (behind > behind_ms) behind_ms = behind;
      }
    });
  }
  std::string call(const std::string& j) { return locked.call(j.data(), j.size()); }
  void wait(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
  void input(Input i, bool on) { std::lock_guard<std::mutex> g(lock); sim.setInput(i, on); }
  Millis now() { std::lock_guard<std::mutex> g(lock); return sim.now(); }
  // the first change of a channel at or after t (sim time), or 0
  Millis changeAt(Channel c, Millis t, Level level) {
    std::lock_guard<std::mutex> g(lock);
    for (auto& x : sim.trace)
      if (x.channel == c && x.t >= t && x.level == level) return x.t;
    return 0;
  }
};

void show_keeps_time_while_the_network_stalls() {
  Rig b;
  b.sim.addExample();  // a fresh board has no shows
  // the example show (TRIG1: OUT1 for 2 s) without its cooldown, so it can run twice
  JsonDocument d;
  deserializeJson(d, b.sim.request(R"({"op":"get_show","show":1})").json());
  d["result"]["data"]["cooldown_ms"] = 0;
  std::string show;
  serializeJson(d["result"]["data"], show);
  TEST_ASSERT_TRUE(b.sim.request("{\"op\":\"put_show\",\"data\":" + show + "}").isOk());
  b.sim.step(2000);  // armed
  b.start();
  // a phone whose socket stalls for 20 s
  std::thread net([&] {
    HttpServer http(b.locked);
    http.add(new StallConn(b.http_stalled), 0);
    Clock::time_point end = Clock::now() + std::chrono::milliseconds(STALL_MS + 500);
    while (Clock::now() < end) {
      http.poll((Millis)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - b.t0).count());
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });
  // a broker that never answers: the MQTT messages are taken from the firmware, the publish hangs
  std::thread mqtt([&] {
    std::vector<MqttMsg> out = b.locked.mqttPoll();
    b.mqtt_stalled = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(STALL_MS));
  });
  while (!b.http_stalled || !b.mqtt_stalled) b.wait(1);

  // 1. TRIG1 starts the example show: OUT1 on after the 100 ms debounce (+ one step), for 2000 ms
  Millis t = b.now();
  b.input(Input::Trig1, true);
  b.wait(150);
  b.input(Input::Trig1, false);
  b.wait(2400);
  Millis on = b.changeAt(Channel::Out1, t, 1000);
  Millis off = b.changeAt(Channel::Out1, t, 0);
  TEST_ASSERT_TRUE_MESSAGE(on && on - t <= 120, "TRIG1 starts the show on time");
  TEST_ASSERT_TRUE_MESSAGE(off && off - on == 2000, "OUT1 on for exactly 2000 ms");

  // 2. stop all, from another client (the serial port, a second phone), during a run
  t = b.now();
  std::string r = b.call(R"({"op":"run","show":1,"id":1})");
  TEST_ASSERT_TRUE_MESSAGE(r.find("\"ok\":true") != std::string::npos, r.c_str());
  b.wait(400);
  TEST_ASSERT_TRUE(b.changeAt(Channel::Out1, t, 1000) != 0);
  Millis stop_at = b.now();
  r = b.call(R"({"op":"stop_all","id":1})");
  TEST_ASSERT_TRUE_MESSAGE(r.find("\"ok\":true") != std::string::npos, r.c_str());
  b.wait(50);
  Millis stopped = b.changeAt(Channel::Out1, stop_at, 0);
  TEST_ASSERT_TRUE_MESSAGE(stopped && stopped - stop_at <= Sim::TICK_MS, "stop_all within a step");

  // 3. a show script's effect: TRIG2 -> OUT2 on for exactly 1 s
  std::string set = R"({"op":"set_script","id":2,"text":"effect lamp:\nwait for trig2\nout2 = on\nsleep 1\nout2 = off\n","sounds":{}})";
  r = b.call(set);
  TEST_ASSERT_TRUE_MESSAGE(r.find("\"ok\":true") != std::string::npos, r.c_str());
  b.wait(300);
  t = b.now();
  b.input(Input::Trig2, true);
  b.wait(150);
  b.input(Input::Trig2, false);
  b.wait(1300);
  on = b.changeAt(Channel::Out2, t, 1000);
  off = b.changeAt(Channel::Out2, t, 0);
  TEST_ASSERT_TRUE_MESSAGE(on && on - t <= 120, "the script effect starts on time");
  TEST_ASSERT_TRUE_MESSAGE(off && off - on == 1000, "OUT2 on for exactly 1 s");

  // the network was stalled the whole time
  TEST_ASSERT_TRUE(Clock::now() - b.t0 < std::chrono::milliseconds(STALL_MS));
  printf("step: largest gap %ld ms, at most %ld ms behind wall time, with the network stalled\n", (long)b.max_gap_ms, (long)b.behind_ms);
  TEST_ASSERT_TRUE_MESSAGE(b.max_gap_ms < 40, "no step waits on the network");
  TEST_ASSERT_TRUE_MESSAGE(b.behind_ms < 40, "the step keeps wall time");
  net.join();
  mqtt.join();
  b.stop = true;
  b.step.join();
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(show_keeps_time_while_the_network_stalls);
  return UNITY_END();
}
