// Board support package (BSP): a bare ESP32-S3 DevKit (ESP32-S3-DevKitC-1 and the many boards laid out like it:
// ESP32-S3-WROOM-1 module, two USB ports), on its own or on a breakout that brings every pin out to terminals.
// Built by `pio run -e esp32s3_generic`; the whole story, with wiring tables: firmware/BARE-DEVKIT.md.
//
// READ THIS FIRST: every output here is a plain 3.3 V logic pin that gives a few milliamps. It cannot switch a
// lamp, a solenoid, a motor or a relay coil by itself. Put a driver between the pin and the load: a MOSFET module
// (for DC loads) or a relay module (for anything else). No Proptroller board takes an S3: nothing on this board
// is protected, inverted or rated by the Proptroller hardware.
//
// The USB port: plug into the one marked UART (or COM): the firmware's serial port (Specter Studio, the console)
// is UART0 behind that port's USB bridge, as on the classic DevKit. The other port (USB, the chip's own) is
// used only to flash it if the UART port is missing.
//
// ------------------------------------------------------------------------------------------------------------
// The DevKitC-1's pins (USB ports at the bottom, left row then right row):
//   left (J1) : 3V3 3V3 RST 4 5 6 7 15 16 17 18 8 3 46 9 10 11 12 13 14 5V GND
//   right (J3): GND 43(TX) 44(RX) 1 2 42 41 40 39 38 37 36 35 0 45 48 47 21 20 19 GND GND
// (other S3 boards put the same GPIOs elsewhere: go by the GPIO numbers printed next to the pins)
//
// Which GPIO is safe for what (every S3 GPIO can be an output; none is input-only):
//   any use               : 1 2 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 21 39 40 41 42 47
//   analog (with Wi-Fi on): 1-10 only (ADC1; ADC2 stops while Wi-Fi runs)
//   boot-strapping        : 0 (the BOOT button) 3 45 46: read at power-on, avoid; 45 must never be pulled high
//                           at power-on (it sets the flash voltage)
//   the RGB LED           : 48 (DevKitC-1 v1.0) or 38 (v1.1) drive the board's own WS2812 LED: avoided here
//   never                 : 19 20 (the native USB port), 43 44 (the serial port, UART0), 26-32 (flash and
//                           PSRAM inside the module), 33-37 (octal PSRAM on the N8R8 / N16R8 modules, the most
//                           common S3 devkits: refused on every S3, since the firmware cannot tell)
//
// What this map gives each firmware terminal:
//   OUT1-4   4 5 6 7          outputs, PWM (1 kHz), active high: HIGH = on
//   RLY1-2   15 16            outputs, on / off, active high (a relay module's IN)
//   CC1-2    17 18            outputs, on / off, active high
//   TRIG1-2  9 10             inputs, active low: a switch between the pin and GND (internal pull-up)
//   USER1-3  11 12 13         inputs, active low, internal pull-up
//   PIXEL    8                WS2812 data, 3.3 V, not inverted
//   SOUND    TX 40, RX 41     the DFPlayer's RX and TX
//   analog   1                a knob between 3V3 and GND, wiper here
//   S4, S5   47, 14           S4: the show script's DMX output (into a MAX485 module), S5: an input (no pull:
//                             wire your own resistor if you use it)
//   status   none             the board's LED is an RGB pixel, not a plain LED
//   I2C      none             the firmware does not use I2C
//   free     2 21 39 42       for your own additions
//
// ------------------------------------------------------------------------------------------------------------
// HOW TO CHANGE IT FOR YOUR OWN WIRING
//
// 1. Move a terminal: change its number in the table below, picking from "any use" (analog: 1-10). The build
//    stops if two terminals share a GPIO; the board refuses the flash, PSRAM, USB and UART0 pins when it starts.
// 2. Drop a terminal you do not want: write pt::NO_PIN instead of its number. The firmware then never touches
//    that GPIO, and the phone page does not offer the terminal.
// 3. An input wired the other way (to 3V3, active HIGH): set its bit in active_high (bit 0 TRIG1, bit 1 TRIG2,
//    bit 2-4 USER1-3). An input with its own resistor on your breakout: set its bit in external_pull.
// 4. Relay modules that switch ON when their IN pin is LOW ("low level trigger"): this firmware drives every
//    output HIGH for on, LOW for off, with no setting to turn that round. Use a "high level trigger" module (or
//    set its H/L jumper to H) or a MOSFET module. A low-level-trigger module as it is would switch on while the
//    board starts and whenever the firmware means "off".
// 5. Pixels: 3.3 V data, not inverted. Most WS2812 strips take it over a short wire (under about 1 m); a 74AHCT125
//    (non-inverting) level shifter to 5 V is the reliable fix. With an INVERTING shifter, set
//    PROPTROLLER_BOARD_PIXEL_INVERTED to true below.
// 6. A status LED of your own: put a plain LED with a 330 ohm resistor on a free GPIO (to GND) and write that GPIO
//    as the status LED.
//
// MAKE YOUR OWN BOARD FROM THIS ONE
//   * Copy this folder to bsp/<your-board>/; change PROPTROLLER_BOARD_ID (a-z, 0-9, '-', '_') and the name.
//   * In platformio.ini:   [env:my_board]
//                           extends = env:esp32s3_generic
//                           build_flags = ${env:esp32s3_generic.base_flags} -I bsp/my-board
//   * `pio run -e my_board -t upload`.
//
// OR CHANGE THE PINS WITHOUT BUILDING ANYTHING (a board package)
//   This map is firmware/board-packages/esp32-s3-devkit.json and is built into the image:
//   {"op":"get_board","preset":"esp32-s3-devkit"} gives it as a starting point; edit the pins, send it with
//   {"op":"set_board", ...} over USB or Wi-Fi and restart the board (firmware/BOARD-PACKAGES.md).
// ------------------------------------------------------------------------------------------------------------
#pragma once
#include "proptroller/pins.h"

#define PROPTROLLER_BOARD_ID "esp32-s3-devkit"
#define PROPTROLLER_BOARD_NAME "ESP32-S3 DevKit"
// false: the PIXEL data leaves the GPIO as it is (true only with an inverting level shifter)
#define PROPTROLLER_BOARD_PIXEL_INVERTED false

constexpr pt::PinMap PROPTROLLER_BOARD_MAP = {
    pt::Mcu::Esp32S3,            // ESP32-S3
    {4, 5, 6, 7},                // OUT1-4 (3.3 V logic: through a MOSFET or relay module)
    {15, 16},                    // RLY1-2 (a relay MODULE's IN pin, high level trigger)
    {17, 18},                    // CC1-2
    {9, 10},                     // TRIG1, TRIG2 (switch to GND)
    {11, 12, 13},                // USER1-3 (buttons to GND)
    8,                           // PIXEL data
    40, 41,                      // sound: TX (to the DFPlayer's RX), RX (from its TX)
    pt::NO_PIN, pt::NO_PIN,      // I2C SDA, SCL: not used
    1,                           // analog in (ADC1: GPIO 1-10)
    {47, 14},                    // expansion S4 (DMX out), S5 (input)
    pt::NO_PIN,                  // status LED: none (the board's LED is an RGB pixel)
    0x00,                        // active_high: none (every input is active low)
    0x00,                        // external_pull: none (the chip's own pull-ups)
};
