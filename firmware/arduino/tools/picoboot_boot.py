# PlatformIO pre-build step of the Pico bootloader environments (boot_rp2040, boot_rp2350; PICO-BOOTLOADER.md).
#
# The bootloader is bare metal (no Arduino framework): its own start-up code (src/picoboot/boot_main.c), its own
# linker script (src/picoboot/boot_<chip>.ld), lib/picoboot for the boot decision, and the pico-sdk's flash,
# watchdog, XOSC and boot ROM code from the arduino-pico package that the firmware builds already install
# (framework-arduinopico: its pico-sdk headers and its prebuilt libpico.a). Nothing is downloaded.
import os

Import("env")  # noqa: F821

PROJ = env.subst("$PROJECT_DIR")  # noqa: F821
board = env.BoardConfig()  # noqa: F821
chip = board.get("build.mcu")
platform = env.PioPlatform()  # noqa: F821
fw = platform.get_package_dir("framework-arduinopico")
if not fw or not os.path.isdir(fw):
    raise SystemExit("picoboot_boot.py: the arduino-pico package is not installed: build a Pico firmware env first (pio run -e picow)")


def flags_from(path):
    out = []
    for line in open(path).read().split():
        line = line.strip()
        if line.startswith("-iwithprefixbefore"):
            out.append(os.path.join(fw, line[len("-iwithprefixbefore/"):]))
    return out


incs = flags_from(os.path.join(fw, "lib", chip, "platform_inc.txt")) + flags_from(os.path.join(fw, "lib", "core_inc.txt"))
incs.append(os.path.join(fw, "pico-sdk", "src", "common", "boot_picobin_headers", "include"))
incs.append(os.path.join(fw, "pico-sdk", "src", "rp2_common", "hardware_ticks", "include"))
defs = [d[2:] for d in open(os.path.join(fw, "lib", chip, "platform_def.txt")).read().split() if d.startswith("-D")]

if chip == "rp2040":
    cpu = ["-march=armv6-m", "-mcpu=cortex-m0plus", "-mthumb"]
else:
    cpu = ["-mcpu=cortex-m33", "-mthumb", "-march=armv8-m.main+fp+dsp", "-mfloat-abi=softfp"]

env.Append(  # noqa: F821
    CPPPATH=incs,
    CPPDEFINES=[tuple(d.split("=", 1)) if "=" in d else d for d in defs] + ["PICOBOOT_BOOTLOADER"],
    CCFLAGS=["-Os", "-ffunction-sections", "-fdata-sections", "-Wall", "-Werror=return-type"] + cpu,
    CFLAGS=["-std=gnu17"],
    ASFLAGS=cpu,
    LINKFLAGS=cpu + ["-nostartfiles", "-Wl,--gc-sections", "-Wl,--no-warn-rwx-segments",
                     "-Wl,--unresolved-symbols=report-all", "-Wl,-Map,${BUILD_DIR}/bootloader.map"],
    LIBS=[env.File(os.path.join(fw, "lib", chip, "libpico.a")), "gcc", "c"],  # noqa: F821
)
script = os.path.join(PROJ, "src", "picoboot", "boot_%s.ld" % chip)
board.update("build.ldscript", script)
env.Replace(LDSCRIPT_PATH=script)  # noqa: F821

# RP2040: boot2 (the flash set-up the boot ROM runs from the first 256 bytes), the board's own from the package
if chip == "rp2040":
    boot2 = board.get("build.arduino.earlephilhower.boot2_source", "boot2_w25q080_2_padded_checksum.S")
    env.BuildSources(os.path.join("$BUILD_DIR", "boot2"), os.path.join(fw, "boot2", chip), "-<*> +<%s>" % boot2)  # noqa: F821


def check(target, source, env):  # noqa: ARG001
    import subprocess
    import sys
    r = subprocess.run([sys.executable, os.path.join(PROJ, "tools", "picoboot_image.py"), "check-boot", env.subst("$PIOENV")])
    if r.returncode != 0:
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check)  # noqa: F821
