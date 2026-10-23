# PlatformIO post script of the fuzz environment: clang instead of gcc for every object (the sketch's sources and
# each library, whose construction environments are cloned before this script runs) and for the link; the
# sanitizer and libFuzzer flags are the environment's build_flags (platformio.ini), so they reach every object.
# PT_FUZZ_FLAGS adds compiler and linker flags, for a clang whose runtime is not where it looks, e.g.
# PT_FUZZ_FLAGS="-resource-dir=<dir with lib/linux/libclang_rt.fuzzer-x86_64.a> --gcc-install-dir=<a GCC with libstdc++>"
import os
import shlex

Import("env", "projenv")  # noqa: F821
LINK = ["-fsanitize=fuzzer,address,undefined"] + shlex.split(os.environ.get("PT_FUZZ_FLAGS", ""))
for e in [env, projenv] + [lb.env for lb in env.GetLibBuilders()]:  # noqa: F821
    e.Replace(CC="clang", CXX="clang++", LINK="clang++")
env.Append(LINKFLAGS=LINK)  # noqa: F821
