// On-hardware benchmark firmware: the generic firmware plus measurements, answered over the protocol
// (tools/hw_bench.py drives it over the network; the same ops work over USB serial as JSON lines).
//
// | op (POST /api/v1/rpc)      | measures                                                                 |
// |----------------------------|--------------------------------------------------------------------------|
// | {"op":"bench_jitter"}      | time between 10 ms steps since the last call: min / mean / max / >12 ms   |
// | {"op":"bench_boot"}        | microseconds from reset to: begin() start, outputs off, controller ready  |
// | {"op":"bench_latency","n":50} | starts n trigger-to-output trials: TRIG1 is pulled low through a jumper |
// |                            | from expansion S4 (ESP32 GPIO15 / Pico GP3), timed until the OUT1 gate     |
// |                            | pin reads high; {"op":"bench_latency_result"} returns them (microseconds) |
//
// Wiring for bench_latency: a 1k resistor from S4 to the TRIG1 tip contact (the board's 4.7k pull-up keeps
// it high otherwise); nothing on OUT1 (the gate pin is read back). Remove the jumper for normal use.
// S4 is also the show-script DMX port (dmx_port = s4): leave dmx_port off on the bench, and unplug a DMX
// module from S4 before fitting the jumper.
// For the true boot-to-safe time put a scope on an OUT gate and on EN / RUN: the firmware's own number
// starts at the CPU's first instruction after the ROM bootloader.

#include <ProptrollerAdvanced.h>
#include "proptroller/arduino/hal.h"

Proptroller prop;

#if defined(ESP32)
static const int S4 = 15;
static const int OUT1_GATE = 23;
#include <esp_timer.h>
static uint64_t usNow() { return esp_timer_get_time(); }
#else
static const int S4 = 3;
static const int OUT1_GATE = 15;
static uint64_t usNow() { return time_us_64(); }
#endif

static uint64_t t_begin = 0, t_ready = 0;
static uint64_t t_off = 0;

class Jitter : public PropTask {
 public:
  uint64_t last = 0, min_us = UINT64_MAX, max_us = 0, sum = 0, n = 0, late = 0;
  void update(PropAdvanced&, uint64_t) override {
    uint64_t now = usNow();
    if (last) {
      uint64_t d = now - last;
      if (d < min_us) min_us = d;
      if (d > max_us) max_us = d;
      sum += d;
      n++;
      if (d > 12000) late++;
    }
    last = now;
  }
  void reset() { min_us = UINT64_MAX; max_us = sum = n = late = 0; }
};
static Jitter jitter;
static volatile int pending_trials = 0;
static std::vector<uint32_t> results;

static bool benchOps(pt::Controller& c, pt::Request& r, pt::Response& out, void*) {
  if (r.op == "bench_jitter") {
    out = pt::Response::ok(r, "bench");
    JsonObject o = out.result();
    o["steps"] = jitter.n;
    o["min_us"] = jitter.n ? jitter.min_us : 0;
    o["mean_us"] = jitter.n ? jitter.sum / jitter.n : 0;
    o["max_us"] = jitter.max_us;
    o["late_over_12ms"] = jitter.late;
    jitter.reset();
    return true;
  }
  if (r.op == "bench_boot") {
    out = pt::Response::ok(r, "bench");
    out.result()["begin_us"] = t_begin;
    out.result()["outputs_off_us"] = t_off;
    out.result()["ready_us"] = t_ready;
    return true;
  }
  if (r.op == "bench_latency") {
    // runs in loop(), outside the controller lock, so the 10 ms step goes on while it waits
    pending_trials = r["n"] | 20;
    results.clear();
    out = pt::Response::ok(r, "bench");
    out.result()["started"] = pending_trials;
    return true;
  }
  if (r.op == "bench_latency_result") {
    out = pt::Response::ok(r, "bench");
    out.result()["done"] = pending_trials == 0;
    JsonArray a = out.result()["latency_us"].to<JsonArray>();
    for (auto v : results) a.add(v);
    return true;
  }
  return false;
}

void setup() {
  t_begin = usNow();
  prop.begin();
  t_off = t_begin;  // begin() drives the outputs off in its first statements (see Proptroller.cpp)
  prop.advanced().when(TRIG1).named("Latency probe").turnOn(OUT1, seconds(1));
  prop.advanced().addTask(jitter);
  prop.advanced().onRequest(benchOps);
  t_ready = usNow();
}

// One trigger-to-output trial: pull TRIG1 low through S4, time until the OUT1 gate pin goes high.
static void trial() {
  pinMode(S4, OUTPUT);
  digitalWrite(S4, LOW);
  uint64_t t0 = usNow(), t1 = 0;
  while (usNow() - t0 < 200000) {
    if (digitalRead(OUT1_GATE) == HIGH) { t1 = usNow(); break; }
  }
  pinMode(S4, INPUT);
  results.push_back(t1 ? (uint32_t)(t1 - t0) : 0);
}

void loop() {
  prop.update();
  static uint64_t next = 0;
  if (pending_trials > 0 && usNow() >= next) {
    trial();
    pending_trials = pending_trials - 1;
    next = usNow() + 1300000;  // the 1 s pulse plus the trigger's release time
  }
}
