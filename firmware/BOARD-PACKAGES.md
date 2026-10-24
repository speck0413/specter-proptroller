# Board packages

One generic firmware image per chip (ESP32 DevKit, Pico, Pico W, Pico 2 W) runs any board built around it. A
**board package** says which GPIO each terminal is on, how each input is wired, whether the pixel data is
inverted, the board's id and name and, if you want, the board's defaults (settings, an alias profile, a show
script). It is JSON, sent to the board (`set_board`, [PROTOCOL.md](PROTOCOL.md)) and kept on it in its own record
([FLASH-MAP.md](FLASH-MAP.md)). Nothing is compiled.

The examples are in [`board-packages/`](board-packages/): the Proptroller board on the ESP32 DevKit and on the
Pico family, and `relay4-button` (four relays and an active-high button). The code is
`firmware/arduino/lib/proptroller/src/proptroller/board_package.h`; `test/test_board_package` tests every rule on
this page.

## The format

```json
{
  "board_package": 1,
  "id": "relay4-button",
  "name": "Four relays and a button (example)",
  "mcu": "esp32-devkit",
  "terminals": {
    "OUT1": {"pin": 23}, "OUT2": {"pin": 22}, "OUT3": {"pin": 21}, "OUT4": {"pin": 19},
    "TRIG1": {"pin": 36, "active": "low", "pull": "board"},
    "TRIG2": {"pin": 18, "active": "high", "pull": "board"}
  },
  "analog": 39,
  "status_led": 2
}
```

| Field | What |
|---|---|
| `board_package` | the format: `1` (a newer number is refused: "update the firmware first") |
| `id` | 1-32 characters of `a-z`, `0-9`, `-`, `_` (not `none`); `info.board`, mDNS TXT `board`, backups and release lists carry it |
| `name` | 1-60 characters, shown by Specter Studio |
| `mcu` | the chip socket: `esp32-devkit`, or `pico` (one package fits the Pico, Pico W and Pico 2 W) |
| `terminals` | `OUT1`-`OUT4`, `RLY1`-`RLY2`, `CC1`-`CC2` (outputs: `pin`), `TRIG1`-`TRIG2`, `USER1`-`USER3` (inputs: `pin`, `active`, `pull`) |
| `active` | inputs: `low` (pressed pulls the pin to ground; the default) or `high` |
| `pull` | inputs: `chip` (the chip's own pull-up for an input active low, pull-down for one active high; the default) or `board` (the board has its own resistor) |
| `pixel` | `{"pin": 33, "inverted": true}`: `inverted` (default `true`) when the board's level shifter inverts the data, as on the Proptroller board |
| `sound` | the DFPlayer port, `{"tx": 17, "rx": 34}` (both) |
| `i2c` | `{"sda": 4, "scl": 16}` (both) |
| `analog` | the analog input (the knob on expansion S3) |
| `expansion` | `{"S4": 15, "S5": 35}`: S4 is the DMX port's transmit line, S5 an input |
| `status_led` | a GPIO, or `"builtin"`: the MCU board's own LED (on the Pico W it sits on the radio chip) |
| `defaults` | optional: `{"settings": {"name": "Lamp"}, "profile": {...}, "script": "...", "script_name": "Lamp show"}`, see below |
| `description`, `$schema` | optional, ignored |

A terminal or function **left out does not exist** on that board: it is never configured, driven or read, and the
phone page and `info` do not offer it. Every other field is an error ("unknown field"), so a misspelt name is
caught rather than ignored.

`"base": "proptroller"` (or another built-in board's id) takes that built-in board's pins for the chip the board
has, with no pin fields beside it: a package that is "the Proptroller board with my defaults".

```json
{"board_package": 1, "id": "lamp-prop", "name": "Lamp prop", "base": "proptroller",
 "defaults": {"settings": {"name": "Lamp"}, "script": "effect lamp:\n  wait for trig1\n  out1 = on\n  wait 2\n  out1 = off\n"}}
```

At most 16 KB as sent (`BOARD_PACKAGE_MAX_BYTES`); stored packed, at most 4,048 bytes (`BOARD_STORE_CAPACITY`, one
4 KB flash block). A package with a 30-alias profile and a 2 KB script packs to about 2 KB; one that does not fit is
refused in plain words ("send the script with set_script instead").

## The pin rules

The board checks every package for its own chip and names the field and the reason:
`OUT3: GPIO 35 is input-only on the ESP32 and cannot drive an output`.

| Chip | Refused | Warned (accepted) |
|---|---|---|
| ESP32 | GPIOs that do not exist (20, 24, 28-31, above 39); 6-11 (the module's flash); 1 and 3 (the USB serial port); 16 and 17 on a module with PSRAM; 34-39 for anything that drives (outputs, pixel, sound TX, I2C, S4, the status LED); an input on 34-39 without `"pull": "board"` (no pull resistor of its own); 12 for anything that drives (it sets the flash voltage at power-on); `analog` other than 32-39 (ADC2 stops while Wi-Fi runs) | the boot-strapping pins 0, 2, 5, 12 (inputs), 15: what is wired there must not pull them the wrong way while the board starts |
| Pico W, Pico 2 W | above GP29; GP23, 24, 25, 29 (the radio's) | |
| Pico | above GP29; GP23, 24, 29 (the Pico board's own); GP25 for anything but `status_led` (the Pico's LED) | |
| every chip | one GPIO used twice ("GPIO 23 is used twice: OUT1 and RLY1"); `analog` on a pin without an ADC (Pico: GP26-28) | |

Every built-in board passes these rules (`every_built_in_board_passes_the_pin_rules`).

## Power-on

1. The flash file system is mounted. No pin has been touched: every GPIO is in its reset state, an input.
2. The board package is read:
   * **nothing stored**: the image's own board. On the stock images that is the Proptroller board (an image built
     with a board support package compiled in, `proptroller_board.h`, has that board instead). Every board that
     ran an earlier firmware has nothing stored and keeps working as it did.
   * **a stored package that checks**: that package.
   * **a stored package that cannot be used** (damaged, from a newer firmware, or not for this chip): **no pins at
     all**. Nothing is driven, no terminal exists, `info.board` is `none`, and `state.board_problem` says why in
     plain words. Wi-Fi, the USB serial port and the protocol work, so a good package can be sent (or the stored
     one forgotten). It counts as an unreadable record: a new image that cannot use the package fails its
     self-check and goes back to the previous one.
3. The package's outputs are driven low (the pixel line to its idle level) and its inputs configured. A pin the
   package does not use is never touched.

On the Proptroller board the outputs stay off during step 1 because every output stage has a gate pull-down. The
mount takes tens of milliseconds (a few hundred on a new board, whose file system is formatted first).

The 1 ms trigger sampling reads each input's pin and polarity from a table made once at power-on: a stored package
costs nothing in the sampling (`sampling_reads_each_input_once_per_millisecond_on_any_board`).

## Sending one

`set_board` ([PROTOCOL.md](PROTOCOL.md)) checks the package, stores it and answers `restart_needed`: the board runs
on the board it started with until it restarts, so a person confirms the change and then sends `reboot`.
`get_board` shows what runs, what is stored (and whether a restart is still to apply it) and the built-in boards;
`get_board` with `preset` gives a built-in board as a package to start from.

* Over the USB serial port and Wi-Fi (the board's page, Specter Studio). Not over MQTT: a pin map is not for a
  whole fleet at once.
* While the page lock is on, Wi-Fi needs the page password; the USB serial port is local access.
* Refused during a firmware update and while a new image waits for its self-check.
* `{"op":"set_board","preset":"relay4-button"}` stores a built-in board by id; `{"op":"set_board","forget":true}`
  removes the stored package (the next restart runs the image's own board).

A first install over USB can also write the board package straight into the file system image it flashes
([FLASH-MAP.md](FLASH-MAP.md) "A board package in the first flash"): the board then starts on its own pins from
the first second.

## Defaults

A package's `defaults` are what a fresh board starts with, applied in this order: the alias `profile`
(`set_profile`'s data), the `settings` (`key: value`, a profile's key aliases work), the show `script`. They are
applied:

* on a fresh board (nothing stored, a factory reset);
* once at the next power-on after `set_board` with `"defaults": "apply"`, on top of the board's own settings (the
  Wi-Fi is kept).

The board keeps them like anything set by hand: a change made later is not undone at the next power-on. A value the
board refuses is logged in plain words and the rest still applies. A stored package's defaults replace the image's
compiled-in ones; a stored package without defaults leaves a fresh board on the firmware's own defaults.

`script_name` (optional, 1-60 characters) is what the default script is called, for a client to name it
("comes with its own script, Lamp show"). `get_defaults` reads the running board's defaults and `apply_defaults`
applies them now, all or some (`"only": ["script", "profile"]`), with no restart and no factory reset
([PROTOCOL.md](PROTOCOL.md)). An image with its defaults compiled in names its script with
`PROPTROLLER_BOARD_SCRIPT_NAME`.

## Factory reset

`factory_reset` clears the settings (with the Wi-Fi), shows and the show script and **keeps the board package**:
the board comes back on its own pins, with its package's defaults. `{"op":"factory_reset","forget_board":true}`
also removes the package. `{"op":"factory_reset","keep_wifi":true}` keeps the name, the Wi-Fi network and its
password and the setup hotspot's password (put back after the package's defaults, so a package's own name does not
replace it): the board comes back on its network, reachable where it was.

A factory reset of a file system that could not be opened formats it, and the board package goes with it: the board
then starts on the image's own board until a package is sent again.

## Built-in boards

Each image carries its default board and the examples for its chip, selectable by id (`set_board` `preset`,
`get_board` `presets`):

| Id | Chip | What |
|---|---|---|
| `proptroller` | every image | the Spectre Proptroller board (its ESP32 or Pico map; the status LED on the Pico W's radio) |
| `relay4-button` | ESP32 | four relays on OUT1-4, a trigger on TRIG1, an active-high button on TRIG2, nothing else |

An image built with a board support package compiled in (`build_flags = -I path/to/bsp`, GOING-FURTHER.md "Make
your own board") puts that board first, as its default, named by `PROPTROLLER_BOARD_ID` (and optionally
`PROPTROLLER_BOARD_NAME`, `PROPTROLLER_BOARD_PIXEL_INVERTED`).
