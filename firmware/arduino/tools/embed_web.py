# PlatformIO pre-build script: embeds the phone web UI (../web) as gzipped bytes in
# lib/proptroller/src/proptroller/web_assets.h (deterministic: gzip mtime 0, so the file only changes when
# the UI does). The header is committed so the library also builds from the Arduino IDE.
import os, sys
try:
    Import("env")  # noqa: F821
    PROJECT = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:
    PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT, "tools"))
import web_assets  # noqa: E402

web_assets.generate(PROJECT)

# The release (firmware/VERSION) and the source revision (git short hash, "unknown" without git).
try:
    import subprocess
    with open(os.path.join(PROJECT, "..", "VERSION")) as f:
        version = f.read().strip()
    try:
        build = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=PROJECT, stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        build = "unknown"
    env.Append(CPPDEFINES=[("PROPTROLLER_FW_VERSION", '\\"%s\\"' % version), ("PROPTROLLER_FW_BUILD", '\\"%s\\"' % build)])  # noqa: F821
except NameError:
    pass
