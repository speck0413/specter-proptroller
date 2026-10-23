// Example board support package (BSP): a generic ESP32 board with four relay outputs on OUT1-4, a trigger input
// (active low, the board's own pull-up) and a button with a pull-down (active high) on TRIG2. It has no RLY/CC
// terminals, no USER buttons, no pixels and no sound port: those are NO_PIN and never touched.
// Use it: build_flags = -I path/to/this/folder (the Proptroller library picks up proptroller_board.h). Layout: pins.h
// PinMap.
#pragma once
#include "proptroller/pins.h"

constexpr pt::PinMap PROPTROLLER_BOARD_MAP = {
    pt::Mcu::Esp32DevKit,
    {23, 22, 21, 19},                     // OUT1-4
    {pt::NO_PIN, pt::NO_PIN},             // RLY1-2: none
    {pt::NO_PIN, pt::NO_PIN},             // CC1-2: none
    {36, 18},                             // TRIG1, TRIG2
    {pt::NO_PIN, pt::NO_PIN, pt::NO_PIN}, // USER1-3: none
    pt::NO_PIN,                           // pixel data: none
    pt::NO_PIN, pt::NO_PIN,               // sound port TX, RX: none
    pt::NO_PIN, pt::NO_PIN,               // I2C SDA, SCL: none
    39,                                   // analog in
    {pt::NO_PIN, pt::NO_PIN},             // expansion S4, S5: none
    2,                                    // status LED
    0x02,                                 // active high: TRIG2 (bit 1)
    0x03,                                 // the board's own resistors: TRIG1, TRIG2
};
