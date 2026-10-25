// The bare-devkit board support packages (bsp/esp32-devkit-bare, bsp/esp32-s3-devkit, bsp/esp32-c3-supermini;
// firmware/BARE-DEVKIT.md): each BSP header is the library's map (pins.h) and the built-in board of its chip, and each
// map keeps the wiring rules, checked here from the chips' datasheets (not from board_package.cpp's tables):
//  * no GPIO serves two functions;
//  * nothing that switches a load or drives a line (OUT1-4, RLY1-2, CC1-2, PIXEL, sound TX) sits on a strapping,
//    flash / PSRAM, USB or USB-serial pin, or on an input-only pin;
//  * input-only pins (classic ESP32 34-39) carry inputs only, and an input there needs the board's own resistor;
//  * the analog input is on ADC1 (ADC2 stops while Wi-Fi runs);
//  * the pixel data is not inverted (no level shifter on a bare devkit).
// The status LED (the classic DevKit's own LED on GPIO2) and expansion S4 (the DMX line, touched only when a show
// script starts DMX, long after power-on) are on strapping pins of the classic ESP32 by design, as on the
// Proptroller board: they are checked against flash, USB and input-only pins only.
#include <unity.h>
#include <string.h>
#include <initializer_list>
#include "proptroller/board_package.h"

using namespace pt;

void setUp() {}
void tearDown() {}

// each header in its own namespace (they all define PROPTROLLER_BOARD_MAP and the same macros)
namespace bare {
#include "../../bsp/esp32-devkit-bare/proptroller_board.h"
static const char* const ID = PROPTROLLER_BOARD_ID;
static const char* const NAME = PROPTROLLER_BOARD_NAME;
static const bool INVERTED = PROPTROLLER_BOARD_PIXEL_INVERTED;
}  // namespace bare
#undef PROPTROLLER_BOARD_ID
#undef PROPTROLLER_BOARD_NAME
#undef PROPTROLLER_BOARD_PIXEL_INVERTED
namespace s3 {
#include "../../bsp/esp32-s3-devkit/proptroller_board.h"
static const char* const ID = PROPTROLLER_BOARD_ID;
static const char* const NAME = PROPTROLLER_BOARD_NAME;
static const bool INVERTED = PROPTROLLER_BOARD_PIXEL_INVERTED;
}  // namespace s3
#undef PROPTROLLER_BOARD_ID
#undef PROPTROLLER_BOARD_NAME
#undef PROPTROLLER_BOARD_PIXEL_INVERTED
namespace c3 {
#include "../../bsp/esp32-c3-supermini/proptroller_board.h"
static const char* const ID = PROPTROLLER_BOARD_ID;
static const char* const NAME = PROPTROLLER_BOARD_NAME;
static const bool INVERTED = PROPTROLLER_BOARD_PIXEL_INVERTED;
}  // namespace c3

// What each chip's pins are (datasheets: ESP32 v4.x section 2.2 and 2.4, ESP32-S3 v1.x 2.3, ESP32-C3 v1.x 2.3; the
// module / board pages for the flash, PSRAM and USB pins).
struct ChipPins {
  Mcu mcu;
  const char* name;
  std::initializer_list<int> strapping, flash, usb, input_only, adc1;
};
static const ChipPins ESP32_PINS = {Mcu::Esp32DevKit, "ESP32", {0, 2, 5, 12, 15}, {6, 7, 8, 9, 10, 11}, {1, 3},
                                    {34, 35, 36, 37, 38, 39}, {32, 33, 34, 35, 36, 37, 38, 39}};
// 26-32 flash / PSRAM, 33-37 octal flash / PSRAM; 19 / 20 USB, 43 / 44 UART0 (the USB bridge)
static const ChipPins S3_PINS = {Mcu::Esp32S3, "ESP32-S3", {0, 3, 45, 46}, {26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37},
                                 {19, 20, 43, 44}, {}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
// 12-17 flash; 18 / 19 USB
static const ChipPins C3_PINS = {Mcu::Esp32C3, "ESP32-C3", {2, 8, 9}, {12, 13, 14, 15, 16, 17}, {18, 19}, {}, {0, 1, 2, 3, 4}};

static bool in(std::initializer_list<int> set, uint8_t p) {
  for (int x : set)
    if (x == p) return true;
  return false;
}

static void checkRules(const PinMap& m, const ChipPins& c) {
  TEST_ASSERT_TRUE(m.mcu == c.mcu);
  TEST_ASSERT_TRUE_MESSAGE(pinsDistinct(m), c.name);
  // the drivers of loads and lines
  const uint8_t drive[] = {m.out[0], m.out[1], m.out[2], m.out[3], m.relay[0], m.relay[1], m.contact[0], m.contact[1],
                           m.pixel, m.df_tx};
  const char* const drive_names[] = {"OUT1", "OUT2", "OUT3", "OUT4", "RLY1", "RLY2", "CC1", "CC2", "PIXEL", "sound TX"};
  for (size_t i = 0; i < sizeof drive; i++) {
    const uint8_t p = drive[i];
    if (p == NO_PIN) continue;
    std::string who = std::string(c.name) + " " + drive_names[i] + " on GPIO " + std::to_string(p);
    TEST_ASSERT_FALSE_MESSAGE(in(c.strapping, p), (who + ": a strapping pin").c_str());
    TEST_ASSERT_FALSE_MESSAGE(in(c.flash, p), (who + ": a flash pin").c_str());
    TEST_ASSERT_FALSE_MESSAGE(in(c.usb, p), (who + ": a USB pin").c_str());
    TEST_ASSERT_FALSE_MESSAGE(in(c.input_only, p), (who + ": an input-only pin").c_str());
  }
  // the status LED and S4 (see the top of this file)
  for (uint8_t p : {m.status_led, m.exp[0]}) {
    if (p == NO_PIN) continue;
    TEST_ASSERT_FALSE(in(c.flash, p) || in(c.usb, p) || in(c.input_only, p));
  }
  // the inputs: never on flash or USB pins; on an input-only pin only with the board's own resistor
  for (int k = 0; k < 5; k++) {
    const uint8_t p = inputPin(m, k);
    if (p == NO_PIN) continue;
    TEST_ASSERT_FALSE(in(c.flash, p) || in(c.usb, p));
    if (in(c.input_only, p)) TEST_ASSERT_TRUE(inputExternalPull(m, k));
  }
  for (uint8_t p : {m.df_rx, m.exp[1], m.sda, m.scl})
    if (p != NO_PIN) TEST_ASSERT_FALSE(in(c.flash, p) || in(c.usb, p));
  // a bare devkit has no resistors of its own on the terminals: every TRIG / USER input uses the chip's pull
  TEST_ASSERT_EQUAL(0, m.external_pull);
  if (m.analog_in != NO_PIN) TEST_ASSERT_TRUE_MESSAGE(in(c.adc1, m.analog_in), c.name);
}

void every_bare_map_keeps_the_wiring_rules() {
  checkRules(ESP32_DEVKIT_BARE, ESP32_PINS);
  checkRules(ESP32S3_DEVKIT, S3_PINS);
  checkRules(ESP32C3_SUPERMINI, C3_PINS);
  // the board's own checks agree (and name nothing worse than a warning)
  for (const PinMap* m : {&ESP32_DEVKIT_BARE, &ESP32S3_DEVKIT, &ESP32C3_SUPERMINI}) {
    ChipRules chip;
    chip.mcu = m->mcu;
    std::vector<std::string> w;
    std::string why;
    TEST_ASSERT_TRUE_MESSAGE(checkPinMap(*m, chip, w, why), why.c_str());
    for (auto& x : w)  // warnings only for inputs, the status LED, S4 and the C3's sound port
      TEST_ASSERT_TRUE_MESSAGE(x.rfind("OUT", 0) != 0 && x.rfind("RLY", 0) != 0 && x.rfind("CC", 0) != 0 && x.rfind("pixel", 0) != 0, x.c_str());
  }
}

// The classic ESP32's bare map keeps every output of the Proptroller board on its GPIO, so neither image ever drives
// a pin the other board reads (an update with the wrong one moves inputs, never an output onto an input).
void the_bare_esp32_drives_only_what_the_proptroller_board_drives() {
  const PinMap& b = ESP32_DEVKIT_BARE;
  const PinMap& p = ESP32_DEVKIT;
  for (int ch = 0; ch < 8; ch++) TEST_ASSERT_EQUAL(channelPin(p, ch), channelPin(b, ch));
  TEST_ASSERT_EQUAL(p.pixel, b.pixel);
  TEST_ASSERT_EQUAL(p.df_tx, b.df_tx);
  TEST_ASSERT_EQUAL(p.exp[0], b.exp[0]);
  TEST_ASSERT_EQUAL(p.status_led, b.status_led);
}

static void sameMap(const PinMap& a, const PinMap& b) { TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(PinMap)); }

void each_bsp_header_is_the_librarys_map_and_built_in_board() {
  sameMap(bare::PROPTROLLER_BOARD_MAP, ESP32_DEVKIT_BARE);
  sameMap(s3::PROPTROLLER_BOARD_MAP, ESP32S3_DEVKIT);
  sameMap(c3::PROPTROLLER_BOARD_MAP, ESP32C3_SUPERMINI);
  struct B { const char* id; const char* name; bool inverted; const PinMap* map; Mcu mcu; };
  const B all[] = {{bare::ID, bare::NAME, bare::INVERTED, &ESP32_DEVKIT_BARE, Mcu::Esp32DevKit},
                   {s3::ID, s3::NAME, s3::INVERTED, &ESP32S3_DEVKIT, Mcu::Esp32S3},
                   {c3::ID, c3::NAME, c3::INVERTED, &ESP32C3_SUPERMINI, Mcu::Esp32C3}};
  for (auto& x : all) {
    TEST_ASSERT_FALSE(x.inverted);
    const BuiltinBoard* b = bareDevkit(x.mcu);
    TEST_ASSERT_NOT_NULL_MESSAGE(b, x.id);
    TEST_ASSERT_EQUAL_STRING(x.id, b->id);
    TEST_ASSERT_EQUAL_STRING(x.name, b->name);
    sameMap(b->map, *x.map);
    TEST_ASSERT_FALSE(b->pixel_inverted);
  }
  // the stock ESP32 image keeps its presets (proptroller, relay4-button): the bare map is a package file there
  ChipRules esp32;
  TEST_ASSERT_NULL(findBuiltin(builtinBoards(esp32), bare::ID));
  // on the S3 and the C3 the devkit is the default (no Proptroller board takes them)
  ChipRules chip;
  chip.mcu = Mcu::Esp32S3;
  TEST_ASSERT_EQUAL_STRING(s3::ID, builtinBoards(chip)[0].id);
  chip.mcu = Mcu::Esp32C3;
  TEST_ASSERT_EQUAL_STRING(c3::ID, builtinBoards(chip)[0].id);
  // compiled in, the BSP comes first on its chip (as the image's own board)
  BuiltinBoard img = {bare::ID, "x", bare::PROPTROLLER_BOARD_MAP, bare::INVERTED};
  chip.mcu = Mcu::Esp32DevKit;
  TEST_ASSERT_EQUAL_STRING(bare::ID, builtinBoards(chip, &img)[0].id);
}

// What the C3 leaves out, on purpose (BARE-DEVKIT.md): no relays, contacts, USER3, expansion port, status LED.
void the_c3_leaves_out_what_does_not_fit() {
  const PinMap& m = ESP32C3_SUPERMINI;
  for (int ch = 4; ch < 8; ch++) TEST_ASSERT_EQUAL(NO_PIN, channelPin(m, ch));
  TEST_ASSERT_EQUAL(NO_PIN, m.user[2]);
  TEST_ASSERT_EQUAL(NO_PIN, m.exp[0]);
  TEST_ASSERT_EQUAL(NO_PIN, m.exp[1]);
  TEST_ASSERT_EQUAL(NO_PIN, m.status_led);
  TEST_ASSERT_EQUAL(9, m.user[0]);  // the BOOT button
  for (int ch = 0; ch < 4; ch++) TEST_ASSERT_NOT_EQUAL(NO_PIN, channelPin(m, ch));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(every_bare_map_keeps_the_wiring_rules);
  RUN_TEST(the_bare_esp32_drives_only_what_the_proptroller_board_drives);
  RUN_TEST(each_bsp_header_is_the_librarys_map_and_built_in_board);
  RUN_TEST(the_c3_leaves_out_what_does_not_fit);
  return UNITY_END();
}
