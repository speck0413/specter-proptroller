# Proptroller: your first project

The Proptroller library runs the Specter Proptroller board: four switched outputs (OUT1-OUT4, they can dim),
two relays (RLY1, RLY2), two isolated contacts (CONTACT1 / CONTACT2 on the silkscreen, CC1 / CC2 in scripts and
code), two trigger jacks (TRIG1, TRIG2), three buttons (USER1-USER3), a sound player and a pixel strip.

New board? Flash the stock firmware and wire your first prop with [GETTING-STARTED.md](../../../../GETTING-STARTED.md)
first: it needs no code at all.

## Start here: a show script (no code)

The easiest way needs no Arduino at all. Open the board's web page, type a show script in the text box,
press Check, then Save:

```
effect porch light:
  wait for trig1
  out1 = on
  audio_single = 1
  wait 30
  fade out1 to 0% over 2
```

Each effect waits for its input, runs its lines from top to bottom, then waits again. The board checks the
script when you save it and says what is wrong in plain words ("line 4: there is no out5 on this board ...").
The language is `firmware/SHOW-SCRIPT.md`: nine kinds of line, and the board's protections apply without being
written. A stored script runs with the generic firmware that this library builds (`prop.begin()`).

## In code: the same in a sketch

When you want code, you describe what should happen in plain words:

```cpp
#include <Proptroller.h>

Proptroller prop;

void setup() {
  prop.begin();
  prop.when(TRIG1).playSound(1).turnOn(OUT1, seconds(2)).then().turnOn(OUT2, seconds(5));
}

void loop() {
  prop.update();
}
```

"When TRIG1 is pressed, play sound 1 and switch OUT1 on for 2 seconds; then switch OUT2 on for 5 seconds."

`prop.begin()` and `prop.update()` are optional. Leave both out and the first `prop` call starts the board,
which then runs by itself in the background: `loop()` is yours, and a `delay()` there stops nothing. Write both
or neither. The examples write both; the workspace sketches leave both out (their `loop()` is free for your own
code). On the Pico family, `loop()` should call `delay()` (or `prop.update()`) now and then: that is when the
phone page and the serial port are served, so while `loop()` is busy without either they stop answering. The
show keeps running either way: triggers and outputs never wait for `loop()`.

Your own sensors and gadgets are plain Arduino code in `setup()` and `loop()`. The free pins are on the
expansion header: I2C on S1 / S2, an analog input on S3, spare GPIOs on S4 / S5 (the table at the top of
`src/proptroller/pins.h`).
That is the whole program. No timing code, no wiring of pins, no libraries to find.

## 1. Install the tools (once)

**PlatformIO in VS Code** (the standard way): follow the [developer quick start](../../../../README.md#developer-quick-start-vs-code),
then use `firmware/arduino/workspace-template/` for your own props (its `README.md`). To try an example, make a
project from it there: `python3 make_envs.py new my-ghost --board esp32dev --from 01_BlinkAnOutput` (boards:
`esp32dev`, `picow`, `pico2w`, `pico`).

**Arduino IDE** (best effort: the library builds there, but a Pico W / Pico 2 W built in the IDE has no
over-the-air update, and the IDE does not use the shared flash map unless you set it up by hand):

1. Install the Arduino IDE 2, add the board packages (File → Preferences → "Additional boards manager URLs":
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` and/or
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`), then
   install "esp32" or "Raspberry Pi Pico/RP2040" in the Boards Manager.
2. Copy this folder to `Documents/Arduino/libraries/Proptroller` (or add it zipped), then install its libraries
   with the Library Manager: **ESP32**: ArduinoJson, espMqttClient (by bertmelis) and Async TCP (by ESP32Async);
   **Pico family**: ArduinoJson and PubSubClient.
3. Tools → Board: "ESP32 Dev Module" or "Raspberry Pi Pico W" (Pico, Pico 2 W). For the ESP32 copy
   `firmware/partitions/esp32-4mb.csv` into the sketch folder as `partitions.csv`: without it the firmware does not
   fit the default app partition (1.25 MB; the examples are 1.30-1.31 MB without the build flags of the PlatformIO
   project), and the stored settings and updates would not match the stock firmware.
   For a Pico pick a Flash Size with a file system, so settings can be saved.

## 2. Your first effect

1. Put **01_BlinkAnOutput** in your project (PlatformIO: `python3 make_envs.py new blink --board esp32dev --from
   01_BlinkAnOutput` in the workspace; Arduino IDE: File → Examples → Proptroller). Connect a 12 V lamp to OUT1, power the board from 12 V (OUT1-OUT4 give the board's supply voltage), plug the USB cable into the
   ESP32 / Pico, press Upload (PlatformIO: the arrow in the status bar). The lamp blinks. A Pico W / Pico 2 W
   is not uploaded this way: its first flash is the merged first-flash file (the workspace README), and later
   updates go over Wi-Fi from the board's page (Firmware tab, the `.pio/build/<env>/firmware.bin` of your
   project).
2. Open the Serial Monitor (115200 baud; PlatformIO: the plug icon). The board says what it is doing, and problems come in plain
   words, for example: `Problem with when(TRIG1): That action does not fit that output (relays and contacts are only on/off ...`.
3. Work through the examples in order. Example 06 uses only the beginner calls (`Proptroller.h`); the others
   also use the advanced layer (`ProptrollerAdvanced.h`, `prop.advanced()`, see GOING-FURTHER.md).

| Example | You learn |
|---|---|
| 01_BlinkAnOutput | the shape of every sketch; outputs and time |
| 02_ButtonPlaysASound | triggers, sound files on the card, cooldown |
| 03_TimedSequence | steps at the same time, `then()`, `wait()`, relays, fading |
| 04_AmbientAndTrigger | a background look that a scene interrupts, pixels, priority |
| 05_TwoBoardsCascade | one board starting another (cable or radio) |
| 06_WiFiAndPhone | making shows on a phone, with no code |
| 07_Mqtt | home automation and your own MQTT topics |
| 08_CustomStateMachine | writing your own logic that runs every 10 ms |
| 09_PulseAndRecover | the built-in pulse-and-recover effect: hold to start, a pulse then a recovery, 3 runs per 3 minutes; names for terminals |

## 3. What the board does for you (safe by default)

* Every output is off from power-on until your sketch decides otherwise, and after any reset.
* A trigger that is already pressed (or stuck) when the board powers up never triggers; it has to be
  released first. Every trigger has to be released for 0.1 s before it can trigger again.
* The board does not measure or limit load current: choose each load within its terminal's rating
  (hardware/README.md). OUT1-OUT4 carry 5 A each on/off; any OUT can be dimmed, but only up to 2 A on a
  12 V supply (1 A on 24 V). RLY1 / RLY2 on a rev A board: 30 V DC at most, never mains.
* A held press never repeats, a press during the cooldown is ignored (never saved for later).
* A trigger closes the jack's tip to its sleeve (a button, a mat, a sensor with a relay output). A **powered
  3-wire sensor** that runs on 3.3 V and draws 15 mA or less (a small 3.3 V PIR such as the AM312; not the 5 V
  HC-SR501) takes its power from the ring and drives the tip high when it triggers: set the board's
  `trig1_input` / `trig2_input` to `sensor_high` (phone page: Settings, "TRIG1 input type"), or
  `prop.advanced().setting("trig1_input", "sensor_high")` in a sketch. In `sensor_high` an unplugged cable counts
  as a trigger: keep `switch` for anything safety-related.

## 4. Sound files

The sound player plays files by number: sound N is the file `0001-….mp3`, `0002-….mp3` … in the card's `mp3`
folder, and the same number means the same file everywhere:

| Sound | Played by |
|---|---|
| 1, 2, … 3000 | `playSound(1)`, `playSound(2)`, …; a show script's `audio_single = 1`, `audio_loop = 2`, … |
| `trigger_track` (1) | the default trigger sound, for a trigger whose effect has no sound of its own (Settings → Sound) |
| `ambient_track` (2) | the ambient loop between triggers (Settings → Sound); off while a script is stored |

The player numbers the files in the order they were written to the card, so let Studio's SD card tool fill it
(it erases the card, names the files and copies them in order); by hand, see `docs/SD-CARD.md` in the
repository ("Setting up the SD card for the DFPlayer"). The card: 32 GB or smaller, FAT32.

## 5. Next

* [GLOSSARY.md](GLOSSARY.md): PWM, debounce, relay, contact closure and the other words used here.
* [GOING-FURTHER.md](GOING-FURTHER.md): everything underneath is yours to use.
* The phone and desktop app protocol: `firmware/PROTOCOL.md`.
