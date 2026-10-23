#!/usr/bin/env bash
# shot.sh <page.html[#hash]> <out.png> [width height]: a headless Chrome screenshot of a guide page (WebGL by SwiftShader)
set -euo pipefail
P="$(cd "$(dirname "$0")/../../html" && pwd)"
D="$(mktemp -d)"; trap 'rm -r "$D"' EXIT
google-chrome --headless=new --use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader --user-data-dir="$D" \
  --hide-scrollbars --window-size="${3:-1600},${4:-900}" --virtual-time-budget=8000 --run-all-compositor-stages-before-draw \
  --enable-logging=stderr --v=0 --screenshot="$2" "file://$P/$1" 2>&1 | grep -i "console\|error" | grep -v "gpu_\|dbus\|Fontconfig\|sandbox" | head -20 || true
