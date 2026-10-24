// Board packages (firmware/BOARD-PACKAGES.md): the pin map as data on one generic image. The package's checks (every
// refusal in plain words, the pin rules per chip, every built-in board passes), its record, and the board's
// behaviour: the built-in board with nothing stored, no pins at all with a stored package that cannot be used,
// set_board stored but applied only by a restart, polarity, a factory reset that keeps it, its defaults, and the
// 1 ms sampling at the same cost.
#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <map>
#include <sstream>
#include "proptroller/board_package.h"
#include "proptroller/storage_ab.h"
#include "proptroller/lz.h"
#include "proptroller/sim/sim.h"

using namespace pt;

void setUp() {}
void tearDown() {}

static const ChipRules ESP32{Mcu::Esp32DevKit, false, false};
static const ChipRules ESP32_PSRAM{Mcu::Esp32DevKit, true, false};
static const ChipRules PICO_PLAIN{Mcu::Pico, false, false};
static const ChipRules PICO_W{Mcu::Pico, false, true};

// four relays on OUT1-4, a trigger with the board's pull-up and a button with a pull-down (active high)
static const char* const RELAY_BOARD = R"({"board_package":1,"id":"relay-board","name":"Relay board","mcu":"esp32-devkit",
  "terminals":{"OUT1":{"pin":23},"OUT2":{"pin":22},"OUT3":{"pin":21},"OUT4":{"pin":19},
               "TRIG1":{"pin":36,"active":"low","pull":"board"},"TRIG2":{"pin":18,"active":"high","pull":"board"}},
  "status_led":2})";

static std::string check(const std::string& json, const ChipRules& chip = ESP32, std::vector<std::string>* warnings = nullptr) {
  BoardConfig c;
  std::vector<std::string> w;
  std::string why;
  bool ok = checkBoardPackageText(json, chip, builtinBoards(chip), c, w, why);
  if (warnings) *warnings = w;
  return ok ? "" : why;
}

static void refused(const char* json, const char* words, const ChipRules& chip = ESP32) {
  std::string why = check(json, chip);
  TEST_ASSERT_FALSE_MESSAGE(why.empty(), json);
  TEST_ASSERT_TRUE_MESSAGE(why.find(words) != std::string::npos, (std::string("got: ") + why + "\nwant: " + words).c_str());
}

// ---------------------------------------------------------------- the package

void every_built_in_board_passes_the_pin_rules() {
  for (const ChipRules& chip : {ESP32, ESP32_PSRAM, PICO_PLAIN, PICO_W}) {
    if (chip.psram) continue;  // the Proptroller board's ESP32 has no PSRAM (GPIO16 / 17 are SCL and sound TX)
    for (auto& b : builtinBoards(chip)) {
      std::vector<std::string> w;
      std::string why;
      TEST_ASSERT_TRUE_MESSAGE(checkPinMap(b.map, chip, w, why), (std::string(b.id) + ": " + why).c_str());
      // and as a package: get_board's export reads back to the same map
      JsonDocument d;
      builtinToJson(b, chip, d.to<JsonObject>());
      BoardConfig c;
      TEST_ASSERT_TRUE_MESSAGE(checkBoardPackage(d.as<JsonVariantConst>(), chip, builtinBoards(chip), c, w, why), why.c_str());
      TEST_ASSERT_EQUAL_MEMORY(&b.map, &c.map, sizeof(PinMap));
      if (b.map.pixel != NO_PIN) TEST_ASSERT_EQUAL(b.pixel_inverted, c.pixel_inverted);
    }
  }
  TEST_ASSERT_EQUAL_STRING("proptroller", builtinBoards(ESP32)[0].id);
  TEST_ASSERT_NOT_NULL(findBuiltin(builtinBoards(ESP32), "relay4-button"));
  TEST_ASSERT_EQUAL(STATUS_LED_BUILTIN, builtinBoards(PICO_W)[0].map.status_led);  // the Pico W's LED is on the radio
  TEST_ASSERT_EQUAL(25, builtinBoards(PICO_PLAIN)[0].map.status_led);
  // the relay board, and one with only an active-high button and four outputs
  TEST_ASSERT_EQUAL_STRING("", check(RELAY_BOARD).c_str());
}

void a_compiled_in_board_becomes_the_default() {
  BuiltinBoard mine = {"relay-board", "Relay board", builtinBoards(ESP32)[1].map, false};
  auto all = builtinBoards(ESP32, &mine);
  TEST_ASSERT_EQUAL_STRING("relay-board", all[0].id);
  TEST_ASSERT_EQUAL(3, (int)all.size());
  BuiltinBoard same_id = {"proptroller", "My Proptroller", ESP32_DEVKIT, true};
  all = builtinBoards(ESP32, &same_id);
  TEST_ASSERT_EQUAL(2, (int)all.size());
  TEST_ASSERT_EQUAL_STRING("My Proptroller", all[0].name);
  // a board for another chip is not offered
  TEST_ASSERT_EQUAL_STRING("proptroller", builtinBoards(PICO_W, &mine)[0].id);
}

void a_bad_package_is_refused_in_plain_words() {
  refused("[1,2]", "a board package is a JSON object");
  refused("{nope", "not valid JSON");
  refused(R"({"id":"x","name":"X","mcu":"esp32-devkit"})", "a board package starts with \"board_package\": 1");
  refused(R"({"board_package":2,"id":"x","name":"X","mcu":"esp32-devkit"})", "format 2; this firmware reads format 1: update the firmware");
  refused(R"({"board_package":1,"id":"My Board","name":"X","mcu":"esp32-devkit"})", "\"id\" must be 1 to 32 characters of a-z");
  refused(R"({"board_package":1,"id":"none","name":"X","mcu":"esp32-devkit"})", "(not \"none\")");
  refused(R"({"board_package":1,"id":"x","mcu":"esp32-devkit"})", "\"name\" must be 1 to 60 characters");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"pico"})", "this board package is for the pico socket; this board has the esp32-devkit");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32"})", "\"mcu\" must be \"esp32-devkit\" or \"pico\"");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","pins":{}})", "unknown field \"pins\"");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"OUT5":{"pin":4}}})", "there is no terminal \"OUT5\" (OUT1-4");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"OUT1":23}})", "OUT1 must be an object");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"OUT1":{"pin":"23"}}})", "OUT1: \"pin\" must be a GPIO number");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"OUT1":{}}})", "OUT1: \"pin\" is missing");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"OUT1":{"pin":23,"active":"high"}}})", "OUT1: unknown field \"active\"");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"TRIG1":{"pin":4,"active":"on"}}})", "TRIG1: \"active\" must be \"low\"");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"TRIG1":{"pin":4,"pull":"up"}}})", "TRIG1: \"pull\" must be \"chip\"");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","pixel":{"pin":33,"inverted":"yes"}})", "pixel: \"inverted\" must be true or false");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","sound":{"tx":17}})", "sound: the sound port needs both \"tx\" and \"rx\"");
  refused(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","status_led":"blue"})", "\"status_led\" must be a GPIO number or \"builtin\"");
  // a package that is too big
  std::string big = R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","description":")" + std::string(BOARD_PACKAGE_MAX_BYTES, 'a') + "\"}";
  refused(big.c_str(), "at most 16 KB");
}

void the_pin_rules_name_the_terminal_and_the_reason() {
  auto one = [](const char* terminal_json) {
    return std::string(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit",)") + terminal_json + "}";
  };
  refused(one(R"("terminals":{"OUT1":{"pin":7}})").c_str(), "OUT1: GPIO 7 is wired to the module's flash memory");
  refused(one(R"("terminals":{"OUT1":{"pin":1}})").c_str(), "OUT1: GPIO 1 is the USB serial port");
  refused(one(R"("terminals":{"OUT1":{"pin":24}})").c_str(), "OUT1: the ESP32 has no GPIO 24");
  refused(one(R"("terminals":{"OUT1":{"pin":40}})").c_str(), "OUT1: the ESP32 has no GPIO 40");
  refused(one(R"("terminals":{"OUT3":{"pin":35}})").c_str(), "OUT3: GPIO 35 is input-only on the ESP32 and cannot drive an output");
  refused(one(R"("pixel":{"pin":39})").c_str(), "pixel: GPIO 39 is input-only");
  refused(one(R"("terminals":{"TRIG1":{"pin":36}})").c_str(), "TRIG1: GPIO 36 has no pull resistor of its own on the ESP32: set \"pull\": \"board\"");
  refused(one(R"("terminals":{"RLY1":{"pin":12}})").c_str(), "RLY1: GPIO 12 sets the flash voltage at power-on");
  refused(one(R"("analog":25)").c_str(), "analog: GPIO 25 cannot read analog while Wi-Fi runs: use GPIO 32-39");
  refused(one(R"("terminals":{"OUT1":{"pin":23},"RLY1":{"pin":23}})").c_str(), "GPIO 23 is used twice: OUT1 and RLY1");
  refused(one(R"("terminals":{"OUT1":{"pin":16}})").c_str(), "OUT1: GPIO 16 is used by this module's PSRAM", ESP32_PSRAM);
  TEST_ASSERT_EQUAL_STRING("", check(one(R"("terminals":{"OUT1":{"pin":16}})")).c_str());  // no PSRAM: free
  // Pico family
  auto pico = [](const char* body) { return std::string(R"({"board_package":1,"id":"x","name":"X","mcu":"pico",)") + body + "}"; };
  refused(pico(R"("terminals":{"OUT1":{"pin":23}})").c_str(), "OUT1: GP23 belongs to the radio on a Pico W", PICO_W);
  refused(pico(R"("terminals":{"OUT1":{"pin":25}})").c_str(), "GP25 belongs to the radio", PICO_W);
  refused(pico(R"("status_led":25)").c_str(), "GP25 belongs to the radio", PICO_W);
  TEST_ASSERT_EQUAL_STRING("", check(pico(R"("status_led":"builtin")"), PICO_W).c_str());
  refused(pico(R"("terminals":{"OUT1":{"pin":25}})").c_str(), "OUT1: GP25 is the Pico's own LED: use it as status_led only", PICO_PLAIN);
  TEST_ASSERT_EQUAL_STRING("", check(pico(R"("status_led":25)"), PICO_PLAIN).c_str());
  refused(pico(R"("terminals":{"OUT1":{"pin":24}})").c_str(), "OUT1: GP24 is used by the Pico board itself", PICO_PLAIN);
  refused(pico(R"("terminals":{"OUT1":{"pin":30}})").c_str(), "OUT1: the Pico has no GP30", PICO_W);
  refused(pico(R"("analog":20)").c_str(), "analog: GP20 cannot read analog: use GP26, GP27 or GP28", PICO_W);
}

void boot_strapping_pins_are_warned_not_refused() {
  std::vector<std::string> w;
  TEST_ASSERT_EQUAL_STRING("", check(R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit",
    "terminals":{"OUT1":{"pin":23},"USER1":{"pin":5}},"status_led":2})", ESP32, &w).c_str());
  TEST_ASSERT_EQUAL(2, (int)w.size());
  TEST_ASSERT_TRUE(w[0].find("USER1: GPIO 5 is read at power-on (a boot-strapping pin)") == 0);
  TEST_ASSERT_TRUE(w[1].find("status_led: GPIO 2 is read at power-on") == 0);
}

void a_package_can_take_a_built_in_boards_pins() {
  BoardConfig c;
  std::vector<std::string> w;
  std::string why;
  JsonDocument d;
  deserializeJson(d, R"({"board_package":1,"id":"my-prop","name":"My prop","base":"proptroller","defaults":{"settings":{"name":"Lamp"}}})");
  TEST_ASSERT_TRUE_MESSAGE(checkBoardPackage(d.as<JsonVariantConst>(), ESP32, builtinBoards(ESP32), c, w, why), why.c_str());
  TEST_ASSERT_EQUAL_STRING("my-prop", c.id.c_str());
  TEST_ASSERT_EQUAL_MEMORY(&ESP32_DEVKIT, &c.map, sizeof(PinMap));
  TEST_ASSERT_TRUE(c.pixel_inverted);
  // the Proptroller board on a Pico W: the same package, the Pico's pins
  TEST_ASSERT_TRUE_MESSAGE(checkBoardPackage(d.as<JsonVariantConst>(), PICO_W, builtinBoards(PICO_W), c, w, why), why.c_str());
  TEST_ASSERT_EQUAL(15, c.map.out[0]);
  refused(R"({"board_package":1,"id":"x","name":"X","base":"proptroller","terminals":{}})", "unknown field \"terminals\" (a package with \"base\" takes its pins");
  refused(R"({"board_package":1,"id":"x","name":"X","base":"big-board"})", "no built-in board \"big-board\" for this chip (it has: proptroller, relay4-button)");
  refused(R"({"board_package":1,"id":"x","name":"X","base":"relay4-button"})", "no built-in board \"relay4-button\"", PICO_W);
}

void the_defaults_are_checked_for_their_shape() {
  refused(R"({"board_package":1,"id":"x","name":"X","base":"proptroller","defaults":{"setting":{}}})", "defaults: unknown field \"setting\"");
  refused(R"({"board_package":1,"id":"x","name":"X","base":"proptroller","defaults":{"settings":"name=Lamp"}})", "\"settings\" must be an object");
  refused(R"({"board_package":1,"id":"x","name":"X","base":"proptroller","defaults":{"settings":{"name":["a"]}}})", "setting \"name\" must be text, a number or true / false");
  refused(R"({"board_package":1,"id":"x","name":"X","base":"proptroller","defaults":{"settings":{"name":"a\nb"}}})", "setting \"name\" holds a line break");
  refused(R"({"board_package":1,"id":"x","name":"X","base":"proptroller","defaults":{"profile":{"aliases":[{"alias":"x"}]}}})", "defaults: profile:");
  refused(R"({"board_package":1,"id":"x","name":"X","base":"proptroller","defaults":{"script":5}})", "\"script\" must be the show script as text");
  std::string s = R"({"board_package":1,"id":"x","name":"X","base":"proptroller","defaults":{"script":")" + std::string(SCRIPT_MAX_BYTES + 1, '#') + "\"}}";
  refused(s.c_str(), "defaults: the script is longer than 8 KB");
  std::string profile, lines, script;
  TEST_ASSERT_TRUE(boardDefaults(R"({"defaults":{"settings":{"name":"Lamp","cooldown_ms":0,"stop_button":false},"script":"x"}})", profile, lines, script));
  TEST_ASSERT_EQUAL_STRING("name=Lamp\ncooldown_ms=0\nstop_button=false\n", lines.c_str());
  TEST_ASSERT_EQUAL_STRING("", profile.c_str());
  TEST_ASSERT_EQUAL_STRING("x", script.c_str());
}

void the_record_round_trips_and_a_damaged_or_newer_one_is_told_apart() {
  std::string json = RELAY_BOARD, back;
  bool apply = false;
  uint16_t format = 0;
  std::string rec = boardRecordPack(json, true);
  TEST_ASSERT_EQUAL(BoardRecordRead::Ok, boardRecordUnpack(rec, back, apply, format));
  TEST_ASSERT_EQUAL_STRING(json.c_str(), back.c_str());
  TEST_ASSERT_TRUE(apply);
  std::string bad = rec;
  bad[BOARD_RECORD_HEADER + 1] ^= 0x55;
  bad.resize(bad.size() - 3);
  TEST_ASSERT_EQUAL(BoardRecordRead::Damaged, boardRecordUnpack(bad, back, apply, format));
  std::string newer = rec;
  newer[4] = 9;
  TEST_ASSERT_EQUAL(BoardRecordRead::Newer, boardRecordUnpack(newer, back, apply, format));
  TEST_ASSERT_EQUAL(9, format);
  TEST_ASSERT_EQUAL(BoardRecordRead::Damaged, boardRecordUnpack("PTB", back, apply, format));
}

// ---------------------------------------------------------------- on the board

static Response setBoard(Sim& s, const std::string& package, const char* extra = "") {
  return s.request(std::string(R"({"op":"set_board","package":)") + package + extra + "}");
}

void with_nothing_stored_the_board_runs_its_built_in_board() {
  Sim s("bp");
  TEST_ASSERT_EQUAL(BoardSource::BuiltIn, s.boardBoot().source);
  Response i = s.request(R"({"op":"info"})");
  TEST_ASSERT_EQUAL_STRING("proptroller", i.doc()["result"]["board"]);
  bool cap = false;
  JsonArrayConst caps = i.doc()["result"]["capabilities"];
  for (JsonVariantConst c : caps) cap = cap || c == "board_package";
  TEST_ASSERT_TRUE(cap);
  Response g = s.request(R"({"op":"get_board"})");
  TEST_ASSERT_TRUE(g.isOk());
  TEST_ASSERT_EQUAL_STRING("esp32-devkit", g.doc()["result"]["mcu"]);
  TEST_ASSERT_EQUAL_STRING("built-in", g.doc()["result"]["running"]["source"]);
  TEST_ASSERT_TRUE(g.doc()["result"]["stored"].isNull());
  TEST_ASSERT_EQUAL_STRING("relay4-button", g.doc()["result"]["presets"][1]["id"]);
  Response p = s.request(R"({"op":"get_board","preset":"relay4-button"})");
  TEST_ASSERT_EQUAL(36, p.doc()["result"]["package"]["terminals"]["TRIG1"]["pin"].as<int>());
  TEST_ASSERT_EQUAL_STRING("high", p.doc()["result"]["package"]["terminals"]["TRIG2"]["active"]);
  TEST_ASSERT_EQUAL_STRING("not_found", s.request(R"({"op":"get_board","preset":"big-board"})").doc()["error"]["code"]);
}

void set_board_is_stored_and_a_restart_applies_it() {
  Sim s("bp");
  Response r = setBoard(s, RELAY_BOARD);
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  TEST_ASSERT_EQUAL_STRING("board_set", r.doc()["result"]["kind"]);
  TEST_ASSERT_TRUE(r.doc()["result"]["restart_needed"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("status_led: GPIO 2 is read at power-on (a boot-strapping pin): what is wired to it must not pull it the wrong way while the board starts",
                           r.doc()["result"]["warnings"][0]);
  // the old board runs until the restart: TRIG2 still active low, RLY1 still there
  TEST_ASSERT_EQUAL_STRING("proptroller", s.request(R"({"op":"info"})").doc()["result"]["board"]);
  Response g = s.request(R"({"op":"get_board"})");
  TEST_ASSERT_EQUAL_STRING("relay-board", g.doc()["result"]["stored"]["id"]);
  TEST_ASSERT_TRUE(g.doc()["result"]["stored"]["pending"].as<bool>());
  TEST_ASSERT_FALSE(s.pins().activeHigh(1));
  TEST_ASSERT_TRUE(s.request(R"({"op":"reboot"})").isOk());
  s.step(20);  // the restart
  TEST_ASSERT_EQUAL(BoardSource::Stored, s.boardBoot().source);
  TEST_ASSERT_EQUAL_STRING("relay-board", s.request(R"({"op":"info"})").doc()["result"]["board"]);
  g = s.request(R"({"op":"get_board"})");
  TEST_ASSERT_EQUAL_STRING("stored", g.doc()["result"]["running"]["source"]);
  TEST_ASSERT_FALSE(g.doc()["result"]["stored"]["pending"].as<bool>());
  // polarity: TRIG2 is active high now, and the terminals the package leaves out are gone
  TEST_ASSERT_TRUE(s.pins().activeHigh(1));
  TEST_ASSERT_FALSE(s.ctl().board().has(Channel::Rly1));
  TEST_ASSERT_FALSE(s.ctl().board().hasSound());
  TEST_ASSERT_FALSE(s.ctl().board().hasPixels());
  Response i = s.request(R"({"op":"info"})");
  JsonArrayConst terms = i.doc()["result"]["terminals"];
  for (JsonVariantConst t : terms) TEST_ASSERT_NOT_EQUAL(0, strcmp(t["name"] | "", "RLY1"));
  // an active-high button: pressing it (the pin high) is a press, idle (low) is not
  s.addExample();
  TEST_ASSERT_TRUE(s.request(R"({"op":"put_show","data":{"id":2,"name":"b","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"OUT2","cues":[{"at":0,"do":"pulse","ms":300}]}]}})").isOk());
  s.step(500);
  TEST_ASSERT_EQUAL(0, s.level(Channel::Out2));
  s.press(Input::Trig2, 150);
  s.step(50);
  TEST_ASSERT_EQUAL(1000, s.level(Channel::Out2));
}

void a_stored_package_that_cannot_be_used_leaves_every_pin_alone() {
  Sim s("bp");
  TEST_ASSERT_TRUE(s.boardFlash().save("this is not a board record"));
  s.powerCycle();
  TEST_ASSERT_EQUAL(BoardSource::None, s.boardBoot().source);
  for (int c = 0; c < CHANNEL_COUNT; c++) TEST_ASSERT_FALSE(s.ctl().board().has(ALL_CHANNELS[c]));
  for (int k = 0; k < INPUT_COUNT; k++) TEST_ASSERT_FALSE(s.ctl().board().has(ALL_INPUTS[k]));
  Response st = s.request(R"({"op":"state"})");
  TEST_ASSERT_TRUE(std::string(st.doc()["result"]["board_problem"] | "").find("could not be read (damaged record): no pins are used (nothing is driven)") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("none", s.request(R"({"op":"info"})").doc()["result"]["board"]);
  TEST_ASSERT_TRUE(s.request(R"({"op":"info"})").doc()["result"]["terminals"].as<JsonArrayConst>().size() == 0);
  // nothing is written to any output pin, whatever is asked
  uint32_t writes = 0;
  for (auto w : s.world().writes) writes += w;
  s.request(R"({"op":"set_output","channel":"OUT1","level":1000,"ms":500})");
  s.addExample();
  s.request(R"({"op":"run","show":1})");
  s.step(300);
  uint32_t after = 0;
  for (auto w : s.world().writes) after += w;
  TEST_ASSERT_EQUAL(writes, after);
  TEST_ASSERT_EQUAL(0, s.world().out[0]);
  // a new image would fail its self-check (the record is unreadable to it)
  TEST_ASSERT_FALSE(s.ctl().recordsOk());
  // a good package (or forgetting the bad one) fixes it at the next restart
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_board","preset":"proptroller"})").isOk());
  s.powerCycle();
  TEST_ASSERT_EQUAL(BoardSource::Stored, s.boardBoot().source);
  TEST_ASSERT_TRUE(s.ctl().board().has(Channel::Rly1));
}

void a_package_for_another_chip_in_the_store_is_not_used() {
  Sim s("bp");
  TEST_ASSERT_TRUE(s.boardFlash().save(boardRecordPack(R"({"board_package":1,"id":"x","name":"X","mcu":"pico","terminals":{"OUT1":{"pin":15}}})", false)));
  s.powerCycle();
  TEST_ASSERT_EQUAL(BoardSource::None, s.boardBoot().source);
  TEST_ASSERT_TRUE(s.boardBoot().problem.find("does not fit this board (this board package is for the pico socket") != std::string::npos);
}

void set_board_refusals() {
  Sim s("bp");
  Response r = setBoard(s, R"({"board_package":1,"id":"x","name":"X","mcu":"esp32-devkit","terminals":{"OUT3":{"pin":35}}})");
  TEST_ASSERT_EQUAL_STRING("invalid", r.doc()["error"]["code"]);
  TEST_ASSERT_EQUAL_STRING("board package: OUT3: GPIO 35 is input-only on the ESP32 and cannot drive an output", r.doc()["error"]["message"]);
  TEST_ASSERT_EQUAL(0, s.boardFlash().saves);  // nothing stored
  r = s.request(R"({"op":"set_board","preset":"big-board"})");
  TEST_ASSERT_EQUAL_STRING("board package: this firmware has no built-in board \"big-board\" (it has: proptroller, relay4-button)", r.doc()["error"]["message"]);
  r = s.request(R"({"op":"set_board"})");
  TEST_ASSERT_EQUAL_STRING("bad_request", r.doc()["error"]["code"]);
  r = s.request(R"({"op":"set_board","preset":"proptroller","forget":true})");
  TEST_ASSERT_EQUAL_STRING("bad_request", r.doc()["error"]["code"]);
  r = s.request(R"({"op":"set_board","preset":"proptroller","defaults":"sometimes"})");
  TEST_ASSERT_EQUAL_STRING("bad_request", r.doc()["error"]["code"]);
  // not over MQTT
  std::vector<MqttMsg> m = s.ctl().mqttOnMessage(s.ctl().mqttPrefix() + "/rpc", R"({"v":1,"id":1,"op":"set_board","preset":"relay4-button"})");
  TEST_ASSERT_TRUE_MESSAGE(m[0].payload.find("not over MQTT") != std::string::npos, m[0].payload.c_str());
  // get_board is a read: fine over MQTT
  m = s.ctl().mqttOnMessage(s.ctl().mqttPrefix() + "/rpc", R"({"v":1,"id":2,"op":"get_board"})");
  TEST_ASSERT_TRUE(m[0].payload.find("\"kind\":\"board\"") != std::string::npos);
  // the page lock: the page password over Wi-Fi; the USB serial port is local
  s.serial("pin=654321");
  s.serial("page_lock=true");
  TEST_ASSERT_EQUAL_STRING("pin", s.request(R"({"op":"set_board","preset":"relay4-button"})").doc()["error"]["code"]);
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_board","preset":"relay4-button","pin":"654321"})").isOk());
  std::string body = R"({"v":1,"op":"set_board","forget":true})";
  TEST_ASSERT_TRUE(s.ctl().callSerial(body.data(), body.size()).find("\"ok\":true") != std::string::npos);
  TEST_ASSERT_TRUE(s.request(R"({"op":"get_board"})").isOk());  // a read needs no password
}

void a_factory_reset_keeps_the_package_unless_told_to_forget_it() {
  Sim s("bp");
  TEST_ASSERT_TRUE(setBoard(s, RELAY_BOARD).isOk());
  s.powerCycle();
  s.serial("name=Bench");
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset"})").isOk());
  s.step(20);
  TEST_ASSERT_EQUAL(BoardSource::Stored, s.boardBoot().source);
  TEST_ASSERT_EQUAL_STRING("relay-board", s.boardBoot().cfg.id.c_str());
  TEST_ASSERT_TRUE(s.ctl().freshBoard());
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset","forget_board":true})").isOk());
  s.step(20);
  TEST_ASSERT_EQUAL(BoardSource::BuiltIn, s.boardBoot().source);
  TEST_ASSERT_EQUAL_STRING("proptroller", s.boardBoot().cfg.id.c_str());
}

static const char* const WITH_DEFAULTS = R"({"board_package":1,"id":"lamp-board","name":"Lamp board","base":"proptroller",
  "defaults":{"profile":{"aliases":[{"alias":"LAMP","terminal":"OUT1"}]},
              "settings":{"name":"Lamp7","cooldown_ms":0},
              "script":"effect blink:\n  wait for trig2\n  lamp = on\n  sleep 1\n  lamp = off\n"}})";

void a_factory_reset_can_keep_the_wifi_and_the_name() {
  Sim s("bp");
  TEST_ASSERT_TRUE(setBoard(s, WITH_DEFAULTS).isOk());
  s.powerCycle();
  s.serial("name=Porch");
  s.serial("wifi_ssid=Office");
  s.serial("wifi_pass=secret-one");
  s.serial("ap_pass=hotspot-two");
  s.serial("trig_debounce_ms=40");
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_script","text":"effect mine:\n  wait for trig1\n  out2 = on\n  sleep 1\n  out2 = off\n"})").isOk());
  TEST_ASSERT_EQUAL_STRING("bad_request", s.request(R"({"op":"factory_reset","keep_wifi":"yes"})").doc()["error"]["code"]);
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset","keep_wifi":true})").isOk());
  s.step(20);
  // a fresh board on its package's defaults (its script, its cooldown), the other settings back to the firmware's ...
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", s.factoryRefused.c_str(), s.factoryRefused.c_str());
  TEST_ASSERT_TRUE(s.ctl().scriptText().find("lamp = on") != std::string::npos);
  TEST_ASSERT_EQUAL(0, s.ctl().settings().fx.cooldown_ms);
  TEST_ASSERT_NOT_EQUAL(40, s.ctl().settings().trig_debounce_ms);
  // ... with its name and Wi-Fi as they were (the package's own name does not replace it)
  TEST_ASSERT_EQUAL_STRING("Porch", s.ctl().settings().name.c_str());
  TEST_ASSERT_EQUAL_STRING("Office", s.ctl().settings().wifi_ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("secret-one", s.ctl().settings().wifi_pass.c_str());
  TEST_ASSERT_EQUAL_STRING("hotspot-two", s.ctl().settings().ap_pass.c_str());
  TEST_ASSERT_EQUAL(BoardSource::Stored, s.boardBoot().source);  // the package kept too
  // written back at once: the next power-on is not fresh again and keeps a change made since
  s.serial("trig_debounce_ms=60");
  s.ctl().saveNow();
  s.powerCycle();
  TEST_ASSERT_FALSE(s.ctl().freshBoard());
  TEST_ASSERT_EQUAL(60, s.ctl().settings().trig_debounce_ms);
  TEST_ASSERT_EQUAL_STRING("Porch", s.ctl().settings().name.c_str());
  // the plain factory reset still clears the Wi-Fi
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset"})").isOk());
  s.step(20);
  TEST_ASSERT_EQUAL_STRING("", s.ctl().settings().wifi_ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("Lamp7", s.ctl().settings().name.c_str());
}

void a_factory_reset_keeping_the_wifi_on_a_board_without_defaults() {
  Sim s("bp");
  s.serial("name=Bench");
  s.serial("wifi_ssid=Office");
  s.serial("trig_debounce_ms=40");
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset","keep_wifi":true})").isOk());
  s.step(20);
  TEST_ASSERT_EQUAL_STRING("Bench", s.ctl().settings().name.c_str());
  TEST_ASSERT_EQUAL_STRING("Office", s.ctl().settings().wifi_ssid.c_str());
  TEST_ASSERT_NOT_EQUAL(40, s.ctl().settings().trig_debounce_ms);
  TEST_ASSERT_EQUAL(0, (int)s.ctl().engine().showCount());
}

void forget_goes_back_to_the_built_in_board_at_the_restart() {
  Sim s("bp");
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_board","preset":"relay4-button"})").isOk());
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING("relay4-button", s.boardBoot().cfg.id.c_str());
  Response r = s.request(R"({"op":"set_board","forget":true})");
  TEST_ASSERT_EQUAL_STRING("proptroller", r.doc()["result"]["id"]);
  TEST_ASSERT_TRUE(r.doc()["result"]["restart_needed"].as<bool>());
  Response g = s.request(R"({"op":"get_board"})");
  TEST_ASSERT_TRUE(g.doc()["result"]["stored"]["pending"].as<bool>());
  TEST_ASSERT_TRUE(g.doc()["result"]["stored"]["id"].isNull());
  s.powerCycle();
  TEST_ASSERT_EQUAL(BoardSource::BuiltIn, s.boardBoot().source);
}


void defaults_apply_once_when_asked_and_on_a_fresh_board() {
  Sim s("bp");
  s.serial("name=Mine");
  // without "apply": the restart keeps the board's own settings
  TEST_ASSERT_TRUE(setBoard(s, WITH_DEFAULTS).isOk());
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING("Mine", s.ctl().settings().name.c_str());
  // with "apply": the package's defaults once, the rest kept
  TEST_ASSERT_TRUE(setBoard(s, WITH_DEFAULTS, R"(,"defaults":"apply")").isOk());
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", s.factoryRefused.c_str(), s.factoryRefused.c_str());
  TEST_ASSERT_EQUAL_STRING("Lamp7", s.ctl().settings().name.c_str());
  TEST_ASSERT_TRUE(s.ctl().scriptText().find("lamp = on") != std::string::npos);
  s.serial("name=Changed");
  s.ctl().saveNow();
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING("Changed", s.ctl().settings().name.c_str());  // applied once
  // a factory reset: a fresh board, with the package's defaults again
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset"})").isOk());
  s.step(20);
  TEST_ASSERT_EQUAL_STRING("Lamp7", s.ctl().settings().name.c_str());
  TEST_ASSERT_TRUE(s.ctl().scriptText().find("lamp = on") != std::string::npos);
}

void the_image_defaults_give_way_to_a_stored_package() {
  Sim s("bp");
  FactoryDefaults image;
  image.settings = "name=ImageDefault\n";
  s.setFactoryDefaults(&image);
  TEST_ASSERT_TRUE(s.flash().wipe());
  s.powerCycle();
  TEST_ASSERT_EQUAL_STRING("ImageDefault", s.ctl().settings().name.c_str());
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_board","preset":"relay4-button"})").isOk());
  TEST_ASSERT_TRUE(s.request(R"({"op":"factory_reset"})").isOk());
  s.step(20);
  // a stored package without defaults: a fresh board starts from the firmware's own defaults
  TEST_ASSERT_EQUAL_STRING("relay4-button", s.boardBoot().cfg.id.c_str());
  TEST_ASSERT_NOT_EQUAL(0, strcmp("ImageDefault", s.ctl().settings().name.c_str()));
}

void sampling_reads_each_input_once_per_millisecond_on_any_board() {
  // the 1 ms sampling costs the same with a stored package: one pin read per input per sample
  auto reads = [](bool package) {
    Sim s("bp");
    if (package) {
      TEST_ASSERT_TRUE(setBoard(s, RELAY_BOARD).isOk());
    }
    s.powerCycle();
    const uint32_t before = s.world().reads[0];
    s.step(1000);
    return s.world().reads[0] - before;
  };
  const uint32_t builtin = reads(false), stored = reads(true);
  TEST_ASSERT_EQUAL(builtin, stored);
  TEST_ASSERT_UINT32_WITHIN(20, 1000, stored);
}

void a_large_package_with_a_profile_and_a_script_fits_the_store() {
  // a package with a 30-alias profile and a 2 KB script, as a project would send it
  std::string aliases;
  for (int i = 0; i < 30; i++)
    aliases += std::string(i ? "," : "") + R"({"alias":"setting_)" + std::to_string(i) + R"(","key":"out1_on_ms","label":"A label for setting )" +
               std::to_string(i) + R"(","unit":"ms","group":"Group"})";
  std::string script;
  for (int i = 0; i < 40; i++) script += "effect e" + std::to_string(i % 8) + ":\\n  wait for trig1\\n  out1 = on\\n  sleep 1\\n  out1 = off\\n";
  std::string pkg = R"({"board_package":1,"id":"big","name":"Big","base":"proptroller","defaults":{"profile":{"aliases":[)" + aliases +
                    R"(]},"settings":{"name":"Big"},"script":")" + script + "\"}}";
  std::string rec = boardRecordPack(pkg, false);
  TEST_ASSERT_TRUE_MESSAGE(rec.size() <= BOARD_STORE_CAPACITY, std::to_string(rec.size()).c_str());
  // incompressible defaults that do not fit are refused in plain words, nothing stored
  Sim s("bp");
  std::string noise;
  uint32_t x = 12345;
  for (size_t i = 0; i < SCRIPT_MAX_BYTES - 64; i++) {
    x = x * 1103515245u + 12345u;
    noise += (char)('a' + (x >> 16) % 26);
  }
  Response r = setBoard(s, std::string(R"({"board_package":1,"id":"n","name":"N","base":"proptroller","defaults":{"script":")") + noise + "\"}}");
  if (boardRecordPack("{\"defaults\":{\"script\":\"" + noise + "\"}}", false).size() > BOARD_STORE_CAPACITY) {
    TEST_ASSERT_EQUAL_STRING("internal", r.doc()["error"]["code"]);
    TEST_ASSERT_TRUE(std::string(r.doc()["error"]["message"] | "").find("send the script with set_script instead") != std::string::npos);
    TEST_ASSERT_EQUAL(0, s.boardFlash().saves);
  } else {
    TEST_ASSERT_TRUE(r.isOk());
  }
}

// get_defaults / apply_defaults: the running board's defaults read, and applied now in part (a firmware type change:
// "use the new firmware's script, keep everything else"), past the page lock once the request had the password.
static const char* const NAMED_DEFAULTS = R"({"board_package":1,"id":"lamp-board","name":"Lamp board","base":"proptroller",
  "defaults":{"profile":{"aliases":[{"alias":"LAMP","terminal":"OUT1"}]},"settings":{"name":"Lamp7","cooldown_ms":0},
              "script":"effect blink:\n  wait for trig2\n  lamp = on\n  sleep 1\n  lamp = off\n","script_name":"Lamp blink"}})";
static const char* const MY_SCRIPT = R"({"op":"set_script","text":"effect mine:\n  wait for trig1\n  out2 = on\n  sleep 1\n  out2 = off\n"})";

void get_defaults_reads_the_running_boards_defaults() {
  Sim s("bp");
  Response g = s.request(R"({"op":"get_defaults"})");
  TEST_ASSERT_EQUAL_STRING("none", g.doc()["result"]["source"]);
  TEST_ASSERT_TRUE(g.doc()["result"]["script"].isNull());
  TEST_ASSERT_EQUAL_STRING("not_found", s.request(R"({"op":"apply_defaults","only":["script"]})").doc()["error"]["code"]);
  TEST_ASSERT_TRUE(setBoard(s, NAMED_DEFAULTS).isOk());
  // a package waiting for its restart: the running (built-in) board's defaults until then
  TEST_ASSERT_EQUAL_STRING("none", s.request(R"({"op":"get_defaults"})").doc()["result"]["source"]);
  s.powerCycle();
  // replacing a running package: its defaults are not readable once the store holds the next one
  TEST_ASSERT_TRUE(s.request(R"({"op":"set_board","preset":"relay4-button"})").isOk());
  TEST_ASSERT_EQUAL_STRING("refused", s.request(R"({"op":"get_defaults"})").doc()["error"]["code"]);
  TEST_ASSERT_TRUE(setBoard(s, NAMED_DEFAULTS).isOk());
  s.powerCycle();
  g = s.request(R"({"op":"get_defaults"})");
  TEST_ASSERT_EQUAL_STRING("board_package", g.doc()["result"]["source"]);
  TEST_ASSERT_EQUAL_STRING("lamp-board", g.doc()["result"]["board"]);
  TEST_ASSERT_EQUAL_STRING("Lamp blink", g.doc()["result"]["script_name"]);
  TEST_ASSERT_TRUE(std::string(g.doc()["result"]["script"] | "").find("lamp = on") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("Lamp7", g.doc()["result"]["settings"]["name"]);
  TEST_ASSERT_EQUAL_STRING("LAMP", g.doc()["result"]["profile"]["aliases"][0]["alias"]);
  refused(R"({"board_package":1,"id":"x","name":"X","base":"proptroller","defaults":{"script_name":""}})", "\"script_name\" must be 1 to 60 characters");
}

void apply_defaults_replaces_only_what_is_asked() {
  Sim s("bp");
  TEST_ASSERT_TRUE(setBoard(s, NAMED_DEFAULTS).isOk());
  s.powerCycle();  // not fresh: the board keeps its own settings (no "apply")
  s.serial("name=Mine");
  TEST_ASSERT_TRUE(s.request(MY_SCRIPT).isOk());
  // the script only: settings, profile and name stay
  Response a = s.request(R"({"op":"apply_defaults","only":["script"]})");
  TEST_ASSERT_TRUE_MESSAGE(a.isOk(), a.json().c_str());
  TEST_ASSERT_EQUAL_STRING("defaults_applied", a.doc()["result"]["kind"]);
  TEST_ASSERT_EQUAL_STRING("script", a.doc()["result"]["applied"][0]);
  TEST_ASSERT_EQUAL(0, a.doc()["result"]["refused"].as<JsonArrayConst>().size());
  TEST_ASSERT_EQUAL_STRING("Mine", s.ctl().settings().name.c_str());
  // the package's script names LAMP, which only its profile has: refused in plain words, the old script kept
  TEST_ASSERT_TRUE(std::string(a.json()).find("refused") != std::string::npos);
  // the script and its profile together
  TEST_ASSERT_TRUE(s.request(MY_SCRIPT).isOk());
  a = s.request(R"({"op":"apply_defaults","only":["script","profile"]})");
  TEST_ASSERT_EQUAL(0, a.doc()["result"]["refused"].as<JsonArrayConst>().size());
  TEST_ASSERT_TRUE(s.ctl().scriptText().find("lamp = on") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("Mine", s.ctl().settings().name.c_str());
  // everything (no "only"): the name too
  TEST_ASSERT_TRUE(s.request(R"({"op":"apply_defaults"})").isOk());
  TEST_ASSERT_EQUAL_STRING("Lamp7", s.ctl().settings().name.c_str());
  TEST_ASSERT_EQUAL_STRING("bad_request", s.request(R"({"op":"apply_defaults","only":["wifi"]})").doc()["error"]["code"]);
}

void apply_defaults_needs_the_page_password_and_then_applies() {
  Sim s("bp");
  TEST_ASSERT_TRUE(setBoard(s, NAMED_DEFAULTS).isOk());
  s.powerCycle();
  s.serial("pin=654321");
  s.serial("page_lock=true");
  TEST_ASSERT_EQUAL_STRING("pin", s.request(R"({"op":"apply_defaults","only":["script","profile"]})").doc()["error"]["code"]);
  TEST_ASSERT_TRUE(s.request(R"({"op":"get_defaults"})").isOk());  // a read
  Response a = s.request(R"({"op":"apply_defaults","only":["script","profile"],"pin":"654321"})");
  TEST_ASSERT_TRUE_MESSAGE(a.isOk(), a.json().c_str());
  TEST_ASSERT_EQUAL(0, a.doc()["result"]["refused"].as<JsonArrayConst>().size());
  TEST_ASSERT_TRUE(s.ctl().scriptText().find("lamp = on") != std::string::npos);
}

void the_images_own_defaults_are_the_firmwares() {
  Sim s("bp");
  FactoryDefaults image;
  image.settings = "name=ImageDefault\n# a comment\ncooldown_ms = 0\n";
  image.script = "effect a:\n  wait for trig1\n  out1 = on\n  sleep 1\n  out1 = off\n";
  image.script_name = "Door knock";
  s.setFactoryDefaults(&image);
  s.powerCycle();
  Response g = s.request(R"({"op":"get_defaults"})");
  TEST_ASSERT_EQUAL_STRING("firmware", g.doc()["result"]["source"]);
  TEST_ASSERT_EQUAL_STRING("Door knock", g.doc()["result"]["script_name"]);
  TEST_ASSERT_EQUAL_STRING("ImageDefault", g.doc()["result"]["settings"]["name"]);
  TEST_ASSERT_EQUAL_STRING("0", g.doc()["result"]["settings"]["cooldown_ms"]);
  TEST_ASSERT_TRUE(s.request(MY_SCRIPT).isOk());
  TEST_ASSERT_TRUE(s.request(R"({"op":"apply_defaults","only":["script"]})").isOk());
  TEST_ASSERT_TRUE(s.ctl().scriptText().find("out1 = on") != std::string::npos);
}

// The stored file exactly as the board writes it (/board-a.bin: the A/B frame around the board record), checked in
// as test/fixtures/records/board-record.bin with its package board-record.json: what a tool that builds a file system
// image for a first flash must produce (after decoding: its LZ stream may differ). BOARD_FIXTURE_WRITE=1 rewrites it.
struct MemFs : FileIo {
  std::map<std::string, std::string> files;
  bool read(const char* p, std::string& out) override {
    auto it = files.find(p);
    if (it == files.end()) return false;
    out = it->second;
    return true;
  }
  bool write(const char* p, const std::string& d) override { files[p] = d; return true; }
  bool remove(const char* p) override { return files.erase(p) > 0; }
};
static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
static const std::string FIX = "test/fixtures/records/";

void the_stored_file_matches_the_fixture_and_reads_back_on_the_board() {
  std::string json = readFile(FIX + "board-record.json");
  while (!json.empty() && (json.back() == '\n' || json.back() == '\r')) json.pop_back();
  TEST_ASSERT_FALSE_MESSAGE(json.empty(), "test/fixtures/records/board-record.json is missing");
  MemFs fs;
  ABStorage store(fs, "/board-a.bin", "/board-b.bin", BOARD_STORE_CAPACITY);
  TEST_ASSERT_TRUE(store.save(boardRecordPack(json, true)));
  const std::string file = fs.files["/board-a.bin"];
  if (getenv("BOARD_FIXTURE_WRITE")) {
    std::ofstream(FIX + "board-record.bin", std::ios::binary) << file;
  }
  TEST_ASSERT_EQUAL(file.size(), readFile(FIX + "board-record.bin").size());
  TEST_ASSERT_TRUE(file == readFile(FIX + "board-record.bin"));
  // a file system holding only that file (a first flash): the board starts on that package and applies its defaults
  MemFs fresh;
  fresh.files["/board-a.bin"] = readFile(FIX + "board-record.bin");
  ABStorage again(fresh, "/board-a.bin", "/board-b.bin", BOARD_STORE_CAPACITY);
  BoardBoot b = boardAtBoot(&again, ESP32, builtinBoards(ESP32));
  TEST_ASSERT_EQUAL(BoardSource::Stored, b.source);
  TEST_ASSERT_TRUE(b.apply_defaults);
  TEST_ASSERT_TRUE(b.has_defaults);
  TEST_ASSERT_TRUE(inputActiveHigh(b.cfg.map, 1));
}

// firmware/board-packages/: the built-in boards as package files (what get_board "preset" answers), every one of them
// accepted by the board. BOARD_FIXTURE_WRITE=1 rewrites them.
void the_example_files_are_the_built_in_boards() {
  struct Example { const char* file; ChipRules chip; const char* id; };
  const Example ex[] = {{"proptroller-esp32.json", ESP32, "proptroller"}, {"proptroller-pico.json", PICO_W, "proptroller"},
                        {"relay4-button.json", ESP32, "relay4-button"}};
  for (auto& e : ex) {
    const std::vector<BuiltinBoard> all = builtinBoards(e.chip);
    const BuiltinBoard* b = findBuiltin(all, e.id);
    TEST_ASSERT_NOT_NULL(b);
    JsonDocument d;
    builtinToJson(*b, e.chip, d.to<JsonObject>());
    std::string want;
    serializeJsonPretty(d, want);
    want += "\n";
    const std::string path = std::string("../board-packages/") + e.file;
    if (getenv("BOARD_FIXTURE_WRITE")) std::ofstream(path, std::ios::binary) << want;
    const std::string got = readFile(path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want.c_str(), got.c_str(), path.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", check(got, e.chip).c_str(), path.c_str());
  }
  // the Pico family file fits the plain Pico too ("builtin" is its LED)
  TEST_ASSERT_EQUAL_STRING("", check(readFile("../board-packages/proptroller-pico.json"), PICO_PLAIN).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(every_built_in_board_passes_the_pin_rules);
  RUN_TEST(a_compiled_in_board_becomes_the_default);
  RUN_TEST(a_bad_package_is_refused_in_plain_words);
  RUN_TEST(the_pin_rules_name_the_terminal_and_the_reason);
  RUN_TEST(boot_strapping_pins_are_warned_not_refused);
  RUN_TEST(a_package_can_take_a_built_in_boards_pins);
  RUN_TEST(the_defaults_are_checked_for_their_shape);
  RUN_TEST(the_record_round_trips_and_a_damaged_or_newer_one_is_told_apart);
  RUN_TEST(with_nothing_stored_the_board_runs_its_built_in_board);
  RUN_TEST(set_board_is_stored_and_a_restart_applies_it);
  RUN_TEST(a_stored_package_that_cannot_be_used_leaves_every_pin_alone);
  RUN_TEST(a_package_for_another_chip_in_the_store_is_not_used);
  RUN_TEST(set_board_refusals);
  RUN_TEST(a_factory_reset_keeps_the_package_unless_told_to_forget_it);
  RUN_TEST(a_factory_reset_can_keep_the_wifi_and_the_name);
  RUN_TEST(a_factory_reset_keeping_the_wifi_on_a_board_without_defaults);
  RUN_TEST(forget_goes_back_to_the_built_in_board_at_the_restart);
  RUN_TEST(defaults_apply_once_when_asked_and_on_a_fresh_board);
  RUN_TEST(the_image_defaults_give_way_to_a_stored_package);
  RUN_TEST(sampling_reads_each_input_once_per_millisecond_on_any_board);
  RUN_TEST(a_large_package_with_a_profile_and_a_script_fits_the_store);
  RUN_TEST(the_stored_file_matches_the_fixture_and_reads_back_on_the_board);
  RUN_TEST(the_example_files_are_the_built_in_boards);
  RUN_TEST(get_defaults_reads_the_running_boards_defaults);
  RUN_TEST(apply_defaults_replaces_only_what_is_asked);
  RUN_TEST(apply_defaults_needs_the_page_password_and_then_applies);
  RUN_TEST(the_images_own_defaults_are_the_firmwares);
  return UNITY_END();
}
