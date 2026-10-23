# PlatformIO post step of the environments behind the Pico bootloader (picow, pico2w) and of the bootloader
# itself (boot_rp2040, boot_rp2350): `pio run -t upload` is refused with a message instead of writing one image.
# The firmware is linked for 0x10007000, so written alone to a board without the bootloader it never runs; the
# bootloader written alone erases nothing above it but leaves no firmware. The first flash of a Pico W / Pico 2 W
# is always the merged flash.uf2 (PICO-BOOTLOADER.md "First flash"); later updates go over the air.
# The core's firmware.uf2 of these envs is deleted right after it is made: copied to a BOOTSEL drive it would write
# one image alone and boot nothing (the plain Pico's firmware.uf2 is not touched: it has no such script).
import os

Import("env")  # noqa: F821

MESSAGE = (
    "\n%s: `pio run -t upload` is not used on this board. The first flash is the merged file:\n"
    "  pio run -e %s -e %s && python3 tools/picoboot_image.py merge %s\n"
    "then hold BOOTSEL, plug the board in and copy .pio/build/picoboot-%s/flash.uf2 to the RPI-RP2 / RP2350 drive.\n"
    "After that, update over the air (the phone page's Firmware tab, or tools/ptclient.py update).\n"
)

pioenv = env.subst("$PIOENV")  # noqa: F821
board = "pico2-w" if env.BoardConfig().get("build.mcu") == "rp2350" else "pico-w"  # noqa: F821
boot_env, fw_env = ("boot_rp2350", "pico2w") if board == "pico2-w" else ("boot_rp2040", "picow")
text = MESSAGE % (pioenv, boot_env, fw_env, board, board)


def refuse(*_args, **_kwargs):
    print(text)
    env.Exit(1)  # noqa: F821


# both: the command never runs, and the pre-action stops before any upload step (port search included)
env.Replace(UPLOADCMD='"$PYTHONEXE" -c "import sys; sys.exit(1)"')  # noqa: F821
env.AddPreAction("upload", refuse)  # noqa: F821


def drop_uf2(*_args, **_kwargs):
    uf2 = env.subst("$BUILD_DIR/${PROGNAME}.uf2")  # noqa: F821
    if os.path.exists(uf2):
        os.remove(uf2)


# after the platform's own post action on the .elf (which writes firmware.uf2)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", drop_uf2)  # noqa: F821
