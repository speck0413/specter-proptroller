# From nothing to a running board

This page takes a computer with nothing installed to Specter Studio built from source, a blank ESP32 DevKit
running the stock firmware, and an effect script on it. The steps are for a Mac; Windows and Linux differences
are at the end.

You need: a 30-pin ESP32 DevKit (ESP32-WROOM-32, 4 MB), a USB **data** cable, and about an hour, most of it
waiting for downloads and the first build.

## 1. The tools (once per computer)

Open **Terminal** and paste these lines one block at a time.

```sh
xcode-select --install
```

A window opens: click **Install** and wait until it is done (git and Apple's compiler come with it).

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Homebrew asks for your Mac password. At the end it prints **Next steps** with two lines to paste (they put
`brew` on your PATH): paste them.

## 2. Get Specter Studio

```sh
mkdir -p ~/specter && cd ~/specter
git clone --recurse-submodules https://github.com/speck0413/specter-studio.git
cd specter-studio
```

## 3. Set up and start Studio

```sh
scripts/setup.sh
export PATH="$(brew --prefix rustup)/bin:$PATH"
echo 'export PATH="$(brew --prefix rustup)/bin:$PATH"' >> ~/.zprofile
npm run tauri dev
```

`scripts/setup.sh` checks and installs everything else: Node.js, Rust, esptool (Homebrew), PlatformIO in
`~/.platformio` (the official installer), libclang (Studio builds the board's file system image with the C
littlefs library, through bindgen: the Command Line Tools' libclang, else `brew install llvm` and the
`LIBCLANG_PATH` line it prints), the firmware submodule, the npm packages and the practice board. It
prints each thing it checks and installs, and it is safe to run again. `scripts/setup.sh --check` only looks and
installs nothing. Anything that needs your password it prints for you to run.

The two `PATH` lines put Homebrew's Rust (`cargo`, `rustc`) on the PATH, now and in new Terminal windows; run
them once. On Linux and Windows skip them (rustup puts itself on the PATH).

`npm run tauri dev` builds and opens the desktop app. The first build takes several minutes; later ones are quick.
Leave this Terminal window open while you use Studio.

## 4. The firmware Studio installs

Studio comes with the stock firmware for the ESP32 DevKit and the Pico W boards, and it also gets newer releases
from GitHub when the computer is online. To install a firmware you built yourself, see
[Building and flashing the firmware](FIRMWARE-BUILD.md).

## 5. Put the firmware on a blank ESP32 DevKit

1. Plug the DevKit into the Mac with the USB cable (the DevKit on its own, not in a board, or the board unpowered).
2. In Studio go to **Boards** and open **Add a board**.
3. Press **Put firmware on a new board (USB)**.
4. Firmware: **Generic (stock)**. Kind of board: **ESP32 board · Proptroller board**. USB port: the one that
   appeared when you plugged it in (`/dev/cu.usbserial-…` or `/dev/cu.wchusbserial…`).
5. Press **Install**, read the question and press **Install** again. It takes about a minute.

No port? Try another cable (many are charge-only), press **Look again**, and see "USB driver" below. Cannot
connect? Hold the DevKit's **BOOT** button while the install starts.

## 6. Connect, the phone page, and a script

**Connect from Studio.** In **Add a board**: **Plugged in with USB**, pick the port, **Connect**. Or join the
board's own Wi-Fi from the Mac and press **Connect through the board's own Wi-Fi**.

**The phone page.** On your phone, join the Wi-Fi **Proptroller-XXXX** (the last four characters differ per
board; the password is in [GETTING-STARTED.md](../GETTING-STARTED.md#5-open-the-boards-page-on-your-phone)). The
board always makes this network. Open **http://192.168.4.1/**: the Live tab shows every input and output.

**Send a script.** In Studio, open the board on **Boards**, type an effect script in its script box, for example

```
effect porch light:
  wait for trig1
  out1 = on
  wait 10
  fade out1 to 0% over 2
```

and press **Send to this board only**. Or type it in the phone page's **Script** tab, **Check** and **Save**.
Without a Proptroller board around the DevKit there are no TRIG jacks: on the DevKit alone, watch the Live tab
and use OUT1's test button; with the board, press a button on TRIG1 ([GETTING-STARTED.md](../GETTING-STARTED.md)
has the wiring).

## Without Studio: esptool

The first-flash image, written by hand: download `first-flash-esp32-devkit-0.1.0-rc.2.bin` from the
[firmware release](https://github.com/speck0413/specter-proptroller/releases), find the port with `ls /dev/cu.*`,
then:

```sh
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX write_flash 0x0 ~/Downloads/first-flash-esp32-devkit-0.1.0-rc.2.bin
```

(`esptool --port … write_flash 0x0 …` works the same if esptool is on your PATH.) Or straight from the source:
`cd firmware/firmware/arduino && ~/.platformio/penv/bin/pio run -e esp32dev -t upload`. Then go on with step 6
on the phone page.

Building, flashing and releasing the firmware in more detail: [FIRMWARE-BUILD.md](FIRMWARE-BUILD.md).

## USB driver

The ESP32 DevKit has a USB-serial chip next to its USB socket: **CP210x** (Silicon Labs) or **CH340** (WCH).

- **Mac:** macOS has drivers for both. If no port appears with a data cable, install the chip maker's driver
  (Silicon Labs CP210x VCP or WCH CH34x) and allow it in System Settings → Privacy & Security.
- **Linux:** the drivers are built in. Add yourself to the serial group once, then log out and in:
  `sudo usermod -aG dialout $USER` (`scripts/setup.sh` says if you need it).
- **Windows:** the port shows in Device Manager under Ports (COM…). No port or a yellow warning sign: install the
  chip maker's driver.

## Windows

- Install **Git for Windows** first (`winget install --id Git.Git -e`), then clone in **Git Bash** as in step 2.
- Instead of `scripts/setup.sh`, in PowerShell in the clone:
  `powershell -ExecutionPolicy Bypass -File scripts\setup.ps1` (checks and prints what is missing), then the same
  with `-Install` (installs the missing tools with winget, LLVM for libclang included, then the submodule and npm
  packages; it prints the `LIBCLANG_PATH` line to set). Windows asks
  for administrator approval for the Microsoft C++ Build Tools and some others; answer it yourself.
- Start Studio from **Git Bash**: `npm run tauri dev` (it runs a small `sh` script first).
- There is no practice board on Windows yet; real boards work.

## Linux

- `scripts/setup.sh` prints the one `sudo apt install …` (or dnf / pacman) line for Tauri's WebKitGTK packages
  and libclang (`libclang-dev`) if they are missing; run it, then `scripts/setup.sh` again. It installs rustup and PlatformIO into your home
  folder itself.
- Node.js 20 or newer: from https://nodejs.org or with nvm (`nvm install 22`); some distributions ship older ones.
- The serial group (above), then log out and in.
- Ports are `/dev/ttyUSB0` or `/dev/ttyACM0`.

## If something goes wrong

- **`scripts/setup.sh` says MISSING**: it prints the command for each; run them, then `scripts/setup.sh` again.
- **The firmware submodule did not check out**: in the `specter-studio` folder run
  `git submodule update --init --recursive`, then `scripts/setup.sh` again.
- **Studio's install says there is no USB install for this kind of board**: Settings → Firmware updates shows
  where firmware comes from; **Use the official releases** goes back to the firmware that comes with Studio and
  the GitHub releases.
- **`cargo` not found when `npm run tauri dev` starts** (Mac): run the two `PATH` lines from step 3.
