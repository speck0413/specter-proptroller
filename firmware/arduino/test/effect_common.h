// Helpers shared by the pulse-and-recover host tests: a simulated board with the effect turned on.
#pragma once
#include <unity.h>
#include <string>
#include <vector>
#include "proptroller/sim/sim.h"

using namespace pt;

#ifndef PT_TEST_NO_OUT_NAMES  // a test that includes Proptroller.h has OUT1 ... already
static const Channel OUT1 = Channel::Out1;
static const Channel OUT2 = Channel::Out2;
static const Channel OUT3 = Channel::Out3;
static const Channel OUT4 = Channel::Out4;
#endif
static const Millis TICK = Sim::TICK_MS;  // outputs change on the 10 ms tick; TRIG1 is sampled every 1 ms

// A board with the effect on (the effect profile's 100 ms trigger debounce and release), named "fx".
class FxSim : public Sim {
 public:
  FxSim() : Sim("fx") {
    request(R"({"op":"delete_show","show":1})");  // the example show uses TRIG1 and OUT1, which the effect owns
    for (const char* l : {"effect=pulse_and_recover", "trig_debounce_ms=100", "trig_release_ms=100", "name=fx"}) {
      std::string out = serial(l);
      TEST_ASSERT_TRUE_MESSAGE(out.find("error") == std::string::npos, out.c_str());
    }
  }
  std::vector<std::pair<Millis, Level>> changes(Channel c, Millis from) const {
    std::vector<std::pair<Millis, Level>> v;
    for (auto& x : trace)
      if (x.channel == c && x.t >= from) v.push_back({x.t, x.level});
    return v;
  }
  using Sim::request;
  Response request(const std::string& json, uint32_t session) { return session ? requestFrom(session, json) : request(json); }
};
typedef FxSim EffectSim;
static const Millis TAP = 150;  // a press long enough for the 100 ms debounce

inline const EffectMachine& fx(Sim& s) { return *s.ctl().effect(); }

// Changes of a channel at or after `from`.
inline std::vector<std::pair<Millis, Level>> changesOf(Sim& s, Channel c, Millis from) {
  std::vector<std::pair<Millis, Level>> v;
  for (auto& x : s.trace)
    if (x.channel == c && x.t >= from) v.push_back({x.t, x.level});
  return v;
}
// First time `c` matches at or after `from`; 0 when never.
template <typename P>
inline Millis when(Sim& s, Channel c, Millis from, P pred) {
  for (auto& x : changesOf(s, c, from))
    if (pred(x.second)) return x.first;
  return 0;
}
inline bool on(Level l) { return l > 0; }
inline bool off(Level l) { return l == 0; }
inline size_t runs(Sim& s, Millis from) {
  size_t n = 0;
  for (auto& x : changesOf(s, Channel::Out1, from)) n += x.second > 0;
  return n;
}
inline bool statusBool(Sim& s, const char* k) {
  Value v;
  return s.status(k, v) && v.type == Value::Type::Bool && v.b;
}
inline int64_t statusInt(Sim& s, const char* k) {
  Value v;
  return s.status(k, v) ? v.i : -1;
}
inline bool armed(Sim& s) { return statusBool(s, "ready"); }
inline Response setp(Sim& s, const std::string& key, const std::string& value_json, uint32_t session = 0) {
  std::string j = R"({"op":"set_params","values":[{"key":")" + key + R"(","value":)" + value_json + "}]}";
  return session ? s.requestFrom(session, j) : s.request(j);
}
inline Response act(Sim& s, const char* key, bool down, uint32_t session = 0) {
  std::string j = std::string(R"({"op":"action","key":")") + key + R"(","down":)" + (down ? "true" : "false") + "}";
  return session ? s.requestFrom(session, j) : s.request(j);
}
inline std::string code(Response& r) { return r.doc()["error"]["code"] | ""; }
