#!/bin/sh
# Builds the simulated board from the repository's firmware (the firmware's own `sim` build: the real controller
# code on this computer) and puts it where Tauri bundles it as a sidecar. Needs PlatformIO (`pio`).
# SIM_TARGET=<rust target triple> names the sidecar for `tauri build --target <triple>` (CI); the default is
# this computer's triple. The simulator is always built for this computer, so SIM_TARGET only renames it.
# The sim builds on Linux and macOS; Windows has no network layer for it yet (net/posix.cpp is POSIX only).
set -e
cd "$(dirname "$0")/.."
PIO=$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")
# the firmware build regenerates a committed header (web_assets.h, from firmware/web): when neither had changes
# before the build, put the header back afterwards; otherwise leave the regenerated one to be committed
H=../firmware/arduino/lib/proptroller/src/proptroller/web_assets.h
KEEP=
git diff --quiet HEAD -- "$H" ../firmware/web 2>/dev/null && KEEP=1
(cd ../firmware/arduino && "$PIO" run -e sim)
if [ -n "$KEEP" ]; then git checkout -- "$H" 2>/dev/null || true; fi
TRIPLE=${SIM_TARGET:-$(rustc -vV 2>/dev/null | sed -n 's/^host: //p')}
if [ -z "$TRIPLE" ]; then echo "error: no rustc to name the sidecar (scripts/setup.sh, or set SIM_TARGET)" >&2; exit 1; fi
OUT=../firmware/arduino/.pio/build/sim/program
EXT=
if [ -f "$OUT.exe" ]; then OUT="$OUT.exe"; EXT=.exe; fi
mkdir -p src-tauri/binaries
cp "$OUT" "src-tauri/binaries/proptroller-sim-$TRIPLE$EXT"
echo "simulator: src-tauri/binaries/proptroller-sim-$TRIPLE$EXT"
