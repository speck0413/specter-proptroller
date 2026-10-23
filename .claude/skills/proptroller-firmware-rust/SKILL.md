---
name: proptroller-firmware-rust
description: How the Rust firmware for the Spectre Proptroller is laid out, built, tested and simulated (ESP32 esp-hal, Pico embassy-rp, host simulator), and the rules for changing it. Load before editing or reviewing anything under firmware/rust or app/src-tauri.
---

# Proptroller firmware (Rust)

Read `firmware/rust/README.md` (layout, first project, run commands) and `firmware/rust/PROTOCOL.md` (the control
protocol every firmware and the apps share). Load `proptroller-hardware` for pins and ratings and
`proptroller-web-app` for the phone UI.

## Layout
- `crates/` host workspace: `proptroller` (beginner layer, start here), `-bsp` (terminals, pin maps, safety guard,
  DFPlayer, WS2812, inputs, storage), `-core` (show engine, triggers, cooldown, run windows), `-proto`, `-device`
  (the generic controller), `-http` (no_std HTTP + WebSocket), `-web` (embeds `web/`), `-sim`, `-provision`.
- `esp32/` (Xtensa, `fw-generic-esp32`), `pico/` (`fw-generic-pico`: Pico, Pico W, Pico 2, Pico 2 W),
  `net/proptroller-net` (network tasks shared by every firmware), `examples/` (numbered beginner examples).
- Everything except the simulator is `no_std` + `alloc`: the firmware runs the code the simulator tests.
- `pico/` holds `proptroller-pico` (board bring-up, shared by every Pico firmware) and `fw-generic-pico`; on the W
  boards GP25 is the radio's chip select, the status LED is the radio chip's. `net/proptroller-net` is the network
  stack every firmware shares.
- Triggers are sampled at 1 kHz; the engine tick stays 10 ms (outputs change on the tick).
- The pulse-and-recover effect is a preset show in `proptroller-core::presets`; the README lists which `E…`
  scenarios it covers. `RunWindow::count_run(now)` expires an ended window before counting.
- Page lock (`page_lock`, hashed page password) replaces the old PIN; it gates every request that changes something.
- A project's own words (keys, labels, MQTT prefix) come from a profile alias table (STATE-MACHINE.md §11).

## Commands (from `firmware/rust`)
```sh
cargo test                                   # every host test, including end to end over sockets
cargo run -p proptroller-sim -- serve        # simulated board, http://localhost:8080 (--count N, --speed X, --no-mdns)
cargo run -p proptroller-sim -- run crates/proptroller-sim/scenarios/*.json
(cd esp32 && cargo build --release)          # needs espup; flash with espflash flash --monitor <elf>
(cd pico && cargo pico | pico-w | pico2 | pico2-w)   # picotool load -x -t elf <elf>
cd web-test && node ui-test.mjs . http://127.0.0.1:18080/   # browser test (playwright-core, CHROMIUM=...)
```

## Rules
- Behaviour of the pulse-and-recover effect is fixed by `firmware/STATE-MACHINE.md`: every `SM-…` rule and
  `E…` scenario has a test that names its id; change that file first, then the code.
- Beginner first: the `proptroller` crate reads as plain words with units (`seconds(2)`), ~10-line effects, no
  generics or lifetimes in the beginner layer, plain-word errors. Advanced users get escape hatches in the lower crates.
- The Rust and C++ firmwares are an A/B pair: same protocol, same web UI, same behaviour. A behaviour change lands
  in both (or is recorded as a gap).
- Secrets (Wi-Fi password, page password) are set-only: never printed, logged, published or read back.
- No build output (`target/`) in commits; no personal paths, customer names or event content in the public tree.
- Hardware has not run this firmware yet: say "tested in the simulator" unless a bench result exists.
