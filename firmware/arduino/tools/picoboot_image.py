#!/usr/bin/env python3
"""The Pico bootloader's images (PICO-BOOTLOADER.md): checks and the first-flash file. Builds only: this script
never writes to a board.

    python3 tools/picoboot_image.py check-app picow     # the firmware is linked for the active slot
    python3 tools/picoboot_image.py check-boot boot_rp2040   # the bootloader fits in front of the state sector
    python3 tools/picoboot_image.py merge pico-w             # bootloader + firmware -> one first-flash file
    python3 tools/picoboot_image.py plan pico-w              # the flash plan (offsets and sizes)

Options: --app-build DIR (the firmware's .pio/build, default this project's: a project of its own, such as one
made from workspace-template/, passes its own), --board pico-w|pico2-w (check-app of an environment with another
name), --app-env NAME (merge: the firmware env, default picow / pico2w).

`merge` writes .pio/build/picoboot-<board>/flash.bin (from 0x10000000: the bootloader, 0xFF up to the active
slot, the firmware) and flash.uf2 (the same, for the BOOTSEL drive). Both cover the state sector (erased: a USB
flash always starts from plain boot, never finishes an old swap or revert over the new image); the dfu slot and
LittleFS are not in them, so a USB flash leaves the stored records as they are. Run from firmware/arduino after
`pio run -e boot_rp2040 -e picow` (Pico W) or `-e boot_rp2350 -e pico2w` (Pico 2 W).
"""
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
BUILD = os.path.join(PROJ, ".pio", "build")

BOARDS = {
    # board: (chip, bootloader env, firmware env, RAM end, UF2 family)
    "pico-w": ("RP2040", "boot_rp2040", "picow", 0x20042000, 0xE48BFF56),
    "pico2-w": ("RP2350", "boot_rp2350", "pico2w", 0x20082000, 0xE48BFF59),
}
CHIP_OF_ENV = {"picow": "pico-w", "pico2w": "pico2-w", "boot_rp2040": "pico-w", "boot_rp2350": "pico2-w"}
OPTS = {"app_build": BUILD, "board": None, "app_env": None}


def map_values():
    text = open(os.path.join(PROJ, "lib", "proptroller", "src", "proptroller", "picoboot_map.h")).read()
    return {k: int(v, 16) for k, v in re.findall(r"#define PICOBOOT_(\w+) (0x[0-9A-Fa-f]+)u", text)}


def layout(board):
    m = map_values()
    chip = BOARDS[board][0]
    flash, active_len = m[chip + "_FLASH"], m[chip + "_ACTIVE_LEN"]
    dfu = m["ACTIVE_OFFSET"] + active_len
    fs = flash - m["EEPROM_LEN"] - m["FS_LEN"]
    return [
        ("boot2 + bootloader" if chip == "RP2040" else "bootloader", m["BOOT_OFFSET"], m["BOOT_LEN"]),
        ("bootloader state", m["STATE_OFFSET"], m["STATE_LEN"]),
        ("active (firmware)", m["ACTIVE_OFFSET"], active_len),
        ("dfu (download)", dfu, active_len + m["SECTOR"]),
        ("LittleFS", fs, m["FS_LEN"]),
        ("EEPROM sector", flash - m["EEPROM_LEN"], m["EEPROM_LEN"]),
    ], flash


def fail(msg):
    sys.stderr.write("picoboot_image: %s\n" % msg)
    sys.exit(1)


def read_bin(env, build=BUILD):
    p = os.path.join(build, env, "firmware.bin")
    if not os.path.exists(p):
        fail("%s not built (pio run -e %s)" % (p, env))
    return open(p, "rb").read()


def objdump():
    pkgs = os.environ.get("PLATFORMIO_PACKAGES_DIR", os.path.join(os.path.expanduser("~"), ".platformio", "packages"))
    return os.path.join(pkgs, "toolchain-rp2040-earlephilhower", "bin", "arm-none-eabi-objdump")


def check_app(env):
    board = OPTS["board"] or CHIP_OF_ENV.get(env)
    if board not in BOARDS:
        fail("%s: which board? (--board pico-w or pico2-w)" % env)
    regions, flash = layout(board)
    _, active, active_len = regions[2]
    base, ram_end = 0x10000000 + active, BOARDS[board][3]
    data = read_bin(env, OPTS["app_build"])
    sp, reset = struct.unpack_from("<II", data, 0)
    if not (0x20000000 < sp <= ram_end):
        fail("%s: stack pointer 0x%08X is not in RAM: not a firmware image" % (env, sp))
    if not (reset & 1 and base + 8 <= (reset & ~1) < base + active_len):
        fail("%s: reset vector 0x%08X is not in the active slot 0x%08X: not linked for the bootloader" % (env, reset, base))
    if len(data) > active_len:
        fail("%s: %d B does not fit the %d B active slot" % (env, len(data), active_len))
    # every loaded section lies in the active slot (none in front of the vector table, none past the slot)
    elf = os.path.join(OPTS["app_build"], env, "firmware.elf")
    od = objdump()
    if os.path.exists(od) and os.path.exists(elf):
        out = subprocess.run([od, "-h", elf], capture_output=True, text=True, check=True).stdout
        for m in re.finditer(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+\S+\n\s+(.*)$", out, re.M):
            name, size, lma, flags = m.group(1), int(m.group(2), 16), int(m.group(4), 16), m.group(5)
            if "LOAD" not in flags or size == 0 or not (0x10000000 <= lma < 0x11000000):
                continue
            if lma < base or lma + size > base + active_len:
                fail("%s: section %s at 0x%08X-0x%08X is outside the active slot" % (env, name, lma, lma + size))
    print("picoboot_image: %s: %d B, %.1f %% of the %d B active slot at 0x%08X" % (env, len(data), 100.0 * len(data) / active_len, active_len, base))


def check_boot(env):
    board = CHIP_OF_ENV[env]
    regions, _ = layout(board)
    boot_len = regions[0][2]
    data = read_bin(env)
    if len(data) > boot_len:
        fail("%s: %d B does not fit the %d B in front of the state sector" % (env, len(data), boot_len))
    vt = 0x100 if board == "pico-w" else 0
    sp, reset = struct.unpack_from("<II", data, vt)
    if not (0x20000000 < sp <= BOARDS[board][3]) or not (reset & 1 and 0x10000000 + vt <= (reset & ~1) < 0x10000000 + boot_len):
        fail("%s: no vector table at 0x%08X" % (env, 0x10000000 + vt))
    if board == "pico2-w" and struct.pack("<I", 0xFFFFDED3) not in data[:4096]:
        fail("%s: no image definition in the first 4 KB (the RP2350 boot ROM would not boot it)" % env)
    print("picoboot_image: %s: %d B of %d B" % (env, len(data), boot_len))


def uf2(data, base, family, keep_below):
    """UF2 blocks of data; blocks that are all 0xFF are left out, except below keep_below (the bootloader and its
    state sector: the boot ROM then erases the state, so a USB flash always starts from plain boot)."""
    blocks = [data[i:i + 256] for i in range(0, len(data), 256)]
    out = bytearray()
    for n, chunk in enumerate(blocks):
        if base + 256 * n >= keep_below and chunk.count(0xFF) == len(chunk):
            continue  # erased: nothing to write
        chunk = chunk.ljust(256, b"\xff")
        out += struct.pack("<IIIIIIII", 0x0A324655, 0x9E5D5157, 0x00002000, base + 256 * n, 256, 0, 0, family)
        out += chunk + b"\x00" * (476 - 256) + struct.pack("<I", 0x0AB16F30)
    count = len(out) // 512
    for i in range(count):  # block numbers and the total
        struct.pack_into("<II", out, 512 * i + 20, i, count)
    return bytes(out)


def merge(board):
    chip, boot_env, app_env, _, family = BOARDS[board]
    app_env = OPTS["app_env"] or app_env
    OPTS["board"] = OPTS["board"] or board
    check_boot(boot_env)
    check_app(app_env)
    regions, _ = layout(board)
    boot, app = read_bin(boot_env), read_bin(app_env, OPTS["app_build"])
    active = regions[2][1]
    image = boot + b"\xff" * (active - len(boot)) + app
    out_dir = os.path.join(OPTS["app_build"], "picoboot-" + board)
    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, "flash.bin"), "wb").write(image)
    u = uf2(image, 0x10000000, family, 0x10000000 + active)
    # the state sector must be in it (a re-flash over a board with a marked or pending update would otherwise be
    # swapped or reverted at the next power-on)
    state_off = regions[1][1]
    covered = {struct.unpack_from("<I", u, i + 12)[0] for i in range(0, len(u), 512)}
    for a in range(0x10000000 + state_off, 0x10000000 + state_off + regions[1][2], 256):
        if a not in covered:
            fail("flash.uf2 does not cover the state sector at 0x%08X" % a)
    open(os.path.join(out_dir, "flash.uf2"), "wb").write(u)
    print("picoboot_image: %s: flash.bin %d B (bootloader %d B at 0x10000000, firmware %d B at 0x%08X), flash.uf2" % (
        board, len(image), len(boot), len(app), 0x10000000 + active))


def plan(board):
    regions, flash = layout(board)
    print("%s (%s, %d MB)" % (board, BOARDS[board][0], flash // (1024 * 1024)))
    for name, off, size in regions:
        print("  %-20s 0x%08X  0x%06X  %9d B" % (name, 0x10000000 + off, size, size))


def main():
    args = sys.argv[1:]
    for opt in ("--app-build", "--board", "--app-env"):
        if opt in args:
            i = args.index(opt)
            if i + 1 >= len(args):
                print(__doc__)
                return 2
            OPTS[opt[2:].replace("-", "_")] = os.path.abspath(args[i + 1]) if opt == "--app-build" else args[i + 1]
            del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    cmd, what = args
    {"check-app": check_app, "check-boot": check_boot, "merge": merge, "plan": plan}[cmd](what)
    return 0


if __name__ == "__main__":
    sys.exit(main())
