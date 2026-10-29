# PlatformIO post-build step of the board environments: tools/stack_check.py for this environment (the worst
# stack paths against each task's stack with a 50 % margin); the build fails when one does not fit.
import os
import subprocess
import sys

Import("env")  # noqa: F821


def check(target, source, env):  # noqa: ARG001
    proj = env.subst("$PROJECT_DIR")
    # the build root (PLATFORMIO_BUILD_DIR or .pio/build): the ESP32's check reads the Pico W's call graph next to its own
    cmd = [sys.executable, os.path.join(proj, "tools", "stack_check.py"), "--env", env.subst("$PIOENV"),
           "--build-dir", env.subst("$PROJECT_BUILD_DIR")]
    r = subprocess.run(cmd, cwd=proj)
    if r.returncode != 0:
        sys.stderr.write("stack check failed (tools/stack_check.py --report --env %s)\n" % env.subst("$PIOENV"))
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check)  # noqa: F821
