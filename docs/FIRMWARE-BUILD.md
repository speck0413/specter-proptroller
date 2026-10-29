# Build the firmware and put it on a board

This page builds the stock firmware from source, puts it on a board over USB, makes a release folder, and
updates boards over Wi-Fi. It assumes PlatformIO is installed: `tools/setup.sh` in this repository, or
Specter Studio's `studio/scripts/setup.sh` ([DEV-SETUP.md](DEV-SETUP.md)). Every command runs in `firmware/arduino`.

If `pio` is not found, use its full path: `~/.platformio/penv/bin/pio` (Windows:
`~/.platformio/penv/Scripts/pio.exe` in Git Bash), and `~/.platformio/penv/bin/python` for the Python tools.

## 1. Build

```sh
cd firmware/arduino
pio run -e esp32dev      # ESP32 DevKit (30-pin, ESP32-WROOM-32, 4 MB)
pio run -e picow         # Raspberry Pi Pico W
pio run -e pico2w        # Raspberry Pi Pico 2 W
pio run -e pico          # plain Pico (no Wi-Fi, updated over USB only)
pio run -e esp32dev_generic -e esp32s3_generic -e esp32c3_supermini   # bare devkits, no Proptroller board
```

The last line builds the bare-devkit images (an ESP32 30-pin DevKit, an ESP32-S3 DevKit, an ESP32-C3 SuperMini on
their own or on a breakout board): [firmware/BARE-DEVKIT.md](../firmware/BARE-DEVKIT.md) has their wiring.

The first build of each downloads its toolchain (a few minutes). The result is in `.pio/build/<env>/`
(`firmware.bin`). In VS Code the same is **Terminal > Run Task > Build all
boards**, or the PlatformIO toolbar's check-mark button for the selected env.

## 2. Put it on a board over USB (from source)

Plug the microcontroller in by USB on its own (out of the Proptroller board, or the board unpowered).

- **ESP32 DevKit:** `pio run -e esp32dev -t upload` (VS Code: the PlatformIO toolbar's arrow button). It writes
  the bootloader, the partition table and the firmware. No port found: see "Ports" below. Cannot connect: hold
  the DevKit's **BOOT** button while it says "Connecting…".
- **Plain Pico:** `pio run -e pico -t upload`. The first time, hold **BOOTSEL** while you plug it in.
- **Pico W / Pico 2 W:** the first time they need the bootloader as well, so use the first-flash file
  (section 4): build it with the VS Code task **Pico W / Pico 2 W first-flash file**, or
  `pio run -e picow -e boot_rp2040 && python tools/picoboot_image.py merge pico-w`
  (Pico 2 W: `-e pico2w -e boot_rp2350` and `merge pico2-w`); the file is
  `.pio/build/picoboot-pico-w/flash.uf2` (`picoboot-pico2-w`). After that, update over Wi-Fi (section 5).

## 3. A release folder

A release is the set of files people install. Build everything, then package it:

```sh
pio run -e esp32dev -e pico -e picow -e pico2w -e boot_rp2040 -e boot_rp2350 -e esp32dev_generic -e esp32s3_generic -e esp32c3_supermini
python tools/picoboot_image.py merge pico-w
python tools/picoboot_image.py merge pico2-w
python tools/release.py ~/specter/release --version 0.1.0-rc.3
```

`--version` defaults to `firmware/VERSION`. `~/specter/release/assets/` then holds:

| File | What it is | Used for |
|---|---|---|
| `generic-esp32-devkit-<v>.bin`, `generic-pico-w-<v>.bin`, `generic-pico2-w-<v>.bin` | **update images**: the firmware alone | updates over Wi-Fi (section 5) |
| `first-flash-esp32-devkit-<v>.bin` | **first-flash image**: bootloader, partition table, otadata and firmware in one file, written from address 0x0 | a new ESP32 over USB (section 4, Studio, the browser flasher) |
| `first-flash-pico-w-<v>.uf2`, `first-flash-pico2-w-<v>.uf2` | **first-flash image** for the Pico's BOOTSEL drive: bootloader and firmware | a new Pico W / Pico 2 W (section 4) |
| `first-flash-pico-<v>.uf2` | the plain Pico's firmware for its BOOTSEL drive (no bootloader; there is no update image for it) | a plain Pico, new or updated over USB |
| `generic-<bare>-<v>.bin`, `first-flash-<bare>-<v>.bin`, `<bare>` = `esp32-devkit-bare`, `esp32-s3-devkit`, `esp32-c3-supermini` | the bare devkits' update and first-flash images (the S3's and C3's bootloader is at 0x0 inside the file) | a devkit with no Proptroller board ([BARE-DEVKIT.md](../firmware/BARE-DEVKIT.md)) |
| `proptroller-firmware.json` | **the manifest**: the version and, per image, `fw` (`generic`), `mcu` (`esp32-devkit`, `pico-w`, `pico2-w`, `pico`, `esp32-s3`, `esp32-c3`), `board` (the board package it starts on: `proptroller`, or the bare devkit's id; the Proptroller board's entry comes first for each `mcu`), the file, its size and SHA-256, and `first_flash: true` on the first-flash images | what Specter Studio reads to pick the right image for each board |

`~/specter/release/flasher/` is the browser flasher page (ESP Web Tools) for the same version: one button for the
Proptroller board's ESP32 (`manifest.json`) and one for the bare devkits (`manifest-bare.json`, one image per chip
family: the flasher picks the ESP32, ESP32-S3 or ESP32-C3 one by the chip it finds).

Specter Studio can use this folder instead of GitHub: turn on **Advanced** (top bar), then **Settings → Firmware
updates → Folder…**, choose the `assets` folder, **Use it**.

## 4. Flash a first-flash image by hand

### ESP32 DevKit with esptool

**Back up first** if the board has anything on it you want to keep. This reads all 4 MB (settings, scripts and
the Wi-Fi details too, so keep the file private):

```sh
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32 --port PORT read_flash 0 0x400000 backup.bin
```

Then write the first-flash image from address 0x0:

```sh
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32 --port PORT write_flash 0x0 first-flash-esp32-devkit-<v>.bin
```

(Windows, in Git Bash: `~/.platformio/penv/Scripts/python.exe`. With esptool on your PATH:
`esptool --port PORT write_flash 0x0 …`.) To go back: `write_flash 0x0 backup.bin`.

The image runs the flash at QIO 80 MHz. Its bootloader's header says DIO 80 MHz, the mode the chip's ROM reads
it in, and the bootloader turns quad mode on itself: write it as it is (no `--flash_mode`; esptool keeps the
image's own setting). `--flash_mode qio` would make the ROM read the bootloader in quad mode before quad mode is
on, and the board would restart over and over until it is flashed again.

**Ports** (PORT above):

| Computer | Port name | Find it |
|---|---|---|
| Mac | `/dev/cu.usbserial-XXXX` or `/dev/cu.wchusbserialXXXX` | `ls /dev/cu.*` before and after plugging in |
| Linux | `/dev/ttyUSB0` (sometimes `/dev/ttyACM0`) | `ls /dev/ttyUSB* /dev/ttyACM*`; you need the `dialout` group (`sudo usermod -aG dialout $USER`, then log out and in) |
| Windows | `COM3`, `COM4`, … | Device Manager, Ports (COM & LPT) |

No port at all: try another cable (many are charge-only), then install the driver for the chip next to the
DevKit's USB socket (Silicon Labs CP210x or WCH CH340).

### Pico W / Pico 2 W: drag and drop

1. Hold **BOOTSEL** on the Pico while you plug in the USB cable, then let go. A drive called `RPI-RP2` (Pico 2 W:
   `RP2350`) appears.
2. Copy `first-flash-pico-w-<v>.uf2` (Pico 2 W: `first-flash-pico2-w-<v>.uf2`) onto it.
3. The Pico restarts by itself when the copy is done, and the drive disappears. Settings, shows and the script
   are kept.

## 5. Updates over Wi-Fi

Once a board runs the firmware, later versions go over Wi-Fi with the **update image** (`generic-<board>-<v>.bin`).
Settings, shows and the script are kept.

- **The board's page:** open it (http://192.168.4.1/ on the board's Wi-Fi `Proptroller-XXXX`, or its address on
  your Wi-Fi), **Firmware** tab, choose the `generic-…bin` for your board, and start the update.
- **Specter Studio:** a board's card says when a newer version is in the release list (GitHub, or the folder
  from section 3) and updates it with one click.

The ESP32 writes its other firmware slot; a Pico W / Pico 2 W takes about 16 s at the restart. A new image that
does not pass its 30-second self-check rolls back to the one before, and the page says `rolled_back`. The plain
Pico is updated over USB only (section 2).

## 6. What CI does on a `v*` tag

Pushing a tag `v<firmware/VERSION>` (for example `v0.1.0-rc.3`) runs `.github/workflows/firmware-release.yml`
(Specter Studio is released from the same repository with `studio-v<version>` tags, which this workflow does not
match; `studio/docs/BUILDING.md`):

1. checks that the tag matches `firmware/VERSION`;
2. builds `esp32dev`, `pico`, `picow`, `pico2w` and the two Pico bootloaders, and merges the Pico W / Pico 2 W
   first-flash files;
3. packages them with `tools/release.py` (section 3);
4. makes a **draft** GitHub Release with every file from `assets/` (a pre-release when the version has a suffix
   such as `-rc.2`). Someone publishes it by hand after a look: Specter Studio and the phone page see only
   published releases;
5. publishes the browser flasher to GitHub Pages.

The images are unsigned: the SHA-256 in the manifest checks the download, it is not a signature.

Every push and pull request that changes `firmware/` runs `.github/workflows/firmware.yml` (nothing is
published); it also runs Studio's tests and e2e (`.github/workflows/studio.yml`), since Studio builds its practice
board from `firmware/` and checks scripts against the golden sets:

| Job | What it runs |
|---|---|
| host tests | `pio test -e native`, the `tools/release.py` tests (`python -m unittest discover -s tools -p 'test_*.py'`) |
| board builds | `pio run` for `esp32dev`, `pico`, `picow`, `pico2w`, the two bootloaders and the three bare devkits, the Pico W / Pico 2 W merge, then `tools/release.py` into a scratch folder (the release packaging without the tag); the folder is kept as a workflow artifact for 7 days |
| examples | `tools/build-examples.sh`: every example sketch on the ESP32 DevKit and the Pico W, the board support package example, every workspace project on every board |
| simulator and phone page | `tools/check-host.sh` with Chrome (`CHROMIUM=/usr/bin/google-chrome`, on GitHub's Ubuntu runners): the protocol client, MQTT, an update, `program --check`, the take / blocks unit tests and every browser test of `firmware/web-test`, each on a fresh simulated board |

PlatformIO's packages are cached per job (key: the hash of `platformio.ini`).

## 7. Board packages

The firmware knows which board it is on from its **board package**: one header, `proptroller_board.h`, with the
GPIO of every terminal. The Proptroller board's is the default, and it reports `board: "proptroller"`, which is
what the manifest's `board` field and Specter Studio match images on. Another board (your own PCB, other pins) is
built with `build_flags = -I path/to/your-bsp`; the examples are `firmware/arduino/bsp/relay4-button/` and the
bare devkits' BSPs (`bsp/esp32-devkit-bare/`, `bsp/esp32-s3-devkit/`, `bsp/esp32-c3-supermini/`, each commented
line by line for adapting it: [BARE-DEVKIT.md](../firmware/BARE-DEVKIT.md)), and
[the library's GOING-FURTHER.md](../firmware/arduino/lib/proptroller/GOING-FURTHER.md) "Make your own board" has the
details.
