# The release and the source revision, as tools/embed_web.py sets them for the board builds.
import os, subprocess
Import("env")  # noqa: F821
root = os.path.join(env.subst("$PROJECT_DIR"), "..", "..", "..")  # noqa: F821
with open(os.path.join(root, "..", "VERSION")) as f:
    version = f.read().strip()
try:
    build = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=root, stderr=subprocess.DEVNULL).decode().strip()
except Exception:
    build = "unknown"
env.Append(CPPDEFINES=[("PROPTROLLER_FW_VERSION", '\\"%s\\"' % version), ("PROPTROLLER_FW_BUILD", '\\"%s\\"' % build)])  # noqa: F821
