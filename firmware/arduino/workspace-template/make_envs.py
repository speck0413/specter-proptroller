#!/usr/bin/env python3
"""Write one PlatformIO env per firmware project into platformio.ini.

A project is an Arduino sketch folder, firmware/<name>/<name>.ino, with a sketch.yaml whose fqbn picks the board
(no sketch.yaml: the ESP32 DevKit). The env is named after the folder. Everything above the PROJECTS line of
platformio.ini is yours to edit; below it is written by this script. Run it after adding, renaming or removing a
project:

    python3 make_envs.py               write platformio.ini
    python3 make_envs.py --check       exit 1 if platformio.ini is not up to date
    python3 make_envs.py --all-boards  print envs for every project on every board (build checks)
    python3 make_envs.py new NAME [--board esp32dev|picow|pico2w|pico] [--from EXAMPLE]
                                       a new project firmware/NAME/ (an empty sketch, or a copy of a library example
                                       such as 03_TimedSequence, or of a sketch folder), then platformio.ini
"""
import shutil
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
INI = os.path.join(HERE, "platformio.ini")
PROJECTS = "; ---- projects: written by make_envs.py from firmware/*/sketch.yaml (do not edit below) ----"
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]*$")  # Arduino's sketch name rule, also a valid env name
DEFAULT_FQBN = "esp32:esp32:esp32"  # a project without sketch.yaml
BOARDS = {  # fqbn -> the board section of platformio.ini
    "esp32:esp32:esp32": "esp32dev",
    "rp2040:rp2040:rpipicow": "picow",
    "rp2040:rp2040:rpipico2w": "pico2w",
    "rp2040:rp2040:rpipico": "pico",
}


def fail(msg):
    sys.exit("make_envs.py: " + msg)


def projects():
    root = os.path.join(HERE, "firmware")
    out = []
    for name in sorted(os.listdir(root)):
        d = os.path.join(root, name)
        if not os.path.isdir(d):
            continue
        if not NAME.match(name):
            fail(f"firmware/{name}: a project name has letters, digits, '-' and '_' only (no spaces): "
                 f"rename the folder and its .ino, for example to {suggest(name)}")
        if not os.path.isfile(os.path.join(d, name + ".ino")):
            fail(f"firmware/{name}: needs {name}.ino (the folder and the sketch have the same name)")
        yaml = os.path.join(d, "sketch.yaml")
        fqbn = DEFAULT_FQBN
        if os.path.isfile(yaml):
            m = re.search(r"^\s*(?:default_)?fqbn:\s*(\S+)", open(yaml, encoding="utf-8").read(), re.M)
            fqbn = m.group(1) if m else None
        if fqbn not in BOARDS:
            fail(f"firmware/{name}/sketch.yaml: fqbn {fqbn} is not one of {', '.join(BOARDS)}")
        out.append((name, BOARDS[fqbn]))
    return out


def suggest(name):
    s = re.sub(r"[^A-Za-z0-9_-]+", "-", name).strip("-_") or "my-prop"
    return s if NAME.match(s) else "p" + s


# arduino-cli profiles per board (the versions PlatformIO's platforms use); {lib} is the library folder
PROFILES = {
    "esp32dev": ("esp32:esp32:esp32", "esp32:esp32 (2.0.17)", "https://espressif.github.io/arduino-esp32/package_esp32_index.json",
                 ["ArduinoJson (7.4.2)", "espMqttClient (1.7.3)", "Async TCP (3.5.0)"]),
    "picow": ("rp2040:rp2040:rpipicow", "rp2040:rp2040 (6.1.0)", "https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json",
              ["ArduinoJson (7.4.2)", "PubSubClient (2.8)"]),
    "pico2w": ("rp2040:rp2040:rpipico2w", "rp2040:rp2040 (6.1.0)", "https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json",
               ["ArduinoJson (7.4.2)", "PubSubClient (2.8)"]),
    "pico": ("rp2040:rp2040:rpipico", "rp2040:rp2040 (6.1.0)", "https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json",
             ["ArduinoJson (7.4.2)", "PubSubClient (2.8)"]),
}

EMPTY_SKETCH = """#include <ProptrollerAdvanced.h>

Proptroller prop;

void setup() {
  // prop.when(TRIG1).playSound(1).turnOn(OUT1, seconds(2));
}

void loop() {
  // your own code; the board runs the phone page, show scripts and updates by itself
}
"""


def sketch_yaml(board, lib_rel):
    fqbn, platform, index, libs = PROFILES[board]
    out = ["# Arduino IDE / arduino-cli profile: the board and pinned versions (make_envs.py reads the fqbn).",
           f"default_profile: {board}", "profiles:", f"  {board}:", f"    fqbn: {fqbn}", "    platforms:",
           f"      - platform: {platform}", f"        platform_index_url: {index}", "    libraries:"]
    out += [f"      - {l}" for l in libs] + [f"      - dir: {lib_rel}"]
    return "\n".join(out) + "\n"


def proptroller_dir():
    m = re.search(r"^\[proptroller\][^\[]*?^dir\s*=\s*(.+?)\s*$", open(INI, encoding="utf-8").read(), re.M | re.S)
    return os.path.normpath(os.path.join(HERE, os.path.expanduser(m.group(1)) if m else ".."))


def new_project(args):
    if not args or args[0].startswith("-"):
        fail("new NAME [--board esp32dev|picow|pico2w|pico] [--from EXAMPLE]")
    name, rest = args[0], args[1:]
    opts = {"--board": "esp32dev", "--from": None}
    while rest:
        if rest[0] not in opts or len(rest) < 2:
            fail(f"new: unknown or incomplete option {rest[0]}")
        opts[rest[0]] = rest[1]
        rest = rest[2:]
    board, src = opts["--board"], opts["--from"]
    if not NAME.match(name):
        fail(f"{name!r}: a project name has letters, digits, '-' and '_' only (no spaces), for example {suggest(name)}")
    if board not in PROFILES:
        fail(f"--board {board}: one of {', '.join(PROFILES)}")
    dst = os.path.join(HERE, "firmware", name)
    if os.path.exists(dst):
        fail(f"firmware/{name} already exists")
    lib = os.path.join(proptroller_dir(), "lib", "proptroller")
    files = {}
    if src:
        cand = [src, os.path.join(lib, "examples", src)]
        d = next((c for c in cand if os.path.isdir(c)), None)
        if d is None:
            fail(f"--from {src}: no such folder (a library example such as 03_TimedSequence, or a sketch folder)")
        inos = [f for f in os.listdir(d) if f.endswith(".ino")]
        if len(inos) != 1:
            fail(f"--from {src}: expected one .ino file, found {len(inos)}")
        for f in os.listdir(d):
            if f.endswith((".ino", ".cpp", ".h", ".c")) or f == "VERSION":
                files[name + ".ino" if f.endswith(".ino") else f] = open(os.path.join(d, f), encoding="utf-8").read()
    else:
        files[name + ".ino"] = EMPTY_SKETCH
    os.makedirs(dst)
    for f, text in files.items():
        open(os.path.join(dst, f), "w", encoding="utf-8").write(text)
    open(os.path.join(dst, "sketch.yaml"), "w", encoding="utf-8").write(sketch_yaml(board, os.path.relpath(lib, dst)))
    if board == "esp32dev":  # the Arduino IDE / arduino-cli take the flash map from the sketch folder
        shutil.copyfile(os.path.join(proptroller_dir(), "..", "partitions", "esp32-4mb.csv"), os.path.join(dst, "partitions.csv"))
    print(f"firmware/{name}/ ({board})")


def env(name, board, suffix=""):
    return f"[env:{name}{suffix}]\nextends = board_{board}\ncustom_project = {name}\n"


def main():
    args = sys.argv[1:]
    if args[:1] == ["new"]:
        new_project(args[1:])
        args = []
    if "--all-boards" in args:
        print("\n".join(env(n, b, "_" + b) for n, _ in projects() for b in BOARDS.values()))
        return
    text = open(INI, encoding="utf-8").read()
    if PROJECTS not in text:
        fail("platformio.ini has lost its PROJECTS line")
    head = text[: text.index(PROJECTS)]
    new = head + PROJECTS + "\n\n" + "\n".join(env(n, b) for n, b in projects())
    if "--check" in args:
        if new != text:
            fail("platformio.ini is not up to date: run python3 make_envs.py")
        return
    if new != text:
        open(INI, "w", encoding="utf-8").write(new)
        print("platformio.ini updated")


if __name__ == "__main__":
    main()
