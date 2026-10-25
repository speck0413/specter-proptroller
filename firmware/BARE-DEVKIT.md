# Run it on a bare devkit

No Proptroller board? The same firmware runs on a plain ESP32 development board, on its own or sitting on a
generic breakout ("expansion") board that brings every pin out to screw terminals or headers. You get the phone
page, Wi-Fi, effect scripts, shows, sound, pixels and updates over the air, on three boards:

| Board | Firmware files (in each release) | Build it yourself |
|---|---|---|
| ESP32 30-pin DevKit (ESP32-WROOM-32, the module the Proptroller board takes) | `first-flash-esp32-devkit-bare-<v>.bin`, `generic-esp32-devkit-bare-<v>.bin` | `pio run -e esp32dev_generic` |
| ESP32-S3 DevKit (ESP32-S3-DevKitC-1 and boards laid out like it) | `first-flash-esp32-s3-devkit-<v>.bin`, `generic-esp32-s3-devkit-<v>.bin` | `pio run -e esp32s3_generic` |
| ESP32-C3 SuperMini | `first-flash-esp32-c3-supermini-<v>.bin`, `generic-esp32-c3-supermini-<v>.bin` | `pio run -e esp32c3_supermini` |

Status: these images are built and tested on the computer (the pin rules, the release files, the firmware in the
simulator, the S3 image in an emulator); none has run on a real devkit yet.

## First: the outputs need a driver

> **A bare devkit's outputs are 3.3 V logic pins that give a few milliamps.** They cannot switch a lamp, a
> solenoid, a motor, a fan or a relay coil by themselves, and wiring one straight to a pin can destroy the chip.
> Put a driver between each output and its load:
>
> * a **MOSFET module** (a logic-level MOSFET board with a 3.3 V input) for DC loads: lamps, LED strips, solenoids,
>   fans, with a flyback diode across anything that has a coil;
> * a **relay module** for anything else, with a **high level trigger** (many have an H/L jumper: set it to H).
>   A "low level trigger" module switches ON when its input is LOW: with this firmware it would switch on while the
>   board starts and whenever the firmware means "off". The firmware has no setting to turn an output round.
>
> Nothing on a bare devkit is protected or rated: the Proptroller board's ratings, TVS protection, output
> drivers and LEDs do not exist here. Power the loads from their own supply and join only its ground to the devkit's.

The other differences from the Proptroller board:

* **Inputs** (TRIG1-2, USER1-3) use the chip's own pull-up resistors: wire a button, switch or step mat between the
  pin and **GND**. A powered sensor must give 3.3 V at most (never 5 V or 12 V) and pull the pin to GND when it
  triggers (an "NPN" or open-collector output).
* **PIXEL** sends 3.3 V data, not inverted (the Proptroller board's level shifter inverts it; a bare devkit has
  none). Most WS2812 strips take 3.3 V data over a short wire (under about 1 m); a 74AHCT125 level shifter to 5 V
  is the reliable fix. Power the strip from its own 5 V supply, not the devkit's pin.
* **Sound**: the DFPlayer Mini is wired by hand: its RX to the devkit's sound TX (with a 1 kΩ resistor), its TX to
  the sound RX, 5 V and GND, and a speaker on its SPK1 / SPK2 pins (3 W at most; never ground a speaker wire).
* **Analog** (the knob): a 10 kΩ potentiometer between 3V3 and GND, its wiper on the analog pin.

## Wiring

Each table is what the firmware calls the terminal, the GPIO it is on, and what to wire there. "GPIO" is the
number printed next to the pin on the board (on some boards with a letter, such as `D23` or `IO23`).

### ESP32 30-pin DevKit

The outputs are on the same GPIOs as on the Proptroller board; the inputs moved to GPIOs that have pull-ups.

| Terminal | GPIO | Wire |
|---|---|---|
| OUT1-4 | 23, 22, 21, 19 | a MOSFET or relay module's input (dimming and fades through a MOSFET module only) |
| RLY1-2 | 25, 26 | a relay module's input (on / off) |
| CC1-2 | 27, 32 | another device's input, through an optocoupler module (on / off) |
| TRIG1, TRIG2 | 13, 18 | a switch to GND |
| USER1-3 | 14, 4, 5 | buttons to GND (GPIO5 is read at power-on: do not hold that button while the board starts) |
| PIXEL | 33 | the strip's DIN (3.3 V data) |
| Sound TX, RX | 17, 16 | the DFPlayer's RX (through 1 kΩ), its TX |
| Analog | 39 | a potentiometer's wiper |
| S4, S5 | 15, 35 | S4: DMX out into a MAX485 module (SHOW-SCRIPT.md section 7); S5: an input (GPIO35 has no pull-up: add a 10 kΩ resistor) |
| Status LED | 2 | the DevKit's own blue LED |

Free for your own use: none that are safe for outputs; 34 and 36 are input-only (no pull-up), 0 and 12 are
strapping pins. Never use 1 and 3 (the USB serial port) or 6-11 (the module's flash).

### ESP32-S3 DevKit

Plug the USB cable into the port marked **UART** (or COM): that is the firmware's serial port (Specter Studio, the
console). The other port (USB) is the chip's own and is not used by the firmware.

| Terminal | GPIO | Wire |
|---|---|---|
| OUT1-4 | 4, 5, 6, 7 | a MOSFET or relay module's input (dimming and fades through a MOSFET module only) |
| RLY1-2 | 15, 16 | a relay module's input |
| CC1-2 | 17, 18 | an optocoupler module's input |
| TRIG1, TRIG2 | 9, 10 | a switch to GND |
| USER1-3 | 11, 12, 13 | buttons to GND |
| PIXEL | 8 | the strip's DIN |
| Sound TX, RX | 40, 41 | the DFPlayer's RX (through 1 kΩ), its TX |
| Analog | 1 | a potentiometer's wiper |
| S4, S5 | 47, 14 | S4: DMX out into a MAX485 module; S5: an input (add your own pull resistor) |
| Status LED | none | the board's LED is an RGB pixel (on GPIO48 or 38), not a plain LED |

Free for your own use: 2, 21, 39, 42. Never use 19 and 20 (USB), 43 and 44 (the serial port), 26-37 (flash and
PSRAM; 35-37 belong to the PSRAM on the common N8R8 / N16R8 modules) or 0, 3, 45, 46 (strapping pins).

### ESP32-C3 SuperMini

The C3 has 13 GPIOs on its pins, so some terminals are left out. Its USB-C port is the chip's own: the computer
sees a "USB JTAG/serial debug unit" (no driver to install).

| Terminal | GPIO | Wire |
|---|---|---|
| OUT1-4 | 5, 6, 7, 10 | a MOSFET or relay module's input (dimming and fades through a MOSFET module only) |
| TRIG1, TRIG2 | 3, 4 | a switch to GND |
| USER1 | 9 | the board's **BOOT** button: a test button with nothing wired |
| USER2 | 2 | a button to GND (GPIO2 is read at power-on: do not hold it while the board starts) |
| PIXEL | 1 | the strip's DIN |
| Sound TX, RX | 21, 20 | the DFPlayer's RX (through 1 kΩ), its TX |
| Analog | 0 | a potentiometer's wiper |

Left out: RLY1-2, CC1-2, USER3, the expansion port S4 / S5 (the C3 has two serial ports; the sound player takes the
one the USB console leaves, so there is none for DMX), the status LED (GPIO8's LED is on a strapping pin and lit by a
LOW level). Never use 18 and 19 (USB). GPIO8 is free but read at power-on (it must be high then).

## Put the firmware on it

* **From the browser:** the [browser flasher](https://speck0413.github.io/specter-proptroller/), the button
  **For a bare devkit**: it finds the chip and writes the right image. A C3 SuperMini that does not show up: hold
  BOOT while you plug it in, then let go.
* **With esptool:** write the first-flash file at address 0x0, naming the chip:

  ```sh
  esptool.py --chip esp32   write_flash 0x0 first-flash-esp32-devkit-bare-<v>.bin
  esptool.py --chip esp32s3 write_flash 0x0 first-flash-esp32-s3-devkit-<v>.bin
  esptool.py --chip esp32c3 write_flash 0x0 first-flash-esp32-c3-supermini-<v>.bin
  ```

* **From source:** `pio run -e esp32dev_generic -t upload` (or `esp32s3_generic`, `esp32c3_supermini`).

Then the board works like any other (GETTING-STARTED.md from section 5): its Wi-Fi `Proptroller-XXXX`, the phone
page, Specter Studio. Updates go over Wi-Fi with the `generic-<board>-<v>.bin` file of the same board.

Flash settings: every bare image is built for **4 MB of flash** (the shared flash map, FLASH-MAP.md), so it runs on
a board with more (the rest stays unused). The S3 and C3 images run the flash at DIO 80 MHz, which every quad
flash chip on these boards supports; the classic ESP32 image is the Proptroller one's QIO 80 MHz. S3 modules with
**octal flash** (the rare N16R16V / N32R8V) are not supported.

## Make it fit your wiring

Two ways, from the quickest:

1. **A board package, no building.** The pin map is data the board keeps in its flash
   ([BOARD-PACKAGES.md](BOARD-PACKAGES.md)). Each bare board's map is built in and in
   [`board-packages/`](board-packages/) (`esp32-devkit-bare.json`, `esp32-s3-devkit.json`,
   `esp32-c3-supermini.json`). Take it as a starting point (`{"op":"get_board","preset":"esp32-c3-supermini"}`),
   change the pins, send it with `set_board` over USB or Wi-Fi and restart the board. A package can move a
   terminal, leave one out, turn an input round (`"active": "high"` for a sensor that drives HIGH), say a resistor
   is on the board (`"pull": "board"`) and set the pixel inversion. The board checks every pin against its chip
   and says in plain words what is wrong (a flash pin, a USB pin, a pin used twice). The stock ESP32 image (the
   Proptroller one) takes `esp32-devkit-bare.json` too, so a bare ESP32 DevKit can run either image.
2. **A board support package (BSP), built in.** Each image above is the firmware with one header compiled in:
   `firmware/arduino/bsp/<board>/proptroller_board.h`. Every line of it is commented: which pins are safe for
   what on that chip, how to move or drop a terminal, how to mark an input active high, how to invert the pixel
   data. Copy the folder, change the pins and the id, add an env to `platformio.ini` that extends the board's env
   with `-I bsp/<your-board>`, and build (the header's comments have the lines to paste). The build refuses two
   terminals on one GPIO and an output on an input-only pin; the board refuses flash, PSRAM and USB pins when it
   starts.

Which pins are safe, per chip:

| | Outputs and inputs | Inputs only | Read at power-on (careful) | Never |
|---|---|---|---|---|
| ESP32 | 4, 13, 14, 16-19, 21-23, 25-27, 32, 33 | 34-39 (no pull-up) | 0, 2, 5, 12, 15 (12: never pulled high at power-on) | 1, 3 (USB serial), 6-11 (flash), 16-17 on a WROVER (PSRAM) |
| ESP32-S3 | 1, 2, 4-18, 21, 39-42, 47 (38 / 48: the RGB LED) | none | 0, 3, 45, 46 (45: never pulled high at power-on) | 19, 20 (USB), 43, 44 (serial), 26-37 (flash / PSRAM) |
| ESP32-C3 | 0, 1, 3-7, 10, 20, 21 (21 flickers at power-on) | none | 2, 8, 9 | 11-17 (flash), 18, 19 (USB) |

Analog with Wi-Fi on: ESP32 32-39, S3 1-10, C3 0-4.

## Timing

The 1 ms input sampling and the 10 ms step work the same on every chip: a hardware timer's interrupt latches every
input pin with its time into a ring, every millisecond, and the step task takes the samples in order, so a press is
seen at the millisecond it happened even when the step runs late. On the dual-core ESP32 and S3 the step and the
interrupt are on core 1 and the network, the pixels and Wi-Fi on core 0. The C3 has one core: the step runs there
at a higher priority than the web server, MQTT, the console and the pixel strip, below Wi-Fi's own driver tasks; the
ring (64 samples) covers the step being held off for up to 64 ms. The console's `mem` line reports the step's
timing (`step max`, `sample handled max ... after it was latched`, `lost`) on any of them.

What is measured: the ESP32 DevKit on the bench (README "Step timing": step max 117-219 us, average 80-110 us);
the S3 image in QEMU without Wi-Fi (arduino/QEMU.md: step max 340-575 us, average about 110 us, samples handled at
most 1.25 ms after their latch, none lost). Not measured: anything on a real S3 or C3, and the C3 at all (no
emulator for it here). The C3 runs at 160 MHz with its code read from flash at DIO, so expect its step to take
roughly one and a half to two times the ESP32's, inside the 1 ms sampling period (an estimate). Wi-Fi's own
tasks run above the step on that one core and can hold it off for a while (how long is unmeasured); the ring keeps
every sample meanwhile. Check it with `mem` on the board.
