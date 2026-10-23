#include "runs.h"
#include <ArduinoJson.h>

namespace pt {

bool RunCounter::parse(const std::string& rec, uint64_t& out) {
  JsonDocument d;
  if (deserializeJson(d, rec) || !d["lifetime"].is<uint64_t>()) return false;
  out = d["lifetime"].as<uint64_t>();
  return true;
}

}  // namespace pt
