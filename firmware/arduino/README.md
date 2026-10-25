# Spectre Proptroller firmware (C++, Arduino framework)

One firmware for four boards, built with PlatformIO on the Arduino cores: ESP32 DevKit (`esp32dev`), Pico
(`pico`, USB only), Pico W (`picow`) and Pico 2 W (`pico2w`), and for three bare devkits with no Proptroller board
(`esp32dev_generic`, `esp32s3_generic`, `esp32c3_supermini`: [../BARE-DEVKIT.md](../BARE-DEVKIT.md)). It speaks the protocol (`firmware/PROTOCOL.md`)
over HTTP, WebSocket, USB serial and MQTT, serves the phone page (`firmware/web`), runs shows, show scripts
(`firmware/SHOW-SCRIPT.md`) and the pulse-and-recover effect (`firmware/STATE-MACHINE.md`), and updates over
the air with rollback. The library (`lib/proptroller`) makes effects easy to write: start with
[lib/proptroller/README.md](lib/proptroller/README.md).

Setup: the [quick start](../../README.md#developer-quick-start-vs-code) in the root README.

```
lib/proptroller/          the library (also an Arduino IDE library: examples/, keywords.txt)
  src/Proptroller.h       the beginner layer: prop.when(TRIG1).playSound(1).turnOn(OUT1, seconds(2))...
  src/ProptrollerAdvanced.h  everything else, listed in one place
  src/proptroller/        show engine, protocol, settings, the Controller, scripts, HTTP/WebSocket, simulator
  src/proptroller/arduino ESP32 / RP2040 platform: pins, PWM, pixels, LittleFS, Wi-Fi, mDNS, MQTT, ESP-NOW, OTA
lib/picoboot, src/picoboot  the Pico W / Pico 2 W bootloader (PICO-BOOTLOADER.md)
src/                      main_wifi.cpp (ESP32, Pico W, Pico 2 W), main_usb.cpp (Pico), main_sim.cpp, fuzz_main.cpp
test/                     host tests (Unity), golden protocol and console files, fixtures, fuzz regressions
tools/                    build helpers, check scripts, ptclient.py (protocol client), hw_check.py, hw_bench.py
bench/hw_bench/           on-hardware measurement firmware
workspace-template/       a workspace for your own firmware: one Arduino sketch folder per project
bsp/relay4-button/        an example board support package (another board's pin map)
bsp/esp32-devkit-bare/, bsp/esp32-s3-devkit/, bsp/esp32-c3-supermini/   the bare devkits' BSPs (BARE-DEVKIT.md)
```

## Build, test, run

```sh
pio run                                 # esp32dev, pico, picow, pico2w and the three bare devkits
pio run -e boot_rp2040 -e boot_rp2350   # the Pico bootloaders
pio run -e esp32dev -t upload           # flash an ESP32 DevKit
pio test -e native                      # every host test
pio test -e native_asan                 # the same under ASan, UBSan, LSan
pio run -e sim && .pio/build/sim/program   # a simulated board: http://localhost:8080/, UDP discovery
tools/check-static.sh                   # clang-tidy and cppcheck
tools/check-host.sh                     # protocol client, an update, and (CHROMIUM set) the browser tests
tools/build-examples.sh                 # every example and the template, for the boards
tools/golden.sh                         # regenerate test/golden and test/golden-console (review the diff)
tools/fuzz.sh [seconds] [target]        # libFuzzer
```

| Environment | Board | Network |
|---|---|---|
| `esp32dev` | 30-pin ESP32 DevKit | Wi-Fi, setup AP, mDNS, UDP discovery, MQTT, ESP-NOW cues |
| `pico` | Raspberry Pi Pico | JSON lines over USB serial |
| `picow`, `pico2w` | Pico W (RP2040), Pico 2 W (RP2350), behind the Pico bootloader | as `esp32dev`, without ESP-NOW |
| `esp32dev_generic` | bare ESP32 30-pin DevKit (BSP `bsp/esp32-devkit-bare`) | as `esp32dev` |
| `esp32s3_generic` | bare ESP32-S3 DevKit (BSP `bsp/esp32-s3-devkit`) | as `esp32dev` |
| `esp32c3_supermini` | ESP32-C3 SuperMini (BSP `bsp/esp32-c3-supermini`), single core, USB-CDC console | as `esp32dev` |
| `esp32s3_qemu` | the S3 image without Wi-Fi, for QEMU's esp32s3 machine (QEMU.md) | none |
| `boot_rp2040`, `boot_rp2350` | the Pico bootloader | none |
| `native`, `native_asan` | host | unit tests |
| `sim` | host | simulated board server |
| `sim_win` | Windows host (MinGW-w64 gcc) | simulated board server, without MQTT |
| `fuzz` | host | libFuzzer targets (`PT_FUZZ_TARGET`) |

The host builds (`native`, `sim`) work on Linux (GCC) and macOS (Apple clang). On clang `tools/strict_flags.py`
leaves out `-Wsign-conversion` (GCC's `-Wconversion` has none for C++), `tools/emc_posix_apple.py` opens
espMqttClient's POSIX client to macOS (it checks `__linux__` only), and `test_heap_peak` is ignored (it counts
glibc's `__libc_malloc`).

`tools/web_assets.py` gzips `firmware/web` into `lib/proptroller/src/proptroller/web_assets.h` before every
build; the header is committed so the library builds in the Arduino IDE. Never edit it by hand.

Flash maps and the first flash of each board: `firmware/FLASH-MAP.md`. Never `pio run -t upload` on `picow`,
`pico2w` or the bootloader envs (it is refused with the recipe).

Arduino IDE (best effort): the library builds. A Pico W / Pico 2 W built there has no over-the-air update (the
bootloader link is a PlatformIO step); for the ESP32 copy `firmware/partitions/esp32-4mb.csv` into the sketch
folder as `partitions.csv`; for the plain Pico pick "2MB (Sketch: 1792KB, FS: 256KB)".

## Your own firmware

`workspace-template/` is a folder to copy: each project is an Arduino sketch folder (`firmware/<name>/<name>.ino`
with a `sketch.yaml` naming its board), and one `platformio.ini` has an env per project with the stock flash maps,
flags and library versions (`make_envs.py` writes the envs from the sketch folders). Its `README.md` has the
first flash and what the Arduino IDE path loses.

Another board (your own PCB, other pins, an active-high button): a board support package, see
`lib/proptroller/GOING-FURTHER.md` "Make your own board" and the example `bsp/relay4-button/`.

## Updates and backups

`info` reports `fw_impl: "cpp"`, `fw_build` (the git revision) and, where updates work (ESP32, Pico W, Pico 2 W),
`update` (`slot`, `max_bytes`, `state`, `rolled_back`). `POST /api/v1/update` takes the next image with its
SHA-256. The ESP32 writes its other app slot; the Pico W / Pico 2 W write the dfu slot and the bootloader swaps
it in at the restart (about 16 s). A new image that does not pass its 30 s self-check rolls back. The plain Pico
is updated over USB. `get_backup` / `put_backup` save and restore everything but the secrets and the board's own
Wi-Fi, name and page lock. Stored records: FLASH-MAP.md "Stored records". The simulator runs the whole flow
(`tools/ptclient.py image OUT.bin --version 0.2.0`; `--fail-self-check` makes every image roll back).

## Checks

| Check | Run | Catches |
|---|---|---|
| Strict warnings, our sources only (`tools/strict_flags.py`) | every build | `-Wall -Wextra -Wshadow -Wconversion -Wswitch-enum ...`; on the boards `-Wframe-larger-than=1024`. `PT_WARNINGS_ONLY=1` lists all |
| Stack budget (`tools/stack_check.py`) | every board build | a path deeper than its stack less 50 % |
| Static analysis | `tools/check-static.sh` | clang-tidy (`.clang-tidy`) and cppcheck findings |
| Host tests, sanitizers | `pio test -e native`, `-e native_asan` | controller, protocol (golden files), engine, scripts, HTTP, updates, backups, records, console, MQTT |
| Fuzzing | `tools/fuzz.sh` | crashes and undefined behaviour on any input; a failing input goes to `test/fuzz-regressions/`, which `test_fuzz_corpus` replays |
| Browser and client | `tools/check-host.sh` | the phone page's tabs, an update, a rollback, the Pico swap (`--swap-ms`), the protocol client |

Code rules:
- Results that say whether something worked are `[[nodiscard]]`; an intended ignore is `(void)call(...)`.
- Enums are `enum class`, used qualified (`Action::Kind::On`). The sketch API's `TRIG1`, `OUT1` ... are plain
  enums so a beginner writes `prop.when(TRIG1)`.
- Name tables carry `PT_ONE_PER(table, Last)`; pin maps and flash map arithmetic are `static_assert`s.
- Times are milliseconds named `..._ms`; the sketch API takes only a `Duration` (`seconds(2)`).
- Every limit and default is a named constant. C++17 on every board.

## Shows on a new board

A new board (first boot or factory reset) has **no shows**: wiring a switch or sensor to an input starts nothing
(the CC1 / CC2 output roles, trigger repeat and sound trigger, still pass a TRIG press on as a 200 ms contact
pulse; Settings changes them). The example show (TRIG1 pulses OUT1 for 2 s with a pixel flash) is added only when
asked for: phone page Shows, **Add an example show**, or `{"op":"add_example"}` from any interface. A board that
an earlier firmware gave the factory example keeps an edited copy; an untouched one (byte for byte) is removed at
power-on and saved without it. From the phone page or the protocol: **+ New**, **Link** (an input turns outputs
on while held, or for a time), replace (Save as, an existing show) and delete (asks on the page) (PROTOCOL.md
`put_show`, `delete_show`, the `until_release` trigger). Recording is the phone page's **Record effect** tab (and
Specter Studio's): what you tap becomes an effect script (`firmware/docs/RECORD-EFFECT.md`). The board has no
recorder or learn mode of its own; USER1-3 are plain triggers.

## The pulse-and-recover effect

`effect = pulse_and_recover` (Settings, group Effect) runs `firmware/STATE-MACHINE.md` as its own state machine
beside the show engine (`proptroller/effect.h`). TRIG1 starts a run (released since the last run, then held for
`trig_debounce_ms` while Ready), OUT1 pulses for `out1_on_ms`, OUT2 recovers for `out2_on_ms`, at most
`runs_per_cooldown` runs per `cooldown_ms` counted from the first run. TRIG2 recovers; OUT3 is the optional
after-pulse output, OUT4 the optional ready lamp. While it is on it owns those terminals. Controls (`action`
keys, MQTT `<prefix>/<name>/control/…`, serial words): `trigger`, `recover`, `cancel`, `restart`; a network
trigger needs `remote_trigger_enable` (never stored). Status lines are also published on
`<prefix>/<name>/status/<key>`.

From a sketch: `prop.advanced().withPulseAndRecover(PulseAndRecover().pulseFor(seconds(1)).recoverFor(seconds(5.5)))`
(example 09). Tests: `test_effect_reference` (one test per SM rule and scenario E1-E26, named after them),
`test_effect_random` (invariants I1-I10, 8 boards x 300 s at 1 ms), `test_aliases` (E27-E31).

## Profiles: a project's own names

`set_profile` / `get_profile` load and read an alias table (STATE-MACHINE.md §11), stored with the settings:
`{"topic_prefix": "myprop", "aliases": [{"alias": "valve_time", "key": "out1_on_ms", "label": "Valve open time",
"unit": "ms"}, {"alias": "go", "control": "trigger"}, {"alias": "REFILL", "terminal": "TRIG2"}]}`. Aliases work
on the web page, MQTT and serial; `topic_prefix` replaces `proptroller` in every topic; a terminal alias shows as
`REFILL (TRIG2)` and works in sketches (`prop.advanced().when("REFILL")`). A project's table lives with that
project.

## How it runs

- `prop.begin()` drives every output low before anything else, then loads settings and shows, opens the
  DFPlayer port and the pixel driver, starts the 10 ms step (its own FreeRTOS task on the ESP32, core 1 on the
  RP2040; triggers are sampled every 1 ms between steps) and Wi-Fi.
- Without `prop.begin()` the first call that needs the controller starts it (`ProptrollerAuto.cpp`), and the
  network and serial port are served in the background: a FreeRTOS task on the ESP32; on the RP2040 core 0 in
  `prop.update()` and in `delay()` (arduino-pico's weak `delay` is replaced; it has no hook after `loop()`, so
  `loop()` must call one of them). `prop.advanced().begin(maker)`
  must then come first in `setup()`. Gate pull-downs hold the outputs low until the controller starts.
- `prop.update()` in `loop()` serves the network and the USB serial port. The controller is behind a lock;
  sockets, DNS and the broker are waited on without it, so the network never delays a step (`test/test_isolation`:
  a socket and a broker stalled for 20 s while triggers, stop all and a script keep their timing). ESP32: MQTT is
  espMqttClientAsync on AsyncTCP and Wi-Fi joins in the background; Pico W: joins and broker connects are bounded
  (about 5 s, 3 s) and run on core 0 (`arduino/net.cpp`). The step never writes the flash: a settings save it
  decides on, the self-check's mark of a new image and a script record to rewrite are done by `prop.update()`. The
  settings save, the script record of `set_script` / `put_backup` and the self-check mark give the lock back while
  they are written, and so do a request's parse, a script check and the MQTT hello (`test/test_safety`; QEMU
  `--net` measures the step gap across `set_script` / `put_backup`). While a request has the lock given back only
  the step runs: every other task (the network, the serial port, the sketch's `loop()` and its calls such as
  `setting()` or `nameOutput()`) that takes the lock waits until the request takes it back
  (`proptroller/window_lock.h`), and a change asked from the step itself (a sketch's task) is refused with "the
  board is busy" (stopping is never refused). `test/test_threads` runs this with real threads, also under
  ThreadSanitizer (`pio test -e native_tsan`: every host test).
  Still under the lock: `factory_reset`'s wipe (a restart follows) and an update's sector writes (update mode,
  outputs off). The flash itself still pauses the step while it erases or programs (QEMU.md N4). The hardware watchdog watches the step, not
  loop(): a hung step restarts the board with the outputs off (ESP32 task watchdog, 5 s; Pico 8 s, fed only while
  core 1's step ran less than 100 ms ago); a stalled network or loop() never does.
- The flash writes need `loop()` to call `prop.update()` (or `delay()`): the settings save, a script record to
  write again and the self-check's mark of a new firmware are done there, never in the step. A `loop()` that
  never calls either (possible only with ProptrollerAuto: the step keeps running without it) never saves a
  change, and a new firmware is never marked good, so the next restart goes back to the previous one (ESP32
  rollback, Pico bootloader). Every example's `loop()` calls `prop.update()`.
- The self-check's mark: when the stored records were read but writing the mark itself fails (a flash error),
  the controller goes back to the previous firmware as for a failed check (the simulator models a failed check
  the same way); it does not try the mark again.
- A trigger held at power-on never triggers. The firmware does not measure load current: each terminal's rating
  is in `hardware/README.md`; maximum on-times (off by default) and soft starts are settings.

## Sizes

Firmware 0.1.0 as built from this tree (2026-10-22), against the 80 % target ("Image" is the flash PlatformIO reports
used):

| Board | Image | Slot | Use | Static RAM |
|---|---|---|---|---|
| esp32dev | 1,136,417 B | 1,966,080 B (app slot) | 57.8 % | 47,888 B |
| pico | 309,544 B | 1,830,912 B (sketch) | 16.9 % | 9,468 B |
| picow | 644,756 B | 933,888 B (active slot) | 69.0 % | 77,008 B |
| pico2w | 635,368 B | 1,982,464 B (active slot) | 32.0 % | 79,348 B |
| esp32dev_generic (2026-10-25) | 1,267,889 B | 1,966,080 B (app slot) | 64.5 % | 49,040 B |
| esp32s3_generic (2026-10-25) | 1,217,489 B | 1,966,080 B (app slot) | 61.9 % | 49,032 B |
| esp32c3_supermini (2026-10-25) | 1,284,456 B | 1,966,080 B (app slot) | 65.3 % | 43,076 B |
| boot_rp2040 / boot_rp2350 | 2,760 / 2,704 B (`firmware.bin` 3,016 / 2,704 B) | 24,576 B (bootloader region) | about 12 % | |

## MQTT on the simulator

`.pio/build/sim/program --mqtt 127.0.0.1:1883` (or `mqtt_broker` / `mqtt_port` in Settings) runs the ESP32's MQTT
session (`net/mqtt.h`) on espMqttClient's POSIX client. `tools/mqtt_check.py` (run by `check-host.sh`) starts its
own broker and the simulator and checks: online and state retained with `boot_id` / `mqtt_seq`, rpc, the broker
down and back, a broker that accepts and never answers (the show keeps time), the will when the simulator dies.

## Changes for sketches written for earlier previews

- `PropAdvanced::firmware()`, `tick()` and `sample()` are gone (d2c945f): the library steps the board itself
  (the ESP32 step task, RP2040 core 1). A sketch that called them stepped the board twice; delete those calls.
- `triggerHoldTime()` now writes only when the sketch's value changed since the last boot, as `setting()` does:
  a value set from the phone meanwhile stays.
- While a request has the controller lock given back, a sketch's calls on another task wait for it (a few ms;
  seconds while an update is checked); a change asked from a `PropTask` (it runs in the step) can be refused
  with "the board is busy: try again in a moment" and is reported with `say()`.

## On hardware so far

One bare ESP32 DevKit (ESP32-D0WD-V3, nothing wired) ran the firmware before this release candidate, over USB
serial only: first flash, boot (ready after 330-437 ms), heap (at least 114 KB free during a 10.5 KB put_backup
with a show running), outputs off at every boot, max on-time, `stop_all`, `pause`, scripts, backups and the
factory reset behaved as documented. Three things it found are fixed here: requests over about 300 B lost bytes
over USB serial (the 256 B receive buffer while loop() wrote a 1.35 KB state line every 250 ms: now a 4 KB
receive buffer, a state line only when it changed and never while a request comes in, and `set_script` takes `bytes`);
an output asked for during a pause came on at its release (now refused); `get_params` and `get_backup` built
their answers holding the lock (now with it given back).

Known limit, measured on that board with a show running: writing the flash stops the step while the chip erases
or programs (both cores lose their cache; QEMU.md N4): `put_backup` of 10.5 KB 98-104 ms, a 7.6 KB `set_script`
44-51 ms, against the bench's 15 ms. Moving the step and everything it calls into IRAM would avoid it and is not
done; these are maintenance requests, so do not send them while a show must keep exact time. Before the fix,
`get_params` gave 32-41 ms and `get_backup` 21-24 ms (lock held while building the answer): to be measured again.

Untested: the Pico bootloader on real chips (PICO-BOOTLOADER.md "Bench steps"), the ESP32 rollback, Wi-Fi beyond the ESP32's network scan, 1 ms
trigger sampling under Wi-Fi load, the DFPlayer, pixels, DMX and MQTT against real devices, any Pico.

Step timing on the bench (no scope needed): `mem` on the serial console reports, since the last `mem`, the
longest and average 10 ms step and how many took over 1 ms or 10 ms, the 1 ms samples the same way (over
250 us), the longest gap between two wakes of the step, on the ESP32 how long after its hardware-timer latch a
sample was handled (and any lost from the 64-entry ring), and the longest wait of the step for the controller
lock. A build with `PLATFORMIO_BUILD_FLAGS=-DPROPTROLLER_STEP_PROFILE pio run -e esp32dev` adds the tick's
time per part (inputs, triggers, script, engine, pins, sound, pixels, the rest) and the longest copy of the state
made by a reader. ESP32 DevKit, QIO 80 MHz (the esp32dev env since 2026-10-25), a script animating 600 or 16
LEDs: step max 117-219 us (average 80-110), 0 over 1 ms, no sample lost; the step's longest wait for the lock
55-60 us with the serial state stream, about 200-280 us while requests come in. A flash write (a settings or
script save, which waits for a quiet moment) stalls both cores whatever the lock does: samples are then handled
up to about 25 ms late. A board updated over the air keeps its DIO 40 MHz bootloader and still boots the QIO
image (bench: average 112-133 us, max 230-321 us).

Bench plan. First `tools/hw_check.py --port <port> --env <env>` per image (flash, boot time, `mem`, a smoke test
with nothing connected). Then `bench/hw_bench` with `tools/hw_bench.py http://<board>/`:

| Measurement | How | Pass |
|---|---|---|
| Trigger to output | 1 kΩ jumper from expansion S4 to the TRIG1 tip; `bench_latency`, 30 trials; once with a scope | < 120 ms |
| Step jitter | `bench_jitter` idle, while the WebSocket is flooded, during a Wi-Fi reconnect and an unreachable MQTT broker | max < 15 ms |
| HTTP / WebSocket round trip | `/api/v1/info`, keep-alive RPC, WebSocket RPC, `/app.js` | p95 < 50 ms |
| Boot to outputs safe | scope on EN / RUN and the OUT gates; `bench_boot` | outputs never high; ready < 1 s |
| Free heap at idle | `mem` after 60 s with one WebSocket client | > 60 KB (ESP32) |
| Pixel timing | scope on PIXEL data | WS2812B spec ±150 ns |
| Update and rollback, ESP32 | an update, one that fails its self-check, one reset 5 s after the restart | `pending` then `valid`; failures come back with `rolled_back`; records survive |
| Task watchdog, ESP32 | a step that blocks 10 s (a loop that blocks never restarts) | restart after about 5 s, outputs low |
| LittleFS headroom | the largest script, 16 large shows, 50 setting changes, power cuts | `store_errors` 0, records read back |
| DFPlayer | a real module and two clones: ACK, checksum, ambient loop, missing file | as the simulator tests |

Network bench steps (the network must never stop, delay or restart a show; each with a show running, OUT1 on a
scope or LED, `mem` before and after):

1. A phone walks out of range with the page open: no restart, the show keeps time, another phone loads the page.
2. No Wi-Fi (station set, router off): shows run from the first second; the hotspot is up from power-on and the
   board retries every 60 s (Pico W: each try holds `loop()` about 5 s; time the longest with a wrong password
   and with the router cut mid-join). The hotspot is `Proptroller-XXXX` at 192.168.4.1.
   With the router on: the hotspot and the network answer at the same time (192.168.4.1 and `<name>.local`).
3. Router back during 2: joined within a minute, `http://<name>.local/` works.
4. A broker that never answers (nothing listening, an unresolvable name, `kill -STOP` mid-session): shows keep
   time, reconnects 5, 10, 20, 40, 60 s apart; resumed, `online` is `true` again with a new `mqtt_seq` run.
5. ESP-NOW cue storm between two ESP32s: the receiver keeps time and runs the cues.
6. Four phones, then a fifth (Pico W: two, then a third): the extra one gets 503 and works once a page closes.
7. A phone that stops reading (asleep with the page open): dropped within 10 s, `mem` flat.
8. The largest backup restored twice on a Pico W with a second phone open: both succeed, minimum heap > 20 KB.
9. A foreign page (another origin) opening `ws://<board-ip>/api/v1/ws`: refused; the board's own page works.
