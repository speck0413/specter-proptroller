// GPIO maps for the two MCU options (hardware/README.md "GPIO map"). Fit either a 30-pin ESP32 DevKit or a
// Raspberry Pi Pico family board, never both. The bare-devkit maps (no Proptroller board) are further down:
// ESP32_DEVKIT_BARE, ESP32S3_DEVKIT, ESP32C3_SUPERMINI (firmware/BARE-DEVKIT.md; their BSPs in bsp/).
//
// | Function                              | ESP32 DevKit     | Pico                  |
// |---------------------------------------|------------------|-----------------------|
// | OUT1-4 gate (PWM, active high)        | 23, 22, 21, 19   | GP15, GP14, GP12, GP11|
// | RLY1-2 coil driver (active high)      | 25, 26           | GP20, GP21            |
// | CONTACT1-2 opto LED (active high)     | 27, 32           | GP22, GP18            |
// | TRIG1, TRIG2 (active low, board 4.7k) | 36, 18           | GP16, GP10            |
// | USER1-3 (active low, internal pull-up)| 13, 14, 5        | GP28, GP27, GP9       |
// | PIXEL data (inverted by the board)    | 33               | GP19                  |
// | DFPlayer RX (MCU TX)                  | 17               | GP8                   |
// | DFPlayer TX (MCU RX), expansion S6    | 34               | GP7 (PIO UART)        |
// | I2C SDA / SCL (S1 / S2)               | 4 / 16           | GP4 / GP5             |
// | analog in (S3)                        | 39               | GP26                  |
// | expansion S4, S5                      | 15, 35           | GP3, GP17             |
// | status LED                            | 2                | 25 (plain Pico only)  |
//
// ESP32 GPIO34-39 are input-only and have no pulls: TRIG1 (36) relies on the board's pull-up.
#pragma once
#include <stdint.h>

namespace pt {

// The chip family a pin map is for. Esp32DevKit is the classic ESP32 (ESP32-WROOM-32 / WROVER, the module the
// Proptroller board takes); Esp32S3 and Esp32C3 run only bare (no Proptroller board takes them).
enum class Mcu : uint8_t { Esp32DevKit, Pico, Esp32S3, Esp32C3 };

static const uint8_t NO_PIN = 255;
// status_led: the MCU board's own LED that is not on a GPIO of its own (the Pico W / Pico 2 W LED sits on the radio
// chip): the core's LED_BUILTIN
static const uint8_t STATUS_LED_BUILTIN = 254;

struct PinMap {
  Mcu mcu;
  uint8_t out[4];
  uint8_t relay[2];
  uint8_t contact[2];
  uint8_t trig[2];
  uint8_t user[3];
  uint8_t pixel;
  uint8_t df_tx;
  uint8_t df_rx;
  uint8_t sda, scl;
  uint8_t analog_in;
  uint8_t exp[2];
  uint8_t status_led;  // NO_PIN if the MCU board has none on a GPIO
  // Inputs, one bit each (bit 0 TRIG1, 1 TRIG2, 2-4 USER1-3): active_high = the input is active when the pin is
  // HIGH (a button with a pull-down; default active low); external_pull = the board has its own resistor (no
  // internal pull), else the internal pull-up (active low) or pull-down (active high) is used.
  uint8_t active_high;
  uint8_t external_pull;
};

constexpr PinMap ESP32_DEVKIT = {Mcu::Esp32DevKit, {23, 22, 21, 19}, {25, 26}, {27, 32}, {36, 18}, {13, 14, 5},
                                    33, 17, 34, 4, 16, 39, {15, 35}, 2, 0, 0x03};

constexpr PinMap PICO = {Mcu::Pico, {15, 14, 12, 11}, {20, 21}, {22, 18}, {16, 10}, {28, 27, 9},
                            19, 8, 7, 4, 5, 26, {3, 17}, 25, 0, 0x03};

// Another board (BOARD PROFILE): build with -D PROPTROLLER_BOARD_HEADER='"my_board.h"', a header that defines
//   constexpr pt::PinMap PROPTROLLER_BOARD_MAP = {...};
// in this layout. A terminal (OUT, RLY, CC, TRIG, USER) or function set to NO_PIN is never configured, driven or
// read, and is left out of the board's terminals (info, the phone page). The checks below run on it too.

// ---- Bare devkits (no Proptroller board; firmware/BARE-DEVKIT.md). Every output is plain 3.3 V logic: it needs a
// driver (a MOSFET module, a relay module) to switch a load. The PIXEL data is not inverted. Each map is copied,
// with a comment on every line, into bsp/<board>/proptroller_board.h (test/test_bsp_generic checks they agree) and
// into firmware/board-packages/<board>.json.
//
// ESP32 30-pin DevKit, bare: every output on the same GPIO as on the Proptroller board (so the two images never
// drive one of the other board's inputs), the inputs moved to GPIOs with internal pull-ups (a bare devkit has no
// 4.7k resistors), no I2C (the firmware does not use it).
constexpr PinMap ESP32_DEVKIT_BARE = {Mcu::Esp32DevKit, {23, 22, 21, 19}, {25, 26}, {27, 32}, {13, 18}, {14, 4, 5},
                                      33, 17, 16, NO_PIN, NO_PIN, 39, {15, 35}, 2, 0, 0x00};
// ESP32-S3 DevKitC-1 style: outputs and inputs on plain GPIOs (no strapping, USB, UART0, flash / PSRAM pins,
// not the RGB LED's 38 / 48); no status LED (the board's LED is an RGB pixel).
constexpr PinMap ESP32S3_DEVKIT = {Mcu::Esp32S3, {4, 5, 6, 7}, {15, 16}, {17, 18}, {9, 10}, {11, 12, 13},
                                   8, 40, 41, NO_PIN, NO_PIN, 1, {47, 14}, NO_PIN, 0, 0x00};
// ESP32-C3 SuperMini: 13 GPIOs on its pins, so no RLY1-2, CC1-2, USER3, I2C or expansion port (the C3 has two
// UARTs: the sound port takes the second, so no DMX); USER1 is the board's BOOT button (GPIO9); no status LED
// (GPIO8's LED is a strapping pin and lit by a low level on most clones).
constexpr PinMap ESP32C3_SUPERMINI = {Mcu::Esp32C3, {5, 6, 7, 10}, {NO_PIN, NO_PIN}, {NO_PIN, NO_PIN}, {3, 4},
                                      {9, 2, NO_PIN}, 1, 21, 20, NO_PIN, NO_PIN, 0, {NO_PIN, NO_PIN}, NO_PIN, 0, 0x00};

// The GPIO of output channel 0-7 (OUT1-4, RLY1-2, CC1-2) and of input 0-4 (TRIG1-2, USER1-3).
constexpr uint8_t channelPin(const PinMap& m, int ch) { return ch < 4 ? m.out[ch] : ch < 6 ? m.relay[ch - 4] : m.contact[ch - 6]; }
constexpr uint8_t inputPin(const PinMap& m, int k) { return k < 2 ? m.trig[k] : m.user[k - 2]; }
constexpr bool inputActiveHigh(const PinMap& m, int k) { return (m.active_high >> k) & 1; }
constexpr bool inputExternalPull(const PinMap& m, int k) { return (m.external_pull >> k) & 1; }

// Pico-only expansion pins S7-S11.
static const uint8_t PICO_ONLY_EXPANSION[5] = {0, 1, 2, 6, 13};
constexpr uint8_t ESP32_INPUT_ONLY[6] = {34, 35, 36, 37, 38, 39};
static const uint8_t ESP32_STRAPPING[5] = {0, 2, 5, 12, 15};

// Checked at compile time: no GPIO serves two functions, and on the ESP32 no output sits on an input-only pin.
// (C++14 constexpr loops: every PlatformIO env builds as C++17; an Arduino IDE build on an older default such as
// gnu++11 skips the check, which the PlatformIO builds have already made.)
#if __cplusplus >= 201402L
constexpr bool pinsDistinct(const PinMap& m) {
  const uint8_t v[22] = {m.out[0], m.out[1], m.out[2], m.out[3], m.relay[0], m.relay[1], m.contact[0], m.contact[1],
                         m.trig[0], m.trig[1], m.user[0], m.user[1], m.user[2], m.pixel, m.df_tx, m.df_rx, m.sda, m.scl,
                         m.analog_in, m.exp[0], m.exp[1], m.status_led};
  for (int i = 0; i < 22; i++)
    for (int j = i + 1; j < 22; j++)
      if (v[i] == v[j] && v[i] != NO_PIN) return false;
  return true;
}
constexpr bool outputsCanDrive(const PinMap& m) {
  const uint8_t v[11] = {m.out[0], m.out[1], m.out[2], m.out[3], m.relay[0], m.relay[1], m.contact[0], m.contact[1],
                         m.pixel, m.df_tx, m.status_led};
  for (int i = 0; i < 11; i++)
    for (int j = 0; j < 6; j++)
      if (m.mcu == Mcu::Esp32DevKit && v[i] == ESP32_INPUT_ONLY[j]) return false;
  return true;
}
// ESP32 GPIO34-39 have no internal pulls: an input there needs the board's own resistor (external_pull)
constexpr bool internalPullsPossible(const PinMap& m) {
  for (int k = 0; k < 5; k++)
    for (int j = 0; j < 6; j++)
      if (m.mcu == Mcu::Esp32DevKit && !inputExternalPull(m, k) && inputPin(m, k) == ESP32_INPUT_ONLY[j]) return false;
  return true;
}
static_assert(pinsDistinct(ESP32_DEVKIT) && pinsDistinct(PICO), "a GPIO serves two board functions");
static_assert(internalPullsPossible(ESP32_DEVKIT), "an ESP32 input on GPIO34-39 needs external_pull");
static_assert(outputsCanDrive(ESP32_DEVKIT), "an ESP32 output on an input-only GPIO (34-39)");
static_assert(pinsDistinct(ESP32_DEVKIT_BARE) && pinsDistinct(ESP32S3_DEVKIT) && pinsDistinct(ESP32C3_SUPERMINI),
              "a GPIO serves two board functions");
static_assert(internalPullsPossible(ESP32_DEVKIT_BARE) && outputsCanDrive(ESP32_DEVKIT_BARE), "the bare ESP32 map");
#endif

// Every GPIO used by a board function (22 entries; no duplicates in a valid map).
inline void allPins(const PinMap& m, uint8_t out[22]) {
  int i = 0;
  for (auto p : m.out) out[i++] = p;
  for (auto p : m.relay) out[i++] = p;
  for (auto p : m.contact) out[i++] = p;
  for (auto p : m.trig) out[i++] = p;
  for (auto p : m.user) out[i++] = p;
  out[i++] = m.pixel;
  out[i++] = m.df_tx;
  out[i++] = m.df_rx;
  out[i++] = m.sda;
  out[i++] = m.scl;
  out[i++] = m.analog_in;
  out[i++] = m.exp[0];
  out[i++] = m.exp[1];
  out[i++] = m.status_led;
}

// Output-driving pins (OUT, relays, contacts, pixel, DF TX, status LED).
inline void outputPins(const PinMap& m, uint8_t out[11]) {
  const uint8_t v[11] = {m.out[0], m.out[1], m.out[2], m.out[3], m.relay[0], m.relay[1],
                         m.contact[0], m.contact[1], m.pixel, m.df_tx, m.status_led};
  for (int i = 0; i < 11; i++) out[i] = v[i];
}

}  // namespace pt
