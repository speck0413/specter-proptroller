// A simulated Proptroller on this computer: the real controller code on virtual pins, served over HTTP /
// WebSocket with UDP discovery, so the phone UI and the desktop app work against it.
//
//   pio run -e sim && .pio/build/sim/program [--port 8080] [--discovery-port 47800] [--id sim1] [--name Name]
//                                            [--speed 1.0] [--fail-self-check] [--swap-ms 16000]
//                                            [--mqtt host[:port]]
//
// POST /api/v1/update takes simulator images (`tools/ptclient.py image OUT.bin --version 0.2.0`): the board restarts into that version and build, pending, and is
// valid 30 s later; --fail-self-check makes every new image fail its self-check and roll back. --swap-ms models
// the Pico W bootloader: a restart that installs (or reverts) an update keeps the board off the network for that
// long (simulated time) and info.update.install_max_s is reported.
//
// --mqtt connects to that broker (the settings mqtt_broker / mqtt_port, as set on the Settings page): the same
// MQTT session as the ESP32 (state, events, rpc, the will; tools/mqtt_check.py checks it with a broker of its own).
//
// Open http://localhost:8080/ : the Live tab's input chips press TRIG1 / TRIG2 / USER1-3.
//
//   .pio/build/sim/program --check [--dmx] [--sounds sounds.json] FILE...
// checks show scripts with the board's own checker (factory settings; --dmx: the DMX add-on on) and prints every message as
// `FILE:LINE:COL: error: message` (or warning), the form editors turn into clickable problems (VS Code's task
// "Check effect scripts"); exit status 1 when any file has an error, 2 when a file cannot be read.
#include <chrono>
#include <fstream>
#include <sstream>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include "proptroller/net/posix.h"
#include "proptroller/script.h"
#include "proptroller/sim/sim.h"

using namespace pt;

// A board support package on the include path (-I path/to/bsp, as a board build): the simulated board gets its pin
// map (proptroller_board.h) and starts fresh with its defaults (proptroller_defaults.h or proptroller_board.h:
// PROPTROLLER_BOARD_PROFILE / _SETTINGS / _SCRIPT, GOING-FURTHER.md "Make your own board").
#if defined(__has_include)
#if __has_include(<proptroller_board.h>)
#include <proptroller_board.h>
#define PT_SIM_MAP (&PROPTROLLER_BOARD_MAP)
#endif
#if __has_include(<proptroller_defaults.h>)
#include <proptroller_defaults.h>
#endif
#endif
#ifndef PT_SIM_MAP
#define PT_SIM_MAP nullptr
#endif
#if defined(PROPTROLLER_BOARD_PROFILE) || defined(PROPTROLLER_BOARD_SETTINGS) || defined(PROPTROLLER_BOARD_SCRIPT)
#define PT_SIM_DEFAULTS 1
#ifndef PROPTROLLER_BOARD_PROFILE
#define PROPTROLLER_BOARD_PROFILE nullptr
#endif
#ifndef PROPTROLLER_BOARD_SETTINGS
#define PROPTROLLER_BOARD_SETTINGS nullptr
#endif
#ifndef PROPTROLLER_BOARD_SCRIPT
#define PROPTROLLER_BOARD_SCRIPT nullptr
#endif
#endif

static bool readFile(const char* path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// --check: one line per message, `file:line:col: error|warning: message` (the line's first word is the column;
// line 0, the whole script, is reported on line 1); the message's own "line N: " is left out (the place says it)
static int checkFiles(int argc, char** argv) {
  SoundList sounds;
  ScriptBoard board;
  board.buttons_are_triggers = true;  // as every board says
  int i = 2, worst = 0;
  for (; i < argc; i++) {
    if (!strcmp(argv[i], "--dmx")) { board.dmx_on = true; continue; }  // dmx_port = s4
    if (strcmp(argv[i], "--sounds") || i + 1 >= argc) break;
    std::string json, why;
    if (!readFile(argv[i + 1], json) || !soundListParse(json, sounds, why)) {
      (void)fprintf(stderr, "%s:1:1: error: %s\n", argv[i + 1], why.empty() ? "cannot read the sound list" : why.c_str());
      return 2;
    }
    i++;
  }
  if (i >= argc) { (void)fputs("usage: program --check [--dmx] [--sounds sounds.json] FILE...\n", stderr); return 2; }
  auto prog = std::make_unique<ScriptProgram>();
  for (; i < argc; i++) {
    std::string raw;
    if (!readFile(argv[i], raw)) { (void)fprintf(stderr, "%s:1:1: error: cannot read this file\n", argv[i]); worst = 2; continue; }
    const std::string text = scriptClean(raw);
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    const ScriptCheck c = scriptCompile(text, sounds, board, *prog);
    auto say = [&](const ScriptMessage& m, const char* kind) {
      const unsigned line = m.line ? m.line : 1;
      unsigned col = 1;
      if (m.line && m.line <= lines.size()) {
        const size_t at = lines[m.line - 1].find_first_not_of(" \t");
        if (at != std::string::npos) col = (unsigned)at + 1;
      }
      std::string msg = m.text;
      const std::string own = "line " + std::to_string(m.line) + ": ";
      if (msg.compare(0, own.size(), own) == 0) msg.erase(0, own.size());
      (void)printf("%s:%u:%u: %s: %s\n", argv[i], line, col, kind, msg.c_str());
    };
    for (const auto& m : c.errors) say(m, "error");
    for (const auto& m : c.warnings) say(m, "warning");
    if (!c.ok() && worst == 0) worst = 1;
  }
  return worst;
}

int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "--check")) return checkFiles(argc, argv);
  int port = 8080, disc = DISCOVERY_PORT;
  std::string id = "sim0000c0de", name, mqtt;
  double speed = 1.0;
  bool fail_self_check = false;
  Millis swap_ms = 0;
  static const char* const USAGE =
      "usage: program [--port 8080] [--discovery-port 47800] [--id sim1] [--name Name] [--speed 1.0]\n"
      "               [--fail-self-check] [--swap-ms 16000] [--mqtt host[:port]]\n"
      "A simulated Proptroller: open http://localhost:<port>/ for its phone page.\n"
      "       program --check [--dmx] [--sounds sounds.json] FILE...   (show scripts: FILE:LINE:COL: error: message)\n";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { (void)fputs(USAGE, stdout); return 0; }
    if (!strcmp(argv[i], "--fail-self-check")) { fail_self_check = true; continue; }
    static const char* const WITH_VALUE[] = {"--port", "--discovery-port", "--id", "--name", "--speed", "--mqtt", "--swap-ms"};
    bool known = false;
    for (const char* o : WITH_VALUE) known = known || !strcmp(argv[i], o);
    if (!known || i + 1 >= argc) {
      (void)fprintf(stderr, known ? "%s needs a value\n%s" : "unknown option %s\n%s", argv[i], USAGE);
      return 2;
    }
    if (!strcmp(argv[i], "--port")) port = (int)strtol(argv[i + 1], nullptr, 10);
    else if (!strcmp(argv[i], "--discovery-port")) disc = (int)strtol(argv[i + 1], nullptr, 10);
    else if (!strcmp(argv[i], "--id")) id = argv[i + 1];
    else if (!strcmp(argv[i], "--name")) name = argv[i + 1];
    else if (!strcmp(argv[i], "--speed")) speed = strtod(argv[i + 1], nullptr);
    else if (!strcmp(argv[i], "--mqtt")) mqtt = argv[i + 1];
    else if (!strcmp(argv[i], "--swap-ms")) swap_ms = (Millis)strtoull(argv[i + 1], nullptr, 10);
    i++;
  }
  Sim sim(id, PROPTROLLER_FW_BUILD, PT_SIM_MAP);  // the real build (the tests' Sim reports "sim")
#if defined(PT_SIM_DEFAULTS)
  // a fresh board with the board support package's defaults
  static FactoryDefaults defaults;
  defaults.profile = PROPTROLLER_BOARD_PROFILE;
  defaults.settings = PROPTROLLER_BOARD_SETTINGS;
  defaults.script = PROPTROLLER_BOARD_SCRIPT;
  sim.setFactoryDefaults(&defaults);
  if (!sim.flash().wipe() || !sim.scriptFlash().wipe()) return 1;
  sim.powerCycle(0);
  if (!sim.factoryRefused.empty()) (void)fprintf(stderr, "board defaults refused:\n%s\n", sim.factoryRefused.c_str());
#endif
  sim.setFailSelfCheck(fail_self_check);
  sim.setSwapMs(swap_ms);
  if (!name.empty()) {
    JsonDocument d;
    d["op"] = "set_params";
    d["values"][0]["key"] = "name";
    d["values"][0]["value"] = name;
    std::string s;
    serializeJson(d, s);
    sim.request(s);
  }
  if (!mqtt.empty()) {
    JsonDocument d;
    d["op"] = "set_params";
    size_t colon = mqtt.rfind(':');
    d["values"][0]["key"] = "mqtt_broker";
    d["values"][0]["value"] = mqtt.substr(0, colon);
    if (colon != std::string::npos) {
      d["values"][1]["key"] = "mqtt_port";
      d["values"][1]["value"] = strtol(mqtt.c_str() + colon + 1, nullptr, 10);
    }
    std::string s;
    serializeJson(d, s);
    Response r = sim.request(s);
    if (!r.isOk()) { (void)fprintf(stderr, "--mqtt: %s\n", r.errorMessage().c_str()); return 1; }
  }
  std::unique_ptr<PosixNet> net(new PosixNet(sim));
  if (!net->start((uint16_t)port, (uint16_t)disc)) {
    (void)fprintf(stderr, "cannot open port %d or discovery port %d\n", port, disc);
    return 1;
  }
  const uint16_t bound = net->httpPort();
  printf("simulated Proptroller %s on http://localhost:%u/ (discovery %s)\n", id.c_str(), bound, disc ? "on" : "off");
  (void)fflush(stdout);
  auto t0 = std::chrono::steady_clock::now();
  Millis ticked = 0;
  uint32_t boots = sim.boots;
  for (;;) {
    Millis wall = (Millis)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    Millis sim_ms = (Millis)((double)wall * speed);
    while (ticked + Sim::TICK_MS <= sim_ms) {
      sim.tick();
      ticked += Sim::TICK_MS;
      if (sim.boots != boots) {
        boots = sim.boots;
        if (swap_ms && sim.slots().swapped) {
          // the bootloader swaps: the board is off the network (every connection dropped) for swap_ms
          net.reset();
          printf("installing the update: off for %llu ms\n", (unsigned long long)swap_ms);
          (void)fflush(stdout);
          std::this_thread::sleep_for(std::chrono::milliseconds((long long)((double)swap_ms / speed)));
          net.reset(new PosixNet(sim));
          if (!net->start(bound, (uint16_t)disc)) {
            (void)fprintf(stderr, "cannot open port %u again\n", bound);
            return 1;
          }
          // the board's own time stood still while it was off
          t0 += std::chrono::milliseconds((long long)((double)swap_ms / speed));
          break;
        }
      }
    }
    net->poll(wall);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
