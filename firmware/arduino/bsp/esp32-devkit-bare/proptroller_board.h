// Board support package (BSP): a bare ESP32 30-pin DevKit (ESP32-WROOM-32, the module the Proptroller board takes),
// on its own or on a breakout / expansion board that brings every pin out to screw terminals or headers.
// Built by `pio run -e esp32dev_generic`; the whole story, with wiring tables: firmware/BARE-DEVKIT.md.
//
// READ THIS FIRST: every output here is a plain 3.3 V logic pin that gives a few milliamps. It cannot switch a
// lamp, a solenoid, a motor or a relay coil by itself. Put a driver between the pin and the load: a MOSFET module
// (for DC loads) or a relay module (for anything else). The Proptroller board's drivers, ratings and inversions do
// not exist on a bare devkit.
//
// ------------------------------------------------------------------------------------------------------------
// The DevKit's pins (30-pin board, USB at the bottom, left row then right row):
//   left : EN  36(VP)  39(VN)  34  35  32  33  25  26  27  14  12  13  GND  VIN
//   right: 23  22  1(TX0)  3(RX0)  21  19  18  5  17  16  4  2  15  GND  3V3
//
// Which GPIO is safe for what:
//   any use (output or input) : 4 13 14 16 17 18 19 21 22 23 25 26 27 32 33
//   input only, NO pull-up    : 34 35 36 39 (these four cannot drive anything and have no internal resistor:
//                               a button there needs your own 10k resistor to 3V3, then "pull": "board")
//   boot-strapping (careful)  : 0 2 5 12 15 (read at power-on; 12 must never be pulled high at power-on: it
//                               sets the flash voltage). Inputs with the internal pull-up are fine on 5; the
//                               blue LED on 2 is fine; avoid outputs on all of them
//   never                     : 1 3 (the USB serial port), 6-11 (the module's flash memory; not on the pins)
//   PSRAM modules (WROVER)    : 16 and 17 belong to the PSRAM: a WROVER devkit cannot use them
//
// What this map gives each firmware terminal (the same GPIOs as the Proptroller board for every OUTPUT, so this
// image and the Proptroller one never drive a pin the other board uses as an input):
//   OUT1-4   23 22 21 19      outputs, PWM (1 kHz), active high: HIGH = on
//   RLY1-2   25 26            outputs, on / off, active high (a relay module's IN)
//   CC1-2    27 32            outputs, on / off, active high
//   TRIG1-2  13 18            inputs, active low: connect a switch between the pin and GND (internal pull-up)
//   USER1-3  14 4 5           inputs, active low, internal pull-up (5 is a strapping pin: do not hold that
//                             button while the board powers up)
//   PIXEL    33               WS2812 data, 3.3 V, not inverted (see "Pixels" below)
//   SOUND    TX 17, RX 16     the DFPlayer's RX and TX (a 1k resistor in the TX line is the usual advice)
//   analog   39               a knob (potentiometer) between 3V3 and GND, wiper here
//   S4, S5   15, 35           S4: the show script's DMX output (into a MAX485 module), S5: an input (input-only
//                             pin: wire your own pull resistor if you use it)
//   status   2                the DevKit's blue LED
//   I2C      none             the firmware does not use I2C
//
// ------------------------------------------------------------------------------------------------------------
// HOW TO CHANGE IT FOR YOUR OWN WIRING
//
// 1. Move a terminal: change its number in the table below. Pick from "any use" above for an output, from "any
//    use" or "input only" for an input. The build stops with a message if two terminals share a GPIO or an
//    output sits on an input-only pin; the board refuses the flash, USB and PSRAM pins when it starts.
// 2. Drop a terminal you do not want: write pt::NO_PIN instead of its number. The firmware then never touches
//    that GPIO, and the phone page does not offer the terminal.
// 3. An input wired the other way (a button to 3V3 with a pull-down, a sensor that drives HIGH when active): set
//    its bit in active_high (bit 0 TRIG1, bit 1 TRIG2, bit 2-4 USER1-3). An input that has its own resistor on
//    your breakout (or on 34-39, which need one): set its bit in external_pull.
// 4. Relay modules that switch ON when their IN pin is LOW ("low level trigger", very common): this firmware
//    drives every output HIGH for on and LOW for off, and it has no setting to turn that round. Use a module
//    with a "high level trigger" (many have an H/L jumper: set it to H), or a MOSFET module. Never use a
//    low-level-trigger module as it is: it would switch on while the ESP32 starts and whenever the firmware
//    means "off".
// 5. Pixels: a bare devkit sends 3.3 V data, not inverted. Most WS2812 strips take it over a short wire (under
//    about 1 m); a 74AHCT125 (non-inverting) level shifter to 5 V is the reliable fix. If you add an INVERTING
//    shifter (one transistor, a 74HC14), set PROPTROLLER_BOARD_PIXEL_INVERTED to true below.
//
// MAKE YOUR OWN BOARD FROM THIS ONE
//   * Copy this folder to bsp/<your-board>/ and change PROPTROLLER_BOARD_ID (a-z, 0-9, '-', '_'; it is shown as
//     info.board and in Specter Studio) and PROPTROLLER_BOARD_NAME.
//   * In platformio.ini add an env:   [env:my_board]
//                                      extends = env:esp32dev_generic
//                                      build_flags = ${env:esp32dev.build_flags} -I bsp/my-board
//   * `pio run -e my_board -t upload` (or make release files: docs/FIRMWARE-BUILD.md).
//
// OR CHANGE THE PINS WITHOUT BUILDING ANYTHING (a board package)
//   The stock firmware reads a board package: JSON with the same pins, sent to the board and kept in its flash
//   (firmware/BOARD-PACKAGES.md). This map is firmware/board-packages/esp32-devkit-bare.json; on this image it is
//   also built in: {"op":"get_board","preset":"esp32-devkit-bare"} gives it to you as a starting point. Edit the
//   pins and send it with {"op":"set_board", ...} over USB or Wi-Fi, then restart the board. The stock ESP32
//   image (the Proptroller board's) takes the same file, so a bare devkit can run that image too. A board package
//   can move, drop and re-wire inputs and set the pixel inversion; it cannot make an output active low either.
// ------------------------------------------------------------------------------------------------------------
#pragma once
#include "proptroller/pins.h"

#define PROPTROLLER_BOARD_ID "esp32-devkit-bare"
#define PROPTROLLER_BOARD_NAME "ESP32 DevKit, bare (no Proptroller board)"
// false: the PIXEL data leaves the GPIO as it is (true only with an inverting level shifter)
#define PROPTROLLER_BOARD_PIXEL_INVERTED false

constexpr pt::PinMap PROPTROLLER_BOARD_MAP = {
    pt::Mcu::Esp32DevKit,        // the classic ESP32
    {23, 22, 21, 19},            // OUT1-4 (3.3 V logic: through a MOSFET or relay module)
    {25, 26},                    // RLY1-2 (a relay MODULE's IN pin, high level trigger)
    {27, 32},                    // CC1-2
    {13, 18},                    // TRIG1, TRIG2 (switch to GND)
    {14, 4, 5},                  // USER1-3 (buttons to GND)
    33,                          // PIXEL data
    17, 16,                      // sound: TX (to the DFPlayer's RX), RX (from its TX)
    pt::NO_PIN, pt::NO_PIN,      // I2C SDA, SCL: not used
    39,                          // analog in (ADC1: 32-39 only, the others stop while Wi-Fi runs)
    {15, 35},                    // expansion S4 (DMX out), S5 (input)
    2,                           // status LED (the DevKit's blue LED)
    0x00,                        // active_high: none (every input is active low)
    0x00,                        // external_pull: none (the chip's own pull-ups)
};
