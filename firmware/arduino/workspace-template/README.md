# My Props: a workspace for your own firmware

Use it where it is (`firmware/arduino/workspace-template` in the Specter Proptroller repository), or copy it
anywhere and then set `dir` under `[proptroller]` in its `platformio.ini` to the full path of the repository's
`firmware/arduino` folder (as it stands, `dir = ..` only works from inside `firmware/arduino`; a build that cannot
find the library says so). Open it in VS Code with the PlatformIO extension, pick a project in PlatformIO's env
selector (status bar), then Build (and Upload on an ESP32 DevKit or a plain Pico).

Each project is an Arduino sketch folder, `firmware/<name>/<name>.ino` (letters, digits, `-` and `_`, no spaces:
`my-ghost`, not `My Ghost`; extra `.cpp` / `.h` files may sit beside it), with a `sketch.yaml` that names its board
(without one it is built for the ESP32 DevKit); the env has the folder's name. Start a new one with

    python3 make_envs.py new my-ghost --board picow                      # an empty sketch
    python3 make_envs.py new my-ghost --board esp32dev --from 03_TimedSequence   # from a library example

(boards: `esp32dev`, `picow`, `pico2w`, `pico`; an ESP32 project also gets a copy of the shared flash map as
`partitions.csv`, which the Arduino IDE needs). After adding, renaming or removing a project by hand run
`python3 make_envs.py` (it rewrites the envs at the end of `platformio.ini`; the rest is yours). `effects/` holds
show scripts: paste one into the board's Script tab. To check them on the computer, run the repository's VS Code task "Check effect scripts" (or
"Check this effect script" on the open file): the board's own checker, `program --check FILE...` from `pio run -e sim`,
prints `FILE:LINE:COL: error: message` and VS Code lists each one in the Problems panel; click it to go to the line.

```
effects/        lamp and prop - lab room 1.txt, doorbell chime - front porch.txt   (show scripts)
firmware/       ghost-in-the-machine/ (ESP32 DevKit), tracking-babies/ (Pico W)   (setup() and loop() each)
platformio.ini  one env per project
```

A sketch needs no `prop.begin()` or `prop.update()`: the first `prop` call starts the board, which runs the phone
page, show scripts and updates by itself; `loop()` is yours (lib/proptroller/README.md).

First flash of a Pico W project (later updates go over the air from the phone page, Firmware tab, with the
project's `.pio/build/<project>/firmware.bin`). Upload is refused on these boards, and the build leaves no
`firmware.uf2` (alone it would not boot); the commands below run from this folder, with `..` standing for the
repository's `firmware/arduino` (your `dir`):

```sh
(cd .. && pio run -e boot_rp2040)    # the bootloader, built in firmware/arduino (Pico 2 W: boot_rp2350)
pio run -e tracking-babies
python3 ../tools/picoboot_image.py merge pico-w --app-build .pio/build --app-env tracking-babies
# hold BOOTSEL, plug in, copy .pio/build/picoboot-pico-w/flash.uf2 to the drive
```

**Arduino IDE / arduino-cli** (best effort): open the sketch folder; `sketch.yaml` pins the board package and the
libraries (`arduino-cli compile --profile <board>`; its `dir:` line points at the library inside the repository, so
fix it in a copied workspace). Building, uploading over USB, the Serial Monitor and the
Serial Plotter work. What you lose: on the ESP32 the shared partition table unless you copy
`firmware/partitions/esp32-4mb.csv` into the sketch folder as `partitions.csv` (without it the app slots and the
stores differ, so over-the-air updates and stored settings do not match the stock firmware); on the Pico W /
Pico 2 W the rollback bootloader (an IDE build is linked for 0x10000000: flash it over USB only).

## The exact layout (for tools)

- `firmware/<name>/<name>.ino`: one project per folder; `<name>` matches `[A-Za-z0-9][A-Za-z0-9_-]*`. Other
  `.cpp` / `.h` files in the folder belong to it. An optional `firmware/<name>/VERSION` holds its version.
- `firmware/<name>/sketch.yaml`: arduino-cli's profile (optional: without it the board is `esp32dev`); the first
  `fqbn:` picks the board:
  `esp32:esp32:esp32` (esp32dev), `rp2040:rp2040:rpipicow` (picow), `rp2040:rp2040:rpipico2w` (pico2w),
  `rp2040:rp2040:rpipico` (pico).
- `effects/*.txt`: show scripts, any file name.
- `platformio.ini`: hand-edited board sections `[board_<board>]`, then one `[env:<name>]` per project with
  `extends = board_<board>` and `custom_project = <name>`, written by `make_envs.py`.
- `project_src.py`: makes `firmware/<name>/` the env's source directory (PlatformIO converts `.ino` files only in
  the source directory) and defines `PROPTROLLER_FW_NAME="<name>"` (and `PROPTROLLER_FW_VERSION` from `VERSION`).
  A tool that builds each sketch in its own PlatformIO project (`src_dir` = the sketch folder) must define the
  same two flags.
