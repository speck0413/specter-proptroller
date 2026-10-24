# Going further

The beginner functions are a thin layer over the real controller. Everything below them is public C++
in namespace `pt`, documented in the headers, and tested on the host.

## The layers

| Layer | Header | What it is |
|---|---|---|
| Beginner | `Proptroller.h` | `prop.begin()`, `prop.update()`, `prop.when(TRIG1)` with `turnOn`, `turnOff`, `wait`, `then`, `playSound` |
| Advanced | `ProptrollerAdvanced.h` | `prop.advanced()`: every other sequence step and rule, direct control, profiles and names, settings, the built-in effect, tasks, hooks, MQTT |
| Controller | `proptroller/device.h` | `pt::Controller`: inputs, engine, roles, sound, pixels, settings, storage, protocol, MQTT topics |
| Engine | `proptroller/engine.h`, `show.h` | shows (the JSON of the phone editor), triggers, priorities, soft start, watchdogs, interlocks |
| Board | `proptroller/board.h`, `input.h`, `dfplayer.h`, `ws2812.h` | pins, debounce and trigger arming, the DFPlayer driver, the inverted pixel encoder |
| Protocol | `proptroller/proto.h`, `netapp.h`, `net/http.h` | requests, responses, events; the HTTP/WebSocket server; the interface any firmware implements |
| Platform | `proptroller/arduino/*.h` | ESP32 / RP2040 pins, Wi-Fi, mDNS, MQTT, storage, the controller lock |

## The advanced layer

`#include <ProptrollerAdvanced.h>` and call `prop.advanced()`: the same controller with every call. The list,
with a comment on each call, is [src/ProptrollerAdvanced.h](src/ProptrollerAdvanced.h).

The safe defaults stay on whichever layer you use.

## Escape hatches

* **The real objects:** `prop.advanced().controller()`, `prop.advanced().engine()`, `prop.advanced().board()`. Build a `pt::Show` yourself
  (or parse one from JSON with `pt::showFromString`) and give it to `prop.advanced().controller().addCodeShow(show)`.
* **Your own state machine:** derive from `PropTask` (its `update(PropAdvanced& prop, uint64_t now)`), register it with `prop.advanced().addTask(task)`; `update()` runs
  every 10 ms with the controller locked (example 08).
* **Watch every output write:** `prop.advanced().afterOutputChange(fn)`; at the board level `board().before_change`
  gets the level by reference and may change it.
* **Direct pin access:** `pt::ESP32_DEVKIT` / `pt::PICO` (proptroller/pins.h) list every GPIO; wrap
  `pt::BoardPins` to put the board on other hardware.
* **Custom protocol messages:** `prop.advanced().onRequest(fn)`: `fn(controller, request, response, user)` sees every
  request after the PIN check, including ops the library does not know, and may answer it (return
  true); an op nobody answers gets the usual bad_request. The phone UI and the desktop app ignore ops they
  do not know.
* **Custom MQTT topics:** `prop.advanced().onMessage("topic", fn)` and `prop.advanced().publish("topic", "payload")` (example 07).
* **Core 1 on the RP2040:** the library runs the 10 ms step there (it defines `setup1()`/`loop1()`); build with
  `-DPROPTROLLER_NO_CORE1` to use core 1 yourself (the step then runs in `prop.update()`).
* **Opt out of a safety rule, explicitly:** `prop.advanced().allowTriggerHeldAtPowerOn(TRIG1)`. The board cannot
  measure current, so there is no current limit to change: keep each load within its terminal's rating.
* **A whole firmware of your own:** implement `pt::Firmware` (a `pt::NetApp`: JSON in and out, info, state,
  events, MQTT topics; plus `tick()` and, if you want the USB console, `serialIn()`), and start it with
  `prop.advanced().begin(makeMine)`, where `pt::Firmware* makeMine(pt::BoardParts& parts)` builds it from the board's
  pins, sound port, pixels, storage and analog input. The network layer, the phone UI and the desktop app
  serve it unchanged; tasks and MQTT topics still work.

## What stays in charge

Whatever your code asks for:

* outputs are off at power-on, before anything else runs;
* a trigger held at power-on never starts anything, and a held trigger starts once until released.

The firmware does not measure or check load current (STATE-MACHINE.md SM-S3): keep each load within its
terminal's rating (`hardware/README.md`). What you can change are soft starts, watchdogs, interlocks and debounce
in Settings.

## Messages from your own tools

* HTTP / WebSocket: `firmware/PROTOCOL.md`. `pio run -e sim && .pio/build/sim/program` gives you a simulated
  board to develop against, and `tools/ptclient.py` (Python 3, no packages) talks to it or to a real board:
  `ptclient.py discover`, `ptclient.py rpc 192.168.4.1:80 '{"op":"list_shows"}'`.
* MQTT: `proptroller/<name>/rpc` takes the same JSON requests; `…/state`, `…/event`, `…/config/<key>` publish
  the rest (PROTOCOL.md → MQTT).
* `{"op":"custom",…}` and any op of your own reach `prop.advanced().onRequest(fn)` (above).

## Testing on your computer

`pio test -e native` runs the library's tests, and
`pio run -e sim && .pio/build/sim/program` runs a simulated board with the phone UI at
http://localhost:8080/. `Proptroller` itself works in host builds: `prop.hostSim()` is the simulated
board, so a sketch's `setup()` can be unit-tested (see `test/test_friendly`).

## Make your own board

The library runs on other boards too. The easy way needs no build: write a **board package**, a small JSON file
with the GPIO of every terminal and how each input is wired, and send it to a board running the stock firmware
(Specter Studio, or `set_board` over the USB serial port): `firmware/BOARD-PACKAGES.md`. The rest of this section
compiles a board into an image instead; such an image starts on that board when no board package is stored.

A board support package (BSP) is a folder with one header,
`proptroller_board.h`, that defines `constexpr pt::PinMap PROPTROLLER_BOARD_MAP` (layout: `src/proptroller/pins.h`):
the GPIO of every terminal, `pt::NO_PIN` for a terminal the board does not have (relays, contacts, USER buttons,
pixels, the sound port), and per input (bit 0 TRIG1, 1 TRIG2, 2-4 USER1-3) `active_high` (a button with a
pull-down; default active low) and `external_pull` (the board's own resistor; otherwise the internal pull-up or
pull-down is used). The runtime setting `trig1_input` / `trig2_input = sensor_high` inverts a trigger once more
(XOR with `active_high`). Select it with one build flag, `build_flags = -I path/to/your-bsp` (or
`-D PROPTROLLER_BOARD_HEADER='"my_board.h"'`); without one the Proptroller board is the default BSP. Name it
with `#define PROPTROLLER_BOARD_ID "my-board"` (and optionally `PROPTROLLER_BOARD_NAME`, and
`PROPTROLLER_BOARD_PIXEL_INVERTED false` when the board has no inverting level shifter on PIXEL): it becomes the
image's default built-in board (`get_board`), and a board package sent later replaces it.

A terminal set to `NO_PIN` is never configured, driven or read and is not offered on the phone page or in `info`.
The map is checked at compile time like the built-in ones (no GPIO used twice, no ESP32 output on GPIO34-39, no
internal pull on GPIO34-39). `bsp/relay4-button/` is an example (four outputs, an active-high button, nothing
else); `tools/build-examples.sh` builds it and `test/test_board_support` runs it on the simulated board
(`Sim(id, build, &map)`).

A BSP can also give a fresh board (nothing stored yet, or after a factory reset) its own starting point: define
any of these as string literals, in `proptroller_board.h` or in a header of their own, `proptroller_defaults.h`
(on the include path, or `-D PROPTROLLER_DEFAULTS_HEADER='"my_defaults.h"'`), which works on the built-in pin
maps too.

| Define | What | Applied as |
|---|---|---|
| `PROPTROLLER_BOARD_PROFILE` | an alias profile (`set_profile`'s JSON) | first |
| `PROPTROLLER_BOARD_SETTINGS` | `key=value` lines, as on the serial console (`#` comments; the profile's key aliases work) | second |
| `PROPTROLLER_BOARD_SCRIPT` | a show script (`firmware/SHOW-SCRIPT.md`) | last |
| `PROPTROLLER_BOARD_SCRIPT_NAME` | what that script is called (`get_defaults`) | |

The board keeps them like anything set by hand: a change made later is not undone at the next power-on. A value
the board refuses is logged in plain words and the rest still applies (`test/test_board_defaults`). So a board
with its own pins, words and behaviour is the stock firmware built with one `-I path/to/bsp` flag.
