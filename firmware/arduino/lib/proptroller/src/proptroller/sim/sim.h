// One simulated board: virtual time, scripted inputs, the real Controller on virtual pins. Used by the host
// tests, the scenario runner and the `sim` environment's network server (the phone UI against a host
// build). Requests with op sim_input drive the virtual inputs.
#pragma once
#include "../device.h"
#include "hardware.h"
#include <memory>

namespace pt {

// Over-the-air updates in the simulator: two app slots in
// memory and a bootloader. An update writes the slot the board does not run from; the restart boots it
// pending; after its self-check (30 s) it is valid; a restart while pending boots the previous slot again
// with rolled_back. A simulator image is "PROPTROLLER-SIM-IMAGE\n", a JSON header line
// {"mcu":"sim","fw_version":…,"fw_build":…,"self_check":"pass"|"fail"} and any payload: the board reports that
// version and build after booting it (`proptroller-sim image` files work here too).
namespace sim_image {
static const char* const MAGIC = "PROPTROLLER-SIM-IMAGE\n";
std::string build(const std::string& fw_version, const std::string& fw_build, bool self_check_passes, size_t payload_len);
// false when `bytes` is not a simulator image
bool parse(const std::string& bytes, std::string& fw_version, std::string& fw_build, bool& self_check_passes);
}  // namespace sim_image

// The Wi-Fi networks the simulated board finds (op wifi_scan), and how long its scan takes.
std::vector<WifiNetwork> simNetworks();
static const Millis SIM_SCAN_MS = 1500;

struct SimFirmware {
  bool present = false;
  std::string fw_version, fw_build;
  bool self_check_passes = true;
};

struct SimSlots {
  int running = 0;
  SimFirmware slots[2];
  BootState state = BootState::Valid;
  bool rolled_back = false;
  int boot_next = -1;      // set by finish: the slot to boot at the next restart
  bool receiving = false;
  std::string image;       // the image being received
  bool fail_self_check = false;  // every self-check fails (simulator option)
  Millis swap_ms = 0;            // a bootloader that swaps the image in at the restart (simulator option)
  bool swapped = false;          // the last boot swapped (an update or a revert)
  void boot();
};

class SimTarget : public UpdateTarget {
 public:
  explicit SimTarget(SimSlots& s) : s_(s) {}
  std::string slot() const override { return s_.running ? "app1" : "app0"; }
  uint32_t maxBytes() const override;
  uint32_t installMaxS() const override { return s_.swap_ms ? SWAP_INSTALL_MAX_S : 0; }
  static const uint32_t SWAP_INSTALL_MAX_S = 180;  // what a Pico W reports (arduino/ota.cpp)
  BootState state() const override { return s_.state; }
  bool rolledBack() const override { return s_.rolled_back; }
  size_t headerLen() const override;
  bool headerOk(const uint8_t* first, size_t n) const override;
  UpdateError begin(uint32_t len) override;
  UpdateError write(const uint8_t* data, size_t n) override;
  UpdateError finish(const uint8_t sha256[32]) override;
  void abort() override { s_.receiving = false; std::string().swap(s_.image); }
  UpdateError markValid() override;

 private:
  SimSlots& s_;
};

struct Change {
  Millis t;
  Channel channel;
  Level level;
};

class Sim : public NetApp {
 public:
  static const Millis TICK_MS = 10;
  // fw_build: what info.fw_build reports (fixed "sim" by default, so the golden files do not change with
  // every commit; `pio run -e sim` gives the real build)
  // `map`: the image's own board (a board support package compiled in: absent terminals, input polarity), named
  // `board_id`; none = the Proptroller board. A board package stored with set_board replaces it from the next
  // power-on, as on the boards (BOARD-PACKAGES.md).
  explicit Sim(const std::string& id = "sim", const std::string& fw_build = "sim", const PinMap* map = nullptr,
               const char* board_id = nullptr);
  Controller& ctl() { return *ctl_; }
  SimWorld& world() { return world_; }
  MemStorage& flash() { return flash_; }
  MemStorage& scriptFlash() { return script_flash_; }
  MemStorage& runsFlash() { return runs_flash_; }  // the lifetime run count (runs.h)
  MemStorage& boardFlash() { return board_flash_; }  // the board package (board_package.h)
  // the chip the simulated board has (default: an ESP32 DevKit), from the next power-on
  void setChip(const ChipRules& c) { chip_ = c; }
  const BoardBoot& boardBoot() const { return ctl_->boardRunning(); }
  const SimPins& pins() const { return pins_; }
  // Tests: run on other stores (ABStorage over a file layer, as the boards do) from the next power-on;
  // nullptr goes back to flash() / scriptFlash().
  void useStores(Storage* settings, Storage* script) {
    store_ = settings ? settings : &flash_;
    script_store_ = script ? script : &script_flash_;
  }
  // A board support package's defaults (device.h FactoryDefaults), applied at every power-on that finds nothing
  // stored, from the next one (or now: powerCycle()). Null: none. The texts must outlive the Sim.
  void setFactoryDefaults(const FactoryDefaults* d) { defaults_ = d; }
  std::string factoryRefused;  // what the last power-on's defaults refused
  Millis now() const { return now_; }
  Millis uptime() const { return now_ - powered_at_; }
  void tick();   // to the next 10 ms step (TRIG1 / TRIG2 sampled every 1 ms on the way)
  void tick1();  // one millisecond
  void step(Millis ms);
  void powerCycle(Millis off_ms = 200);
  Level level(Channel c) const;
  void setInput(Input i, bool active) { world_.input_high[index(i)] = pins_.activeHigh(index(i)) ? active : !active; }
  void press(Input i, Millis ms) { setInput(i, true); step(ms); setInput(i, false); }
  // Input changes at 1 ms resolution, pattern = (ms from now, active): {{5, true}, {6, false}} is a 1 ms
  // closure. They happen as time is stepped.
  void script(Input i, const std::vector<std::pair<Millis, bool>>& pattern);
  // Handle a request (sim ops here, everything else by the controller).
  Response handle(Request& r);
  Response request(const std::string& json);
  // add the example show (op add_example; a fresh board has none): TRIG1 pulses OUT1 for 2 s
  void addExample() { (void)request(R"({"op":"add_example"})"); }
  std::vector<Change> trace;
  uint32_t boots = 0;
  // the two app slots and the boot selection (survive restarts)
  SimSlots& slots() { return slots_; }
  void setFailSelfCheck(bool fail) { slots_.fail_self_check = fail; }
  // Model the Pico bootloader: a restart that installs an update (or reverts one) keeps the board off for
  // ms (main_sim takes the network down that long) and info.update.install_max_s is reported.
  void setSwapMs(Millis ms) { slots_.swap_ms = ms; }
  // NetApp: POST /api/v1/update reaches the controller
  bool updateBegin(const UpdateRequest& req, std::string& answer) override { return ctl_->updateBegin(req, answer); }
  bool updateWrite(const uint8_t* data, size_t n, std::string& answer) override { return ctl_->updateWrite(data, n, answer); }
  std::string updateFinish() override { return ctl_->updateFinish(); }
  void updateAbort() override { ctl_->updateAbort(); }

  // NetApp: the network server talks to the simulated board through this.
  std::string call(const char* body, size_t len) override;
  std::string callFrom(uint32_t session, const char* body, size_t len) override;
  void sessionClosed(uint32_t session) override { ctl_->sessionClosed(session); }
  std::string consoleLine(const std::string& line) override { return ctl_->consoleLine(line); }
  // A request on a WebSocket session (as the phone page sends it).
  Response requestFrom(uint32_t session, const std::string& json);
  // The serial console: one line in, the answer out.
  std::string serial(const std::string& line) { return ctl_->consoleLine(line); }
  // An effect status value (run_count, runs_left, ... or its alias); false when there is none.
  bool status(const std::string& key, Value& out) const;
  std::string infoJson() override { return ctl_->infoJson(); }
  std::string stateJson() override;
  uint32_t eventsSince(uint32_t seq, std::vector<std::string>& out) override { return ctl_->eventsSince(seq, out); }
  uint32_t eventsSeq() override { return ctl_->eventsSeq(); }
  std::string announceJson(uint16_t port) override { return ctl_->announceJson(port); }
  std::vector<std::pair<std::string, std::string>> txt() override { return ctl_->txt(); }
  void netChanged(const NetState& n) override { ctl_->netChanged(n); }
  NetConfig netConfig() override { return ctl_->netConfig(); }
  // MQTT: the controller's (the simulator talks to a real broker, net/posix.cpp)
  bool mqttTarget(std::string& host, uint16_t& port) override { return ctl_->mqttTarget(host, port); }
  uint32_t mqttEpoch() override { return ctl_->mqttEpoch(); }
  std::string mqttClientId() override { return ctl_->mqttClientId(); }
  MqttMsg mqttWill() override { return ctl_->mqttWill(); }
  std::vector<MqttMsg> mqttOnConnect() override { return ctl_->mqttOnConnect(); }
  std::vector<std::string> mqttSubscriptions() override { return ctl_->mqttSubscriptions(); }
  std::vector<MqttMsg> mqttOnMessage(const std::string& t, const std::string& p) override { return ctl_->mqttOnMessage(t, p); }
  std::vector<MqttMsg> mqttPoll() override { return ctl_->mqttPoll(); }
  void cueIn(const std::string& c) override { ctl_->cueIn(c); }

 private:
  void tickAt(Millis at);
  struct Scripted { Millis at; Input input; bool active; };
  std::vector<Scripted> script_;
  void applyScript(Millis t);
  void boot();
  void record();
  SimWorld world_;
  SimPins pins_;
  SimUart uart_;
  SimPixels pixels_;
  MemStorage flash_;
  MemStorage script_flash_{SCRIPT_STORE_CAPACITY};
  MemStorage runs_flash_{RUNS_STORE_CAPACITY};
  MemStorage board_flash_{BOARD_STORE_CAPACITY};
  ChipRules chip_;
  bool has_image_board_ = false;
  BuiltinBoard image_board_{};
  std::string image_board_id_;
  Storage* store_ = &flash_;
  Storage* script_store_ = &script_flash_;
  SimDmx dmx_{world_};
  Identity identity_;
  SimSlots slots_;
  SimTarget target_{slots_};
  std::unique_ptr<Controller> ctl_;
  const FactoryDefaults* defaults_ = nullptr;
  Millis now_ = 0, powered_at_ = 0;
  Millis scan_due_ = 0;  // a simulated Wi-Fi scan reports then (0 = none running)
  Level last_[CHANNEL_COUNT] = {0};
  bool lease_[INPUT_COUNT] = {false};
  Millis lease_until_[INPUT_COUNT] = {0};
};

// Scripted scenarios (JSON): settings, shows, timed inputs and requests, and
// expectations. Returns the number of checks; failures are appended to `failures`.
struct ScenarioReport {
  std::string name;
  std::vector<std::string> failures;
  size_t checks = 0;
  bool passed() const { return failures.empty(); }
};
bool runScenario(const std::string& json, ScenarioReport& out, std::string& err);

}  // namespace pt
