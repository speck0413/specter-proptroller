#!/usr/bin/env python3
"""Tests of tools/release.py on a made-up build tree (no PlatformIO, no esptool):

    python3 -m unittest discover -s tools -p 'test_*.py'      (from firmware/arduino)

The merge step is replaced by a stand-in that concatenates the parts, so the test checks what release.py lists,
copies and names, not esptool itself (the release workflow and FIRMWARE-BUILD.md run the real one)."""
import hashlib
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import release  # noqa: E402

FAKE_ESPTOOL = """import sys
a = sys.argv[1:]
out = a[a.index("-o") + 1]
files = [x for x in a if not x.startswith("-") and not x.startswith("0x") and x not in ("esp32", "keep", "4MB", "merge_bin", out)]
with open(out, "wb") as f:
    for p in files:
        with open(p, "rb") as src:
            f.write(src.read())
"""


def read(path):
    with open(path, "rb") as f:
        return f.read()


def load_json(path):
    with open(path) as f:
        return json.load(f)


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.build = os.path.join(t, "build")
        self.files = {
            "esp32dev/bootloader.bin": b"boot",
            "esp32dev/partitions.bin": b"parts",
            "esp32dev/firmware.bin": b"esp32 firmware",
            "picow/firmware.bin": b"pico w firmware",
            "picoboot-pico-w/flash.uf2": b"pico w first flash",
            "pico2w/firmware.bin": b"pico 2 w firmware",
            "picoboot-pico2-w/flash.uf2": b"pico 2 w first flash",
            "pico/firmware.uf2": b"plain pico uf2",
        }
        for rel, data in self.files.items():
            self.write(os.path.join(self.build, rel), data)
        pio = os.path.join(t, "pio")
        self.write(os.path.join(pio, "packages", "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin"), b"otadata")
        fake = os.path.join(t, "fake_esptool.py")
        self.write(fake, FAKE_ESPTOOL.encode())
        self.saved = (release.PIO_HOME, release.esptool)
        release.PIO_HOME = pio
        release.esptool = lambda: [sys.executable, fake]
        self.out = os.path.join(t, "out")

    def tearDown(self):
        release.PIO_HOME, release.esptool = self.saved
        self.tmp.cleanup()

    @staticmethod
    def write(path, data):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)

    def run_release(self, *extra):
        with redirect_stdout(io.StringIO()):
            return release.main([self.out, "--version", "9.8.7-rc.1", "--build-root", self.build, *extra])

    def test_assets_and_manifest(self):
        self.assertEqual(self.run_release(), 0)
        assets = os.path.join(self.out, "assets")
        self.assertEqual(sorted(os.listdir(assets)), sorted([
            "generic-esp32-devkit-9.8.7-rc.1.bin", "generic-pico-w-9.8.7-rc.1.bin", "generic-pico2-w-9.8.7-rc.1.bin",
            "first-flash-esp32-devkit-9.8.7-rc.1.bin", "first-flash-pico-w-9.8.7-rc.1.uf2",
            "first-flash-pico2-w-9.8.7-rc.1.uf2", "first-flash-pico-9.8.7-rc.1.uf2", "proptroller-firmware.json"]))
        m = load_json(os.path.join(assets, "proptroller-firmware.json"))
        self.assertEqual(m["version"], "9.8.7-rc.1")
        imgs = m["images"]
        # the update images first, then the first-flash ones (a client taking the first match gets the update)
        firsts = [bool(i.get("first_flash")) for i in imgs]
        self.assertEqual(firsts, sorted(firsts))
        self.assertEqual([(i["mcu"], bool(i.get("first_flash"))) for i in imgs], [
            ("esp32-devkit", False), ("pico-w", False), ("pico2-w", False),
            ("esp32-devkit", True), ("pico-w", True), ("pico2-w", True), ("pico", True)])
        for i in imgs:
            data = read(os.path.join(assets, i["file"]))
            self.assertEqual((i["fw"], i["impl"], i["board"]), ("generic", "cpp", "proptroller"))
            self.assertEqual((i["bytes"], i["sha256"]), (len(data), hashlib.sha256(data).hexdigest()))

    def test_plain_pico_is_first_flash_only(self):
        self.run_release()
        assets = os.path.join(self.out, "assets")
        pico = [i for i in load_json(os.path.join(assets, "proptroller-firmware.json"))["images"] if i["mcu"] == "pico"]
        # no update image: the plain Pico has no update capability (PROTOCOL.md), only its UF2 for the BOOTSEL drive
        self.assertEqual(len(pico), 1)
        self.assertTrue(pico[0]["first_flash"])
        self.assertEqual(read(os.path.join(assets, pico[0]["file"])), self.files["pico/firmware.uf2"])
        self.assertFalse(any(f.startswith("generic-pico-9") for f in os.listdir(assets)))

    def test_flasher(self):
        self.run_release()
        flasher = os.path.join(self.out, "flasher")
        self.assertEqual(sorted(os.listdir(flasher)), ["first-flash-esp32-devkit-9.8.7-rc.1.bin", "index.html", "manifest.json"])
        web = load_json(os.path.join(flasher, "manifest.json"))
        self.assertEqual(web["builds"][0]["parts"], [{"path": "first-flash-esp32-devkit-9.8.7-rc.1.bin", "offset": 0}])
        merged = read(os.path.join(flasher, "first-flash-esp32-devkit-9.8.7-rc.1.bin"))
        self.assertEqual(merged, b"boot" + b"parts" + b"otadata" + b"esp32 firmware")
        self.assertIn(b"9.8.7-rc.1", read(os.path.join(flasher, "index.html")))

    def test_a_missing_build_is_refused(self):
        os.remove(os.path.join(self.build, "pico", "firmware.uf2"))
        with self.assertRaises(SystemExit) as e, redirect_stdout(io.StringIO()):
            release.main([self.out, "--version", "1.0.0", "--build-root", self.build])
        self.assertEqual(e.exception.code, 1)
        self.assertFalse(os.path.exists(os.path.join(self.out, "assets")))


if __name__ == "__main__":
    unittest.main()
