#if !defined(ARDUINO) || defined(PROPTROLLER_SIM)
#include "sim.h"
#include "../flash_map.h"
#include "../password.h"
#include <algorithm>
#include <string.h>
#include <stdio.h>

namespace pt {

// ---------------------------------------------------------------- over-the-air updates

namespace sim_image {
std::string build(const std::string& fw_version, const std::string& fw_build, bool passes, size_t payload_len) {
  JsonDocument h;
  h["mcu"] = "sim";
  h["fw_version"] = fw_version;
  h["fw_build"] = fw_build;
  h["self_check"] = passes ? "pass" : "fail";
  std::string head;
  serializeJson(h, head);
  std::string v = MAGIC;
  v += head;
  v += '\n';
  v.reserve(v.size() + payload_len);
  for (size_t i = 0; i < payload_len; i++) v += (char)(uint8_t)(i % 251);
  return v;
}

bool parse(const std::string& bytes, std::string& fw_version, std::string& fw_build, bool& passes) {
  size_t m = strlen(MAGIC);
  if (bytes.compare(0, m, MAGIC) != 0) return false;
  size_t e = bytes.find('\n', m);
  if (e == std::string::npos) return false;
  JsonDocument h;
  if (deserializeJson(h, bytes.data() + m, e - m) || h["mcu"] != "sim") return false;
  if (!h["fw_version"].is<const char*>() || !h["fw_build"].is<const char*>()) return false;
  fw_version = h["fw_version"].as<std::string>();
  fw_build = h["fw_build"].as<std::string>();
  passes = (h["self_check"] | "pass") != std::string("fail");
  return true;
}
}  // namespace sim_image

void SimSlots::boot() {
  receiving = false;
  std::string().swap(image);
  swapped = boot_next >= 0 || (state == BootState::Pending && slots[1 - running].present);
  if (boot_next >= 0) {
    running = boot_next;
    boot_next = -1;
    state = BootState::Pending;
    rolled_back = false;
  } else if (state == BootState::Pending) {
    int other = 1 - running;
    if (slots[other].present) {
      running = other;
      rolled_back = true;
    }
    state = BootState::Valid;
  }
}

uint32_t SimTarget::maxBytes() const { return ESP32_APP_SLOT_BYTES; }  // the ESP32's app slot
size_t SimTarget::headerLen() const { return strlen(sim_image::MAGIC); }
bool SimTarget::headerOk(const uint8_t* first, size_t n) const {
  size_t m = strlen(sim_image::MAGIC);
  return n >= m && memcmp(first, sim_image::MAGIC, m) == 0;
}

UpdateError SimTarget::begin(uint32_t len) {
  s_.receiving = true;
  s_.image.clear();
  s_.image.reserve(len);
  s_.boot_next = -1;  // the other slot is about to be overwritten: it no longer boots next
  return UpdateError();
}

UpdateError SimTarget::write(const uint8_t* data, size_t n) {
  if (!s_.receiving) return UpdateError::internal("no update is being received");
  s_.image.append((const char*)data, n);
  return UpdateError();
}

UpdateError SimTarget::finish(const uint8_t sha256[32]) {
  std::string bytes;
  bytes.swap(s_.image);
  s_.receiving = false;
  // what was "written" is read back and checked, as a board does
  uint8_t d[32];
  pt::sha256((const uint8_t*)bytes.data(), bytes.size(), d);
  if (memcmp(d, sha256, 32) != 0) return UpdateError::internal("the written image does not read back right");
  SimFirmware fw;
  if (!sim_image::parse(bytes, fw.fw_version, fw.fw_build, fw.self_check_passes)) return UpdateError::invalid(updateWrongBoardText("sim"));
  fw.present = true;
  int other = 1 - s_.running;
  s_.slots[other] = fw;
  s_.boot_next = other;
  return UpdateError();
}

UpdateError SimTarget::markValid() {
  const SimFirmware& f = s_.slots[s_.running];
  if (!(f.present && f.self_check_passes) || s_.fail_self_check) return UpdateError::internal("the simulated self-check failed");
  s_.state = BootState::Valid;
  return UpdateError();
}

// ---------------------------------------------------------------- the board

Sim::Sim(const std::string& id, const std::string& fw_build, const PinMap* map, const char* board_id)
    : pins_(world_), uart_(world_), pixels_(world_) {
  if (map) {
    image_board_id_ = board_id ? board_id : "custom";
    image_board_ = {image_board_id_.c_str(), "Board support package (built in)", *map, true};
    has_image_board_ = true;
    chip_.mcu = map->mcu;
  }
  identity_.id = id;
  identity_.mcu = "sim";
  identity_.extra_caps = {"sim"};
  slots_.slots[0].present = true;
  slots_.slots[0].fw_version = identity_.fw_version;
  slots_.slots[0].fw_build = fw_build;
  boot();
}

void Sim::boot() {
  powered_at_ = now_;
  world_.now = 0;
  DfBehaviour b = world_.df.behaviour;
  auto card = world_.df.card;
  world_.df = DfModel();
  world_.df.behaviour = b;
  world_.df.card = card;
  world_.uart_rx.clear();
  // the bootloader picks the image, which says who the board is
  slots_.boot();
  identity_.fw_version = slots_.slots[slots_.running].fw_version;
  identity_.fw_build = slots_.slots[slots_.running].fw_build;
  // the board package first, before any pin is used (as Proptroller::start on the boards)
  const std::vector<BuiltinBoard> builtins = builtinBoards(chip_, has_image_board_ ? &image_board_ : nullptr);
  const BoardBoot bb = boardAtBoot(&board_flash_, chip_, builtins);
  const bool first = boots == 0;
  bool polarity_changed = false;
  for (int k = 0; k < INPUT_COUNT; k++) polarity_changed = polarity_changed || first || pins_.activeHigh(k) != inputActiveHigh(bb.cfg.map, k);
  pins_.setMap(bb.cfg.map);
  if (polarity_changed)
    for (int k = 0; k < INPUT_COUNT; k++) world_.input_high[k] = !pins_.activeHigh(k);  // idle
  identity_.board = bb.cfg.id;
  ctl_.reset(new Controller(pins_, uart_, pixels_, store_, identity_));
  ctl_->setUpdateTarget(&target_);
  ctl_->setScriptStore(script_store_);
  ctl_->setRunStore(&runs_flash_);
  ctl_->setDmx(&dmx_);
  ctl_->setBoardPackages(&board_flash_, bb, chip_, builtins);
  factoryRefused = ctl_->applyBoardDefaults(defaults_);
  NetState n;
  n.mode = "sim";
  n.ip = "127.0.0.1";
  ctl_->setNet(n);
  ctl_->wifiScanReady(false);
  scan_due_ = 0;
  boots++;
  record();
}

void Sim::powerCycle(Millis off_ms) {
  if (ctl_) {
    ctl_->saveIfDirty();
    ctl_->board().allOff();
  }
  ctl_.reset();
  for (auto& o : world_.out) o = 0;
  for (auto& d : world_.digital) d = false;
  world_.dmx_on = false;
  record();
  now_ += off_ms;
  boot();
}

void Sim::tick1() {
  // one millisecond: a trigger sample, and the controller's step on every 10th
  if ((now_ + 1 - powered_at_) % TICK_MS == 0) {
    tickAt(now_ + 1);
    return;
  }
  now_++;
  applyScript(now_);
  world_.now = now_ - powered_at_;
  ctl_->sampleTriggers(now_ - powered_at_);
}

void Sim::tick() {
  // the triggers are sampled every 1 ms, as on the board (scripted changes land on their ms)
  Millis next = now_ + TICK_MS - (now_ - powered_at_) % TICK_MS;
  for (Millis t = now_ + 1; t < next; t++) {
    applyScript(t);
    if (t > powered_at_) {
      world_.now = t - powered_at_;
      ctl_->sampleTriggers(t - powered_at_);
    }
  }
  tickAt(next);
}

// What the simulated board "sees" (op wifi_scan): fixed, so tests and screenshots are repeatable. Two access
// points share a name and one name repeats weaker; the list the page gets has each name once, strongest first.
std::vector<WifiNetwork> simNetworks() {
  return {{"Office", -48, true}, {"Guest Wi-Fi", -71, false}, {"Office", -63, true},
          {"Workshop 2.4", -55, true}, {"", -40, true}, {"Neighbour", -86, true}};
}

void Sim::tickAt(Millis at) {
  now_ = at;
  applyScript(now_);
  for (int i = 0; i < INPUT_COUNT; i++)
    if (lease_[i] && now_ >= lease_until_[i]) { lease_[i] = false; world_.input_high[i] = true; }
  Millis up = now_ - powered_at_;
  world_.now = up;
  auto bytes = world_.df.tick(up);
  world_.uart_rx.insert(world_.uart_rx.end(), bytes.begin(), bytes.end());
  ctl_->tick(up);
  ctl_->pixelService(up);  // on a board outside the step (a core-0 task, loop()); here right after it, in step
  ctl_->background();  // the board's loop does this between steps
  // the Wi-Fi scan, as the boards' network loop does it: started when the controller wants one, the fixed
  // sample list SIM_SCAN_MS later
  if (scan_due_ == 0 && ctl_->wifiScanWanted()) scan_due_ = up + SIM_SCAN_MS;
  if (scan_due_ != 0 && up >= scan_due_) {
    scan_due_ = 0;
    ctl_->wifiScanDone(simNetworks(), true);
  }
  record();
  if (ctl_->rebootRequested()) powerCycle(100);
}

void Sim::script(Input i, const std::vector<std::pair<Millis, bool>>& pattern) {
  for (auto& p : pattern) script_.push_back({now_ + p.first, i, p.second});
  std::stable_sort(script_.begin(), script_.end(), [](const Scripted& a, const Scripted& b) { return a.at < b.at; });
}

void Sim::applyScript(Millis t) {
  while (!script_.empty() && script_.front().at <= t) {
    setInput(script_.front().input, script_.front().active);
    script_.erase(script_.begin());
  }
}

void Sim::step(Millis ms) {
  for (Millis k = 0; k < (ms + TICK_MS - 1) / TICK_MS; k++) tick();
}

void Sim::record() {
  Level l[CHANNEL_COUNT];
  world_.levels(l);
  for (int i = 0; i < CHANNEL_COUNT; i++)
    if (l[i] != last_[i]) { trace.push_back({now_, ALL_CHANNELS[i], l[i]}); last_[i] = l[i]; }
}

Level Sim::level(Channel c) const {
  Level l[CHANNEL_COUNT];
  world_.levels(l);
  return l[index(c)];
}

Response Sim::handle(Request& r) {
  if (r.op == "sim_input") {
    Input i = Input::Trig1;
    (void)reqInput(r, "input", i);
    bool active = r["active"].as<bool>();
    setInput(i, active);
    lease_[index(i)] = active && !r["lease_ms"].isNull();
    if (lease_[index(i)]) lease_until_[index(i)] = now_ + r["lease_ms"].as<uint32_t>();
    return Response::done(r);
  }
  return ctl_->handle(r);
}

bool Sim::status(const std::string& key, Value& out) const {
  JsonDocument d;
  ctl_->stateToJson(d.to<JsonObject>());
  for (JsonVariant x : d["status"].as<JsonArray>())
    if (x["key"] == key) return Value::fromJson(x["value"], out);
  return false;
}

Response Sim::requestFrom(uint32_t session, const std::string& json) {
  ctl_->setOrigin(Controller::Origin::Session, session);
  Response r = request(json);
  ctl_->setOrigin(Controller::Origin::Http);
  return r;
}

std::string Sim::callFrom(uint32_t session, const char* body, size_t len) {
  ctl_->setOrigin(Controller::Origin::Session, session);
  std::string out = call(body, len);
  ctl_->setOrigin(Controller::Origin::Http);
  return out;
}

Response Sim::request(const std::string& json_in) {
  Request r;
  Response* err = nullptr;
  std::string json, why;
  if (!Controller::aliasIn(ctl_->settings().profile, json_in.data(), json_in.size(), json, why)) return errorFor(json_in.data(), json_in.size(), ErrorCode::BadRequest, why);
  if (!parseRequest(json.data(), json.size(), r, err, ctl_->hasRequestHook())) {
    Response e = *err;
    delete err;
    return e;
  }
  return handle(r);
}

std::string Sim::call(const char* body_in, size_t len_in) {
  Request r;
  Response* err = nullptr;
  std::string generic, why;
  if (!Controller::aliasIn(ctl_->settings().profile, body_in, len_in, generic, why)) return errorFor(body_in, len_in, ErrorCode::BadRequest, why).json();
  const char* body = generic.data();
  size_t len = generic.size();
  if (!parseRequest(body, len, r, err, ctl_->hasRequestHook())) {
    std::string s = err->json();
    delete err;
    return s;
  }
  Response resp = handle(r);
  // a state answered through the simulator shows its momentary holds
  if (r.op == "state") {
    JsonArray ins = resp.result()["inputs"];
    for (int i = 0; i < INPUT_COUNT; i++) ins[i]["held"] = lease_[i];
  }
  return resp.json();
}

std::string Sim::stateJson() {
  JsonDocument d;
  ctl_->stateToJson(d.to<JsonObject>());
  JsonArray ins = d["inputs"];
  for (int i = 0; i < INPUT_COUNT; i++) ins[i]["held"] = lease_[i];
  std::string s;
  serializeJson(d, s);
  return s;
}

// ---------------------------------------------------------------- scenarios

bool runScenario(const std::string& json, ScenarioReport& rep, std::string& err) {
  JsonDocument sc;
  if (deserializeJson(sc, json)) { err = "bad JSON"; return false; }
  rep.name = sc["name"].as<std::string>();
  Sim sim("scenario");
  if (sc["settings"].is<JsonArray>() && sc["settings"].size() > 0) {
    JsonDocument req;
    req["op"] = "set_params";
    req["values"] = sc["settings"];
    std::string s;
    serializeJson(req, s);
    Response r = sim.request(s);
    if (!r.isOk()) rep.failures.push_back("settings rejected: " + r.errorMessage());
  }
  if (sc["shows"].is<JsonArray>() && sc["shows"].size() > 0) {
    sim.request(R"({"op":"delete_show","show":1})");
    for (JsonVariant show : sc["shows"].as<JsonArray>()) {
      JsonDocument req;
      req["op"] = "put_show";
      req["data"] = show;
      std::string s;
      serializeJson(req, s);
      Response r = sim.request(s);
      if (!r.isOk()) rep.failures.push_back("show " + show["id"].as<std::string>() + " rejected: " + r.errorMessage());
    }
  }
  sim.trace.clear();
  Millis start = sim.now();
  struct Step { Millis at; JsonVariant v; size_t order; };
  std::vector<Step> steps;
  size_t k = 0;
  for (JsonVariant s : sc["steps"].as<JsonArray>()) steps.push_back({s["at"].as<Millis>(), s, k++});
  std::stable_sort(steps.begin(), steps.end(), [](const Step& a, const Step& b) { return a.at < b.at; });
  std::vector<std::pair<Millis, Input>> releases;
  Millis until = sc["until"].as<Millis>();
  size_t i = 0;
  char b[160];
  for (;;) {
    Millis t = sim.now() - start;
    for (auto it = releases.begin(); it != releases.end();) {
      if (it->first <= t) { sim.setInput(it->second, false); it = releases.erase(it); }
      else ++it;
    }
    while (i < steps.size() && steps[i].at <= t) {
      JsonVariant st = steps[i].v;
      std::string d = st["do"].as<std::string>();
      Input in = Input::Trig1;
      if (d == "press") {
        if (!parseInput(st["input"] | "", in)) { err = "unknown input " + st["input"].as<std::string>(); return false; }
        sim.setInput(in, true);
        releases.push_back({t + st["ms"].as<Millis>(), in});
      } else if (d == "set") {
        if (!parseInput(st["input"] | "", in)) { err = "unknown input " + st["input"].as<std::string>(); return false; }
        sim.setInput(in, st["active"].as<bool>());
      } else if (d == "request") {
        std::string s;
        serializeJson(st["req"], s);
        Response r = sim.request(s);
        if (!st["ok"].isNull()) {
          rep.checks++;
          if (r.isOk() != st["ok"].as<bool>()) {
            (void)snprintf(b, sizeof b, "%llu ms: request %s ok=%d (wanted %d)", (unsigned long long)steps[i].at, s.c_str(), r.isOk(), st["ok"].as<bool>());
            rep.failures.push_back(b);
          }
        }
      } else if (d == "expect") {
        JsonObject levels = st["levels"];
        for (JsonPair p : levels) {
          rep.checks++;
          Channel c = Channel::Out1;
          if (!parseChannel(p.key().c_str(), c)) { rep.failures.push_back(std::string("unknown channel ") + p.key().c_str()); continue; }
          Level got = sim.level(c), want = p.value().as<Level>();
          if (got != want) {
            (void)snprintf(b, sizeof b, "%llu ms: %s = %u, expected %u", (unsigned long long)steps[i].at, p.key().c_str(), got, want);
            rep.failures.push_back(b);
          }
        }
      } else if (d == "expect_sound") {
        rep.checks++;
        bool got = sim.world().df.playing, want = st["playing"].as<bool>();
        if (got != want) {
          (void)snprintf(b, sizeof b, "%llu ms: sound playing = %d, expected %d", (unsigned long long)steps[i].at, got, want);
          rep.failures.push_back(b);
        }
      } else if (d == "power_cycle") {
        sim.powerCycle(st["off_ms"].isNull() ? 200 : st["off_ms"].as<Millis>());
      } else {
        err = "unknown step " + d;
        return false;
      }
      i++;
    }
    if (t >= until) break;
    sim.tick();
  }
  return true;
}

}  // namespace pt
#endif
