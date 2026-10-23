# PlatformIO pre-build script: the strict warnings, as errors, for this project's own sources only (src/ and
# lib/proptroller), never for the Arduino cores or the libraries we use. Every environment (esp32dev, pico, picow,
# pico2w, native, sim, bench) loads it (platformio.ini).
#
# -Wframe-larger-than=1024 (boards only): the largest frame of our code is about 800 B on the ESP32 (Controller::handle,
# mqttOnMessage); tools/stack_check.py budgets the worst paths at about 4 KB of the 8 KB core-0 / 12 KB loop
# stacks, so one frame over 1 KB is a new big local that belongs on the heap.
import os
import subprocess

Import("env")  # noqa: F821

STRICT = [
    "-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wconversion", "-Wnull-dereference", "-Wformat=2",
    "-Wimplicit-fallthrough", "-Wswitch-enum", "-Wframe-larger-than=1024",
]
GCC_ONLY = ["-Wduplicated-cond", "-Wlogical-op"]

proj = env.subst("$PROJECT_DIR")  # noqa: F821
ours = [os.path.join(proj, "src") + os.sep, os.path.join(proj, "lib", "proptroller") + os.sep, os.path.join(proj, "lib", "picoboot") + os.sep,
        os.path.join(proj, "test") + os.sep]
cc = env.subst("$CC")  # noqa: F821


def is_clang(cc):
    # by what it is, not its name: macOS's `gcc` is Apple clang
    if "clang" in cc:
        return True
    try:
        return "clang" in subprocess.run([cc, "--version"], capture_output=True, text=True, check=False).stdout
    except OSError:
        return False


# clang's -Wconversion also turns on -Wsign-conversion, which GCC's does not for C++: off, so a clang host (macOS)
# checks what the GCC builds check
CLANG_ONLY = ["-Wno-sign-conversion"]
flags = STRICT + (CLANG_ONLY if is_clang(cc) else GCC_ONLY)
if env.subst("$PIOPLATFORM") == "native":  # noqa: F821
    # the host (64-bit, an 8 MB stack, tests that keep whole simulated boards in locals): no frame limit there;
    # the limit is for the boards' stacks
    # and no -Wnull-dereference: at the host's -O2 GCC reports it inside ArduinoJson's MemoryPoolList (inlined
    # into our code), not in ours; the boards (-Os) keep it
    flags = [f for f in flags if not f.startswith("-Wframe-larger-than") and f != "-Wnull-dereference"]
if env.subst("$PIOENV") in ("native_asan", "fuzz"):  # noqa: F821
    # sanitizer and fuzz builds (-O1, instrumented) are for running: GCC reports flow warnings inside the
    # libraries' inlined code there; the warnings gate is the normal builds
    flags = [f for f in flags if f != "-Werror"]
if os.environ.get("PT_WARNINGS_ONLY"):  # list every warning without stopping at the first file
    flags = [f for f in flags if f != "-Werror"]



def strict(build_env, node):
    path = node.srcnode().get_abspath()
    if not any(path.startswith(o) for o in ours):
        return node
    # the cores' and libraries' headers as system headers: their warnings are not ours (GCC ignores a -I that is
    # also given as -isystem)
    system = []
    for inc in build_env.get("CPPPATH", []):
        d = build_env.subst(str(inc))
        if d and not any(os.path.abspath(d).startswith(o.rstrip(os.sep)) for o in ours):
            system += ["-isystem", d]
    return build_env.Object(node, CCFLAGS=build_env["CCFLAGS"] + system + flags)


env.AddBuildMiddleware(strict)  # noqa: F821
