# PlatformIO pre-build script (sim): espMqttClient 1.7.3 gates its POSIX client (sockets, std::mutex, steady_clock)
# on __linux__ only, so on macOS it stops at "#error Target platform not supported". That code is plain BSD sockets
# and builds on macOS once `::htons` / `::htonl` lose the `::` (the sim ignores SIGPIPE itself: net/posix.cpp), so
# the installed copy is opened to __APPLE__ too. Idempotent; a no-op off macOS and once the library supports it.
import os
import sys

Import("env")  # noqa: F821

if sys.platform == "darwin":
    root = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "espMqttClient", "src")  # noqa: F821
    for d, _, files in os.walk(root):
        for f in files:
            if not f.endswith((".h", ".cpp")):
                continue
            path = os.path.join(d, f)
            with open(path, encoding="utf-8") as fh:
                text = fh.read()
            patched = text if "defined(__APPLE__)" in text else \
                text.replace("defined(__linux__)", "(defined(__linux__) || defined(__APPLE__))")
            # htons / htonl are macros on macOS, so `::htons(...)` does not parse
            patched = patched.replace("::htons(", "htons(").replace("::htonl(", "htonl(")
            if patched != text:
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(patched)
