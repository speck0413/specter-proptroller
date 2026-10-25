// Board support package (BSP): an ESP32-C3 SuperMini (the thumb-sized C3 board with a USB-C port, BOOT and RESET
// buttons and 16 pins), on its own or on a breakout that brings its pins out to terminals.
// Built by `pio run -e esp32c3_supermini`; the whole story, with wiring tables: firmware/BARE-DEVKIT.md.
//
// READ THIS FIRST: every output here is a plain 3.3 V logic pin that gives a few milliamps. It cannot switch a
// lamp, a solenoid, a motor or a relay coil by itself. Put a driver between the pin and the load: a MOSFET module
// (for DC loads) or a relay module (for anything else). No Proptroller board takes a C3: nothing on this board
// is protected, inverted or rated by the Proptroller hardware.
//
// The C3 is a single-core chip with the USB port built in (no USB bridge): the USB-C port is the firmware's serial
// port, and the computer sees it as a "USB JTAG/serial debug unit" (no driver to install).
//
// ------------------------------------------------------------------------------------------------------------
// The SuperMini's pins (USB at the top, left row then right row):
//   left : 5V GND 3V3 4 3 2 1 0
//   right: 5 6 7 8 9 10 20 21
// That is 13 GPIOs, so this map leaves out what does not fit (see "Left out" below).
//
// Which GPIO is safe for what (every C3 GPIO can be an output; none is input-only):
//   any use               : 0 1 3 4 5 6 7 10
//   20 21                 : the chip's UART0: it prints its boot messages on 21 while it starts (an output there
//                           flickers at power-on). Fine for the DFPlayer, which ignores them
//   analog (with Wi-Fi on): 0-4 only (ADC1)
//   boot-strapping        : 2 8 9: read at power-on. 9 is the BOOT button (held at power-on = flashing mode);
//                           8 carries the blue LED on most SuperMinis (lit when the pin is LOW) and must be high
//                           at power-on; 2 is used here for a button with the internal pull-up (do not hold it
//                           while the board powers up)
//   never                 : 18 19 (the USB port), 11-17 (the flash memory; not on the pins)
//
// What this map gives each firmware terminal:
//   OUT1-4   5 6 7 10         outputs, PWM (1 kHz), active high: HIGH = on
//   TRIG1-2  3 4              inputs, active low: a switch between the pin and GND (internal pull-up)
//   USER1    9                the board's BOOT button (active low): a test button with nothing wired
//   USER2    2                input, active low, internal pull-up
//   PIXEL    1                WS2812 data, 3.3 V, not inverted
//   SOUND    TX 21, RX 20     the DFPlayer's RX and TX
//   analog   0                a knob between 3V3 and GND, wiper here
// Left out (not enough pins, or not on a C3): RLY1-2, CC1-2, USER3, the expansion port S4 / S5 (the C3 has two
// UARTs: the USB console needs none, the sound port takes UART1, so there is none left for DMX), I2C (unused),
// the status LED (GPIO8's LED is on a strapping pin and lit by a LOW level, the other way round from what the
// firmware drives).
//
// ------------------------------------------------------------------------------------------------------------
// HOW TO CHANGE IT FOR YOUR OWN WIRING
//
// 1. Move a terminal: change its number in the table below, picking from "any use" (analog: 0-4). The build stops
//    if two terminals share a GPIO; the board refuses the flash and USB pins when it starts.
// 2. Drop a terminal you do not want: write pt::NO_PIN instead of its number. That frees its GPIO for another
//    terminal: for example drop the sound port (TX, RX = pt::NO_PIN) and use 20 and 21 for RLY1-2 (they
//    flicker at power-on: fine for a relay that you do not mind clicking once, not for a valve). The DMX port
//    (expansion S4) needs the sound port's UART: give S4 a GPIO only with the sound port left out (the build and
//    the board refuse both at once).
// 3. An input wired the other way (to 3V3, active HIGH): set its bit in active_high (bit 0 TRIG1, bit 1 TRIG2,
//    bit 2-4 USER1-3). An input with its own resistor on your breakout: set its bit in external_pull.
// 4. Relay modules that switch ON when their IN pin is LOW ("low level trigger"): this firmware drives every
//    output HIGH for on, LOW for off, with no setting to turn that round. Use a "high level trigger" module (or
//    set its H/L jumper to H) or a MOSFET module. A low-level-trigger module as it is would switch on while the
//    board starts and whenever the firmware means "off".
// 5. Pixels: 3.3 V data, not inverted. Most WS2812 strips take it over a short wire (under about 1 m); a 74AHCT125
//    (non-inverting) level shifter to 5 V is the reliable fix. With an INVERTING shifter, set
//    PROPTROLLER_BOARD_PIXEL_INVERTED to true below.
//
// MAKE YOUR OWN BOARD FROM THIS ONE
//   * Copy this folder to bsp/<your-board>/; change PROPTROLLER_BOARD_ID (a-z, 0-9, '-', '_') and the name.
//   * In platformio.ini:   [env:my_board]
//                           extends = env:esp32c3_supermini
//                           build_flags = ${env:esp32c3_supermini.base_flags} -I bsp/my-board
//   * `pio run -e my_board -t upload` (hold BOOT while plugging in if the port does not show up).
//
// OR CHANGE THE PINS WITHOUT BUILDING ANYTHING (a board package)
//   This map is firmware/board-packages/esp32-c3-supermini.json and is built into the image:
//   {"op":"get_board","preset":"esp32-c3-supermini"} gives it as a starting point; edit the pins, send it with
//   {"op":"set_board", ...} over USB or Wi-Fi and restart the board (firmware/BOARD-PACKAGES.md).
// ------------------------------------------------------------------------------------------------------------
#pragma once
#include "proptroller/pins.h"

#define PROPTROLLER_BOARD_ID "esp32-c3-supermini"
#define PROPTROLLER_BOARD_NAME "ESP32-C3 SuperMini"
// false: the PIXEL data leaves the GPIO as it is (true only with an inverting level shifter)
#define PROPTROLLER_BOARD_PIXEL_INVERTED false

constexpr pt::PinMap PROPTROLLER_BOARD_MAP = {
    pt::Mcu::Esp32C3,                     // ESP32-C3
    {5, 6, 7, 10},                        // OUT1-4 (3.3 V logic: through a MOSFET or relay module)
    {pt::NO_PIN, pt::NO_PIN},             // RLY1-2: left out (no pins)
    {pt::NO_PIN, pt::NO_PIN},             // CC1-2: left out (no pins)
    {3, 4},                               // TRIG1, TRIG2 (switch to GND)
    {9, 2, pt::NO_PIN},                   // USER1 = the BOOT button, USER2 (button to GND), USER3: left out
    1,                                    // PIXEL data
    21, 20,                               // sound: TX (to the DFPlayer's RX), RX (from its TX)
    pt::NO_PIN, pt::NO_PIN,               // I2C SDA, SCL: not used
    0,                                    // analog in (ADC1: GPIO 0-4)
    {pt::NO_PIN, pt::NO_PIN},             // expansion S4, S5: left out (no UART for DMX)
    pt::NO_PIN,                           // status LED: none (see above)
    0x00,                                 // active_high: none (every input is active low)
    0x00,                                 // external_pull: none (the chip's own pull-ups)
};
