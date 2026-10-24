#!/bin/sh
# Compile every example sketch for the ESP32 DevKit and the Pico W (pio ci, the library from lib/), then every
# project of workspace-template/ for every board (its envs checked against make_envs.py first), and, with arduino-cli
# installed, each workspace sketch through its sketch.yaml profile.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
log="${TMPDIR:-/tmp}/proptroller-example.log"
rp2_platform="https://github.com/maxgerhardt/platform-raspberrypi.git#5d4561a05e3b212660ac6fdd3fbfb328d1988aa1"
# pio ci checks -b against the installed platforms' boards before it reads -O platform=, so on a fresh machine
# (CI) the Pico W board is unknown until its platform is installed
pio pkg install -g -p "$rp2_platform" >"$log" 2>&1 || { tail -30 "$log"; exit 1; }
for ex in "$here"/lib/proptroller/examples/*/; do
  for board in esp32dev rpipicow; do
    echo "== $(basename "$ex") on $board"
    if [ "$board" = esp32dev ]; then
      # the shared flash map, as the README tells Arduino IDE users to set up (partitions.csv) and the workspace
      # projects copy: the firmware no longer fits the core's default 1.25 MB app partition
      opts="-O platform=espressif32@7.1.3 -O board_build.partitions=$here/../partitions/esp32-4mb.csv"
      deps="bblanchon/ArduinoJson@7.4.2, bertmelis/espMqttClient@1.7.3, ESP32Async/AsyncTCP@3.5.0"
      flags="-DARDUINOJSON_USE_LONG_LONG=1 -DCONFIG_ASYNC_TCP_RUNNING_CORE=0 -DCONFIG_ASYNC_TCP_PRIORITY=3 -DEMC_ALLOW_NOT_CONNECTED_PUBLISH=0"
    else
      opts="-O platform=$rp2_platform -O board_build.core=earlephilhower"
      deps="bblanchon/ArduinoJson@7.4.2, knolleary/PubSubClient@2.8"
      flags="-DARDUINOJSON_USE_LONG_LONG=1"
    fi
    pio ci "$ex"*.ino --lib "$here/lib/proptroller" -b $board $opts \
      -O "lib_deps=$deps" -O "build_flags=$flags" \
      >"$log" 2>&1 || { tail -30 "$log"; exit 1; }
  done
done
# a board support package (bsp/relay4-button: its pin map, checked at compile time) with the first example
echo "== 01_BlinkAnOutput with the example board support package bsp/relay4-button (esp32dev)"
pio ci "$here"/lib/proptroller/examples/01_BlinkAnOutput/*.ino --lib "$here/lib/proptroller" -b esp32dev -O platform=espressif32@7.1.3 \
  -O "board_build.partitions=$here/../partitions/esp32-4mb.csv" -O "lib_deps=bblanchon/ArduinoJson@7.4.2, bertmelis/espMqttClient@1.7.3, ESP32Async/AsyncTCP@3.5.0" \
  -O "build_flags=-DARDUINOJSON_USE_LONG_LONG=1 -DCONFIG_ASYNC_TCP_RUNNING_CORE=0 -DCONFIG_ASYNC_TCP_PRIORITY=3 -DEMC_ALLOW_NOT_CONNECTED_PUBLISH=0 -std=gnu++17 -I$here/bsp/relay4-button" \
  >"$log" 2>&1 || { tail -30 "$log"; exit 1; }
# the workspace: its platformio.ini is up to date, then every project on every board (in a copy)
ws="$here/workspace-template"
python3 "$ws/make_envs.py" --check
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp -r "$ws/effects" "$ws/firmware" "$ws/project_src.py" "$tmp/"
sed -e "s#^dir = ..\$#dir = $here#" -e '/^; ---- projects:/,$d' "$ws/platformio.ini" >"$tmp/platformio.ini"
python3 "$ws/make_envs.py" --all-boards >>"$tmp/platformio.ini"
echo "== workspace-template: every project on every board"
pio run -d "$tmp" >"$log" 2>&1 || { tail -30 "$log"; exit 1; }
# the workspace sketches through their sketch.yaml profiles (the Arduino IDE path), when arduino-cli is installed
# with its board packages; skipped otherwise (it is optional: PlatformIO is the build of record)
if command -v arduino-cli >/dev/null 2>&1; then
  for sk in "$ws"/firmware/*/; do
    echo "== $(basename "$sk") with arduino-cli compile --profile (sketch.yaml)"
    (cd "$sk" && arduino-cli compile --profile "$(sed -n 's/^default_profile: *//p' sketch.yaml)" .) \
      >"$log" 2>&1 || { tail -30 "$log"; exit 1; }
  done
else
  echo "== arduino-cli not installed: the sketch.yaml profiles are not compiled (optional)"
fi
echo "all examples build"
