---
name: proptroller-firmware-arduino
description: How the C++ Arduino/PlatformIO firmware and the proptroller Arduino library are laid out, built and tested (esp32dev, pico, picow, pico2w, native Unity tests, host simulator), and how they stay in step with the Rust firmware. Load before editing or reviewing anything under firmware/arduino.
---

# Proptroller firmware (C++, Arduino)

The code is in `firmware/arduino`. Read `firmware/arduino/README.md` and
`lib/proptroller/README.md` (the beginner's first project). The protocol is `firmware/PROTOCOL.md`; the
pulse-and-recover effect is `firmware/STATE-MACHINE.md` (read it on main, keep no copy). Load
`proptroller-hardware` for pins and ratings.

## Layout
- `lib/proptroller/`: a real Arduino library (`library.properties`, `library.json`, `keywords.txt`, examples
  `01_BlinkAnOutput` … `09_PulseAndRecover`, `GLOSSARY.md`, `GOING-FURTHER.md`).
  Two layers. `src/Proptroller.h` is the beginner layer and offers only: `Proptroller prop; prop.begin();
  prop.update(); prop.when(TRIG1)` and, on the `Sequence`, `turnOn(o)`, `turnOn(o, time)`, `turnOff(o)`,
  `wait(time)`, `then()`, `playSound(n)`, with `seconds` / `milliseconds` / `minutes`. `src/ProptrollerAdvanced.h`
  is everything else: `prop.advanced()` returns a `PropAdvanced` (rests and runs per rest, remote starts,
  hold-to-start, every other step, pixels, profiles and names, `setting`, `triggerHoldTime`, `PulseAndRecover` /
  `withPulseAndRecover`, direct control, `addTask` (`PropTask::update(PropAdvanced&, now)`), hooks, MQTT,
  `begin(FirmwareMaker)`, `memoryReport()`); its `when()` gives an `AdvancedSequence`. Never add a call to the
  beginner layer without the owner; `test_friendly` checks the split at compile time. Safe defaults are the
  same on both layers. What beginners see first is the show script (`firmware/SHOW-SCRIPT.md`, owner
  2026-10-20); the sketch API is the code-level layer underneath;
  `src/proptroller/` board support, show engine (`engine`, `window` = RunWindow), protocol (`proto`), settings,
  the generic `Controller` (`device`; `device_effect.cpp` the effect and the aliases; `device_script.cpp` show
  scripts: `get_script` / `check_script` / `set_script`, MQTT `script/set|check` -> `script/result`, the script's
  own LittleFS record, the boot re-check, settings that would break it refused, `dmx_port`), `script` (the
  show-script checker into a fixed step table and its runner; every message is
  `firmware/show-script-golden/README.md`, the contract written by the Rust side), `effect` (the
  pulse-and-recover state machine), `profile.cpp` (alias tables), `password` (salted SHA-256, the same hash as Rust), HTTP/WebSocket
  (`net/`), simulator (`sim/`); `src/proptroller/arduino/` the ESP32 / RP2040 platform code.
- `src/main_wifi.cpp` (ESP32, Pico W, Pico 2 W), `main_usb.cpp` (Pico), `main_sim.cpp`, `main_bench.cpp`;
  `bench/hw_bench/` the on-hardware benchmark firmware.
- `test/` Unity host tests (bsp, core, device, http, golden, mqtt, effect_reference, effect_random,
  pulse_and_recover, aliases, friendly, script, script_golden (all of `firmware/show-script-golden/` byte
  for byte and its traces), script_board, script_fit); `test/golden/` is written by the
  Rust simulator, `test/test_golden/session.inc` is the same session by hand (keep both in step).
- `tools/`: `web_assets.py` (runs before every build), `golden/` (Rust crate: the golden generator plus the
  `conformance` and `bench` bins), `golden.sh`, `check-host.sh`, `build-examples.sh`, `hw_bench.py`,
  `hw_check.py` (bench: flash, boot time, `mem`, a smoke test with nothing connected).
- `AB-REPORT.md`: flash/RAM, the fit on every board, build times, host performance against Rust, parity gaps,
  the bench plan.

## Commands (from `firmware/arduino`, PlatformIO in a venv, e.g. `~/.platformio/penv/bin` on PATH)
```sh
pio run                                  # esp32dev, pico, picow, pico2w
pio test -e native                       # every host test
pio run -e sim && .pio/build/sim/program # simulated board on http://localhost:8080
CHROMIUM=/usr/bin/google-chrome tools/check-host.sh   # Rust client conformance + phone UI test on the C++ sim
CARGO_TARGET_DIR=<outside the tree> tools/golden.sh   # regenerate the golden files from Rust (needs cargo)
tools/build-examples.sh                  # compile every example
```

## Rules
- Same behaviour as Rust (A/B pair): the golden session stays byte-identical (only the `bad_request` parser
  wording differs). After merging `firmware-rust`: regenerate the golden files, mirror new calls in
  `session.inc`, port changed messages (`ignoredText`, `ParamDesc::explain`, `explain(ShowError)`), and run
  `firmware/rust/web-test/ui-test.mjs` and `settings-test.mjs` against the C++ simulator.
- Generic names only in this tree (run, pulse, recovery, recover, cooldown, trigger out, sound out,
  `remote_trigger_enable`); a project's own words come from a profile alias table, kept with that project.
- `effect = pulse_and_recover` runs the effect as its own state machine beside the show engine, as Rust's
  `pulse_recover.rs`: settings group Effect (`out1_on_ms` … `remote_trigger_enable`, same order, labels and help as
  Rust's `effect_params()`), owned terminals, actions `trigger` / `recover` / `cancel`, status lines, the state
  panel machine. `test/test_effect_reference` has a test per SM id and E1-E26; `test/test_effect_random` checks
  I1-I10 every 1 ms (`Sim::tick1`). Turning it on removes the untouched example show. The recovery time is
  `out2_on_ms` only (no knob). The show version is `presets.h` `PulseAndRecoverShow` (`start_when_held`,
  `queue_api_runs`, `cooldown_from_start`), C++ only, tested by `test/test_pulse_and_recover`.
- SM-S3: no load-current settings, guard or faults; `pixel_brightness` (0-100 %) is a plain colour scale; the
  per-output maximum on-times and soft starts stay.
- Profiles (SM-AL): `set_profile` / `get_profile` with the reference's table format; key, control, status and
  terminal aliases on web, MQTT and serial; `topic_prefix`; `test/test_aliases` (E27-E31) with neutral names
  (VALVE, REFILL). Terminal names reach sketches through `prop.advanced().when("REFILL")` / `.loadProfile()`.
- TRIG1 / TRIG2 are sampled every 1 ms (`Firmware::sample` between ticks, `Sim::script` for 1 ms patterns,
  `Sim::tick1` for a 1 ms step); on the RP2040 the step runs on core 1 with its own 8 KB stack. The serial port
  takes JSON lines (local access: no page lock) and the console (`key=value`, `key`, controls, `status`,
  `settings`, `help`, `mem`).
- Page lock: `page_lock` + `pin` (set-only, salted SHA-256); the `pin` capability only while locked; MQTT
  `config/set` and `control/restart` refused while locked (not on the USB serial port). `proptroller/broadcast/getName` is the only broadcast
  topic; no board may be named `broadcast` or contain `+ # /`.
- WebSocket sessions: 2 s ping, 5 s heartbeat timeout; `Firmware::sessionClosed` ends what a phone page was given.
- The web UI is `firmware/rust/web`, gzipped into `lib/proptroller/src/proptroller/web_assets.h` by
  `tools/web_assets.py`; the header is committed so the library builds in the Arduino IDE. Never edit it by hand.
- Beginner first (as in Rust): plain names with units, ~10-line sketches, numbered examples, plain-word errors;
  every limit and default is a named constant.
- Secrets set-only; no `.pio/` output in commits; no personal paths or event content in the public tree.
