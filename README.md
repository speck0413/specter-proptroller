# Specter Proptroller

An all-in-one, open-source controller for props, animatronics and show effects. One 134 x 80 mm board takes a plug-in
microcontroller (a 30-pin ESP32 DevKit **or** a Raspberry Pi Pico / Pico W / Pico 2 / Pico 2 W; a plain Pico 2 has no firmware build yet) and gives it the I/O a prop needs:

| Block | What it does |
|---|---|
| 4 power outputs (OUT1-4) | high-side switched supply voltage, 5 A each continuous at 12-24 V (7.5 A for 5 s at low duty from idle, 10 A start-up surge under 1 s; 10 A board total); PWM dimming and soft start up to 2 A at 12 V (1 A at 24 V); LED per output |
| 2 relays (RLY1-2) | dry COM / NO / NC contacts. Rev B: **120 V AC 5 A (3 A inductive) or 28 V DC 10 A**; rev A: **≤30 V DC only** |
| Trigger inputs (TRIG1, TRIG2) | 3.5 mm TRS jacks: contact closure or a small powered sensor (3.3 V on the ring) |
| Contact outputs (CONTACT1-2; CC1-2 in scripts and code) | isolated optocoupler closures on 3.5 mm jacks, to trigger other controllers or devices: signals only, ≤30 V DC, plan on about 2 mA, tip + / sleeve − |
| Pixels | 5 V level-shifted data for addressable LED strips |
| Audio | DFPlayer Mini MP3 module (two-way UART), speaker terminal and stereo line out |
| Buttons | three on-board user buttons |
| Expansion | 3 x 11 servo-style S/V/G header (I2C, analog, spare GPIOs), RAILS power terminal for accessories |
| Power | 9-24 V DC in (barrel ≤5 A or terminal ≤10 A), on-board 5 V / 2.5 A buck, TVS protection. Ratings are for a 12-24 V supply (12 V is the primary use case); below 12 V (down to the 9 V minimum), derate: the OUT FETs see less gate drive and run hotter |

Controllers can be cascaded: a CONTACT output or a relay of one board triggers the TRIG1 / TRIG2 input of another, so one
controller can drive up to four others; ESP-NOW / Wi-Fi is the option for richer coordination.

## Getting started

New here? **[GETTING-STARTED.md](GETTING-STARTED.md)** goes from the parts on your table to a running prop with no
programming: what to buy, flashing the stock firmware from the browser (ESP32) or by copying one file (Pico W),
the sound card, wiring, and your first effect script typed on your phone. Then:

- [firmware/SHOW-SCRIPT.md](firmware/SHOW-SCRIPT.md): the effect script language (nine kinds of line), and
  [firmware/show-script-examples/](firmware/show-script-examples/) with wiring notes;
- [the Arduino library](firmware/arduino/lib/proptroller/README.md): effects in code;
- [hardware/README.md](hardware/README.md): ratings, pinout and power.
- [docs/DEV-SETUP.md](docs/DEV-SETUP.md): from nothing to a running board with Specter Studio built from
  source (macOS, with Windows and Linux notes): the setup script, the clone, a USB install and a script.
- [docs/FIRMWARE-BUILD.md](docs/FIRMWARE-BUILD.md): build the firmware for each board, flash it over USB, make a
  release folder, update over Wi-Fi, and what CI does on a release tag.

## Status

This repository holds hardware rev B (relay insulation margin and air-gap slots for a 120 V AC relay rating, see
`hardware/README.md`), not yet ordered. Rev A, the first ordered design (silk "RELAYS: <=30V DC ONLY"; not yet built or
tested), differs as `hardware/README.md` describes (ratings, relay positions, errata).
A 3D-printed enclosure is in `enclosure/` (not printed yet).
Firmware: C++ on the Arduino cores with PlatformIO (`firmware/arduino`), tested on the host and in a simulator; not yet run on a
board. The desktop app is Specter Studio, a separate repository. Wiring guides and a build guide are in `docs/` (below).

## What is here

| Path | Contents |
|---|---|
| `hardware/board.ts` | the board, written as design intent for the [aieda](https://github.com/speck0413/ai-native-eda) toolchain (parts, nets, placement constraints, routing classes) |
| `hardware/frozen.ts`, `hardware/routes.ts` | the reviewed part placement and locked routing, so every build reproduces the same board |
| `hardware/lib/parts.ts` | the parts this board uses (KiCad footprints, LCSC numbers, pad maps checked against datasheets) |
| `hardware/requirements.yaml` | requirements with ids traced by the build |
| `hardware/README.md` | ratings, GPIO map, power budget, copper sizing, test pads and first-board checks |
| `enclosure/` | the 3D-printed enclosure: OpenSCAD model, `design.toml` with every size, Bambu Studio projects, build and check tools |
| `GETTING-STARTED.md` | from a new board to a running prop, no programming |
| `firmware/` | the firmware (`arduino/`), the protocol (`PROTOCOL.md`), flash maps, the phone web UI (`web/`) and its browser tests (`web-test/`), golden sets and example show scripts |
| `.github/workflows/release.yml` | on a tag `v<firmware/VERSION>`: builds the stock firmware (ESP32 DevKit, Pico W, Pico 2 W), drafts a GitHub Release with the update and first-flash images and `proptroller-firmware.json`, and publishes the browser flasher to GitHub Pages |
| `docs/` | wiring guides (interactive 3D pages, written pages, videos), the case build guide, switching AC loads, developer setup with Specter Studio (`DEV-SETUP.md`), building and flashing the firmware (`FIRMWARE-BUILD.md`) |
| `tools/` | `setup.sh` (firmware setup; `--dev` for the developer checks), bootstrap, JLC order packages (with rotation corrections), routing helpers |

The generated KiCad project (`hardware/kicad/`: board, schematic, rules, symbol library, build records) and the order files
(`hardware/fab/`: gerbers zip, BOM, CPL, three JLC packages) are committed, so you can open or order the board without building it.
Regenerating takes a few minutes and runs `tools/portable_paths.py` so the files carry no machine-specific paths.

## Developer quick start (VS Code)

For changing the firmware itself. To use a board, [GETTING-STARTED.md](GETTING-STARTED.md) is enough; to write
effects in code, start with [the library's README](firmware/arduino/lib/proptroller/README.md).

1. Clone and set up (Linux or macOS; Windows: see [Windows](#windows) below):
   ```sh
   git clone https://github.com/speck0413/specter-proptroller.git && cd specter-proptroller
   tools/setup.sh            # PlatformIO, a host compiler, the board packages; asks before sudo
   tools/setup.sh --dev      # optional: also clang-tidy, cppcheck, fuzzers, browser-test tools
   ```
   It offers (asking first) to put `pio` on your PATH and, on Linux, the serial-port access (the `dialout`
   group and PlatformIO's udev rules; log out and in afterwards).
2. Open it: `code spectre-proptroller.code-workspace`, then click **Install** when VS Code offers the
   recommended extensions (PlatformIO IDE, C/C++, EditorConfig, Markdown lint, spell checker, WSL).
3. Wait for PlatformIO to finish loading (status bar), then **Terminal > Run Task**:
   - **Native unit tests**: the host tests (no board needed).
   - **Host simulator**: a simulated board; open <http://localhost:8080/> for its phone page.
   - **Build all boards**: ESP32 DevKit, Pico, Pico W, Pico 2 W.
4. Flash a board (USB):
   - ESP32 DevKit: `cd firmware/arduino && pio run -e esp32dev -t upload`
   - Pico (no Wi-Fi): `cd firmware/arduino && pio run -e pico -t upload` (the first time, hold BOOTSEL while
     plugging it in). It is updated over USB only.
   - Pico W / Pico 2 W (first time): run the task **Pico W / Pico 2 W first-flash file**, hold BOOTSEL, plug in,
     copy `firmware/arduino/.pio/build/picoboot-pico-w/flash.uf2` (Pico 2 W: `picoboot-pico2-w`) to the drive.
     After that, update over Wi-Fi from the board's page.
5. Change something: your own firmware starts from `firmware/arduino/workspace-template/`; effects without code are show
   scripts (`firmware/show-script-examples/`). Writing sketches: [the library's README](firmware/arduino/lib/proptroller/README.md);
   how the firmware is built and tested: [firmware/arduino/README.md](firmware/arduino/README.md).

### Windows

Two ways, both with VS Code:

- **Native (simplest for building and flashing boards):** install [VS Code](https://code.visualstudio.com/), then the
  **PlatformIO IDE** extension (it installs PlatformIO and its Python by itself), and open
  `spectre-proptroller.code-workspace`. The tasks **Build all boards** and **Pico W / Pico 2 W first-flash file** and
  PlatformIO's own Upload button work in it. Install the USB driver your ESP32 DevKit needs (Silicon Labs CP210x or
  WCH CH340). The host unit tests and the simulator also need a C++ compiler for Windows (for example MSYS2's
  MinGW-w64 g++ on PATH; the simulator is `pio run -e sim_win`, without MQTT); the tasks that are shell scripts (static check, fuzzers, browser tests, QEMU, golden
  files) need WSL 2.
- **WSL 2 (everything):** install WSL 2 with Ubuntu (`wsl --install` in an administrator PowerShell), install the
  **WSL** extension in VS Code, open the folder with **WSL: Open Folder in WSL**, and run `tools/setup.sh` in its
  terminal. WSL does not see USB by itself: install [usbipd-win](https://github.com/dorssel/usbipd-win) in Windows,
  then `usbipd list`, `usbipd bind --busid <id>` (once, administrator) and `usbipd attach --wsl --busid <id>` each
  time the board is plugged in.


### If something goes wrong

- **The page says `rolled_back`**: the new firmware did not pass its self-check and the board went back to the
  one before. Your settings are as they were; try the update again or report it.
- **A Pico W that will not stay up** (restarts every few seconds): flash it again over USB (hold BOOTSEL, copy
  the first-flash file). Re-flashing keeps the settings, shows and script; if it still restarts (a saved setting
  such as an unreachable Wi-Fi), erase everything as below and set it up again.
- **Start over, erasing everything** (settings, Wi-Fi, shows, script): Settings, Factory reset on the page, or
  over USB: Pico `picotool erase` (then flash again), ESP32 `pio run -e esp32dev -t erase` (or
  `esptool.py erase_flash`) and flash again.


## Build (hardware)

Requires KiCad 10 (with `kicad-cli`), Node.js, Python 3 and Java (for Freerouting).

```sh
git clone --recursive <this repo> && cd spectre-proptroller
tools/bootstrap.sh        # aieda submodule, its Node deps, Freerouting
make typecheck
make build                # KiCad project in hardware/kicad/ (about 4 minutes)
make fab order            # gerbers in hardware/fab/, JLC BOM/CPL packages in hardware/fab/order/
```

Always check JLC's placement preview before ordering.

The committed `hardware/kicad/proptroller.kicad_pcb` is the reference board. Every signal and power net is locked (`routes.ts`) and the
placement is pinned, so a rebuild reproduces the same tracks and vias with the toolchain it was made with: KiCad 10.0.6, Freerouting 2.5.0
(`tools/freerouting.pin`), aieda v0.2.0-1-gcd1f38a (submodule). Other tool versions may change the GND fanout or pour fill: compare before replacing it.

## Wiring guides

New to wiring? [docs/wiring-guides/](docs/wiring-guides/README.md) has a starter kit (a button, a motion sensor, a lamp,
a fan, a pixel strip and a speaker on one desk) and short guides for the common hookups, each as an interactive 3D page
with narration ([docs/wiring-guides/html/index.html](docs/wiring-guides/html/index.html), works offline), a written page,
and a renderable video. Assembling the board into its case: [docs/build-guide/](docs/build-guide/index.html) (written for rev A and an earlier case:
where it differs, `hardware/README.md` and `enclosure/README.md` are right).

## Safety

Read **[SAFETY.md](SAFETY.md)** first: the project is provided as-is, with no warranty and no certification, and you build
and use it at your own risk.

The board is powered from **low-voltage DC**: use a listed power supply with its own over-current protection (≤10 A); the board has
no fuses. The recommended way to switch AC loads is **off the board**, with a listed relay-controlled outlet box driven at low voltage
([docs/ac-control.md](docs/ac-control.md)). Only the rev B relays may switch AC directly, up to **120 V AC 5 A (3 A inductive)** each, only
from a GFCI-protected outlet, with a 5 A fuse in each relay's COM wire, and in the AC case with its earth leads on both earth holes (H2, H3). That rating rests on the relay's own insulation and a design margin
(≥3.0 mm copper clearance and air-gap slots around the relay contacts), not a certification; fog fluid residue, condensation and dust
degrade it over time, so keep the board clean and dry (`hardware/README.md`). Rev A relays are rated ≤30 V DC only: **AC on rev A relays is experimental, for prove-out only, not considered safe, and recommended against until rev B**.
Every other terminal, and every 3.5 mm jack, is low-voltage DC only. The board does not measure or limit current: keep each load
within its terminal's rating ([hardware/README.md](hardware/README.md)).

## License

MIT, see [LICENSE](LICENSE). The licence covers everything in this repository: the software (firmware, tools, web
page), the hardware design files (`hardware/`, `enclosure/`) and the documentation. See also [SAFETY.md](SAFETY.md).
