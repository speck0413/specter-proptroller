# PlatformIO pre-script: build the env's project folder, firmware/<project>/, as its own source directory, so
# PlatformIO turns its .ino into C++ the Arduino way (make_envs.py writes custom_project). The firmware reports the
# folder name as its `fw` name, and the version in firmware/<project>/VERSION when there is one.
import os

Import("env")  # noqa: F821
# the library and the build scripts come from [proptroller] dir: say so at once when it points nowhere (a copied
# workspace still saying `dir = ..`), instead of "ProptrollerAdvanced.h: No such file" deep in the build
config = env.GetProjectConfig()  # noqa: F821
pt_dir = config.get("proptroller", "dir") if config.has_option("proptroller", "dir") else ""
pt_dir = os.path.join(env.subst("$PROJECT_DIR"), os.path.expanduser(pt_dir)) if pt_dir else ""  # noqa: F821
if pt_dir and not os.path.isfile(os.path.join(pt_dir, "lib", "proptroller", "src", "ProptrollerAdvanced.h")):
    print("\nThe Proptroller library is not at %s.\nSet dir under [proptroller] in platformio.ini to the full path of "
          "the repository's firmware/arduino folder (workspace-template/README.md).\n" % os.path.abspath(pt_dir))
    env.Exit(1)  # noqa: F821
name = env.GetProjectOption("custom_project")  # noqa: F821
src = os.path.join(env.subst("$PROJECT_DIR"), "firmware", name)  # noqa: F821
env.Replace(PROJECT_SRC_DIR=src)  # noqa: F821
defines = [("PROPTROLLER_FW_NAME", '\\"%s\\"' % name)]
version = os.path.join(src, "VERSION")
if os.path.isfile(version):
    defines.append(("PROPTROLLER_FW_VERSION", '\\"%s\\"' % open(version).read().strip()))
env.Append(CPPDEFINES=defines)  # noqa: F821
