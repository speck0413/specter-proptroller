#!/usr/bin/env python3
"""Package a firmware release from finished builds (PROTOCOL.md "Releases"). Builds nothing and never writes to a
board: run it from firmware/arduino after

    pio run -e esp32dev -e pico -e picow -e pico2w -e boot_rp2040 -e boot_rp2350
    python3 tools/picoboot_image.py merge pico-w && python3 tools/picoboot_image.py merge pico2-w

    python3 tools/release.py OUT_DIR [--version 0.1.0] [--build-root DIR]

OUT_DIR/assets/ gets the release assets (attached to the GitHub Release):
  generic-esp32-devkit-<v>.bin, generic-pico-w-<v>.bin, generic-pico2-w-<v>.bin   update images (the board's page,
                                                                                   Firmware tab, or Specter Studio)
  first-flash-esp32-devkit-<v>.bin   bootloader, partition table, otadata and firmware merged, written from 0x0
  first-flash-pico-w-<v>.uf2, first-flash-pico2-w-<v>.uf2   bootloader and firmware, for the BOOTSEL drive
  first-flash-pico-<v>.uf2           the plain Pico's firmware.uf2 (no bootloader, no update image: it has no
                                     update target and is updated over USB with a newer first-flash file)
  proptroller-firmware.json          the manifest Specter Studio reads: the update images first, then the
                                     first-flash images ("first_flash": true)
OUT_DIR/flasher/ gets the browser flasher (ESP Web Tools): index.html, manifest.json and the first-flash ESP32 image,
served from one origin (GitHub Pages), because GitHub's release downloads send no CORS headers.

The version defaults to firmware/VERSION; --build-root names the folder holding <env>/ (default .pio/build). Unsigned: the SHA-256 in the manifest is a transfer check, not a signature.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
BUILD = os.path.join(PROJ, ".pio", "build")
PIO_HOME = os.environ.get("PLATFORMIO_CORE_DIR", os.path.join(os.path.expanduser("~"), ".platformio"))


def esp32_parts(build):
    """ESP32 first flash (FLASH-MAP.md): (offset, file)"""
    return [
        (0x1000, os.path.join(build, "esp32dev", "bootloader.bin")),
        (0x8000, os.path.join(build, "esp32dev", "partitions.bin")),
        (0xE000, os.path.join(PIO_HOME, "packages", "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")),
        (0x10000, os.path.join(build, "esp32dev", "firmware.bin")),
    ]


def images(build):
    """board (info.mcu) -> (update image or None, first-flash image or None)"""
    return {
        "esp32-devkit": (os.path.join(build, "esp32dev", "firmware.bin"), None),  # first flash: merged below
        "pico-w": (os.path.join(build, "picow", "firmware.bin"), os.path.join(build, "picoboot-pico-w", "flash.uf2")),
        "pico2-w": (os.path.join(build, "pico2w", "firmware.bin"), os.path.join(build, "picoboot-pico2-w", "flash.uf2")),
        # the plain Pico: USB only (no update capability), arduino-pico's own UF2 is the whole first flash
        "pico": (None, os.path.join(build, "pico", "firmware.uf2")),
    }
# a version at least two weeks old, pinned (the flasher page loads it from the CDN)
ESP_WEB_TOOLS = "https://unpkg.com/esp-web-tools@10.1.1/dist/web/install-button.js?module"


def die(msg):
    print("release.py: " + msg, file=sys.stderr)
    sys.exit(1)


def entry(path, mcu, first_flash=False):
    with open(path, "rb") as f:
        data = f.read()
    e = {"fw": "generic", "impl": "cpp", "mcu": mcu, "board": "proptroller", "file": os.path.basename(path), "bytes": len(data),
         "sha256": hashlib.sha256(data).hexdigest()}
    if first_flash:
        e["first_flash"] = True
    return e


def esptool():
    tool = os.path.join(PIO_HOME, "packages", "tool-esptoolpy", "esptool.py")
    if os.path.exists(tool):
        return [sys.executable, tool]
    return [sys.executable, "-m", "esptool"]


FLASHER_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Specter Proptroller flasher</title>
<script type="module" src="{esp_web_tools}"></script>
<style>
  :root {{ color-scheme: light dark; --bg: #fff; --fg: #1b1b1f; --muted: #5b5b66; }}
  @media (prefers-color-scheme: dark) {{ :root {{ --bg: #141418; --fg: #ececf1; --muted: #a0a0ad; }} }}
  body {{ background: var(--bg); color: var(--fg); font: 16px/1.5 system-ui, sans-serif; margin: 0 auto;
         max-width: 40rem; padding: 2rem 16px; }}
  small, .hint {{ color: var(--muted); }}
</style>
</head>
<body>
<h1>Specter Proptroller: flash an ESP32 DevKit</h1>
<p>Firmware {version}. Plug the ESP32 DevKit into this computer by USB (out of the board, or the board unpowered),
then press the button and pick its port (CP210x or CH340).</p>
<esp-web-install-button manifest="manifest.json">
  <span slot="unsupported">This browser cannot talk to USB: use Chrome or Edge on a computer.</span>
  <span slot="not-allowed">The page must be opened over https.</span>
</esp-web-install-button>
<p class="hint">No port? Install the USB driver (Silicon Labs CP210x or WCH CH340) and try another cable. Cannot
connect? Hold the DevKit's BOOT button while you connect. A Pico W or Pico 2 W is flashed by copying its
<code>first-flash-…uf2</code> file from the release onto its BOOTSEL drive (GETTING-STARTED.md).</p>
<p><small>The image is the stock firmware, unsigned. Releases:
<a href="https://github.com/speck0413/specter-proptroller/releases">github.com/speck0413/specter-proptroller</a></small></p>
</body>
</html>
"""


def main(argv):
    args = list(argv)
    opts = {"--version": None, "--build-root": BUILD}
    for opt in opts:
        if opt in args:
            i = args.index(opt)
            if i + 1 >= len(args):
                die(opt + " needs a value")
            opts[opt] = args[i + 1]
            del args[i:i + 2]
    version, build = opts["--version"], os.path.abspath(opts["--build-root"])
    if len(args) != 1 or args[0].startswith("-"):
        print(__doc__)
        return 2
    out = os.path.abspath(args[0])
    if version is None:
        version = open(os.path.join(PROJ, "..", "VERSION")).read().strip()
    parts, imgs = esp32_parts(build), images(build)
    for _, p in parts:
        if not os.path.exists(p):
            die(p + " is missing: build first (see the top of this file)")
    for upd, first in imgs.values():
        for p in (upd, first):
            if p and not os.path.exists(p):
                die(p + " is missing: build first (see the top of this file)")
    assets = os.path.join(out, "assets")
    flasher = os.path.join(out, "flasher")
    for d in (assets, flasher):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)

    updates, firsts = [], []
    for mcu, (upd, first) in imgs.items():
        if upd:
            dst = os.path.join(assets, "generic-%s-%s.bin" % (mcu, version))
            shutil.copyfile(upd, dst)
            updates.append(entry(dst, mcu))
        if first:
            dst = os.path.join(assets, "first-flash-%s-%s.uf2" % (mcu, version))
            shutil.copyfile(first, dst)
            firsts.append(entry(dst, mcu, first_flash=True))

    merged = os.path.join(assets, "first-flash-esp32-devkit-%s.bin" % version)
    # the flash mode and speed as the build made the bootloader (esp32dev: QIO 80 MHz, FLASH-MAP.md): its header says
    # DIO 80 MHz, the mode the ROM reads it in, and the QIO bootloader switches the chip to QIO itself. A header
    # that says QIO makes the ROM read the bootloader in QIO before the chip's quad mode is on: it never starts.
    cmd = esptool() + ["--chip", "esp32", "merge_bin", "-o", merged, "--flash_mode", "keep", "--flash_freq", "keep",
                       "--flash_size", "4MB"]
    for off, p in parts:
        cmd += ["0x%x" % off, p]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    firsts.insert(0, entry(merged, "esp32-devkit", first_flash=True))

    # update images first: a client that picks the first entry matching fw / impl / mcu gets the update image
    manifest = {"version": version, "images": updates + firsts}
    with open(os.path.join(assets, "proptroller-firmware.json"), "w") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")

    shutil.copyfile(merged, os.path.join(flasher, os.path.basename(merged)))
    web = {"name": "Specter Proptroller", "version": version, "new_install_prompt_erase": True,
           "builds": [{"chipFamily": "ESP32", "parts": [{"path": os.path.basename(merged), "offset": 0}]}]}
    with open(os.path.join(flasher, "manifest.json"), "w") as f:
        json.dump(web, f, indent=1)
        f.write("\n")
    with open(os.path.join(flasher, "index.html"), "w") as f:
        f.write(FLASHER_HTML.format(esp_web_tools=ESP_WEB_TOOLS, version=version))

    for name in sorted(os.listdir(assets)):
        print("%10d  assets/%s" % (os.path.getsize(os.path.join(assets, name)), name))
    print("flasher/: index.html, manifest.json, " + os.path.basename(merged))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
