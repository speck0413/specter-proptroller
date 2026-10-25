#!/usr/bin/env bash
# The ESP32 firmware in Espressif's QEMU (QEMU.md): build, merge the 4 MB flash image a first USB flash writes
# (FLASH-MAP.md), boot it, and test it over the emulated UART. Exit code 1 when a check fails.
#
#   tools/qemu/run-esp32.sh              both stages, about 2 minutes
#   tools/qemu/run-esp32.sh --soak 300   and then 300 s of emulated time with shows running
#   tools/qemu/run-esp32.sh --net        the network build (env esp32dev_qemu_eth, tools/qemu/eth): discovery,
#                                        conformance, an update over HTTP, MQTT, show-first under stalled clients
#   tools/qemu/run-esp32.sh --serve      the network build, kept up on http://127.0.0.1:18080/ for 10 minutes
#   tools/qemu/run-esp32.sh --s3         the bare ESP32-S3 image without Wi-Fi (env esp32s3_qemu) on QEMU's esp32s3
#                                        machine: the serial smoke test (BARE-DEVKIT.md; the C3 has no QEMU here)
#
# Stage 1 boots the board's own image (env esp32dev) up to the Wi-Fi start, where QEMU's missing radio ends it.
# Stage 2 boots the same firmware without Wi-Fi (env esp32dev_qemu) and runs the serial smoke test.
# Espressif's QEMU release is downloaded once into $HOME/.cache/proptroller-qemu (never into the repo).
set -euo pipefail
cd "$(dirname "$0")/../.."

QEMU_TAG=esp-develop-9.2.2-20260417
QEMU_TAR=qemu-xtensa-softmmu-esp_develop_9.2.2_20260417-x86_64-linux-gnu.tar.xz
QEMU_SHA=0eecb2a34a5586c0e59110f77b9343b7b336e82fdb0e1a30e1dc1bab8a547e35
CACHE="$HOME/.cache/proptroller-qemu"
QEMU="$CACHE/qemu/bin/qemu-system-xtensa"
PIO_HOME="$HOME/.platformio"
PIO="$(command -v pio || echo "$PIO_HOME/penv/bin/pio")"
PY="$PIO_HOME/penv/bin/python"
[ -x "$PY" ] || PY=python3
ESPTOOL="$PIO_HOME/packages/tool-esptoolpy/esptool.py"
BOOT_APP0="$PIO_HOME/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
ADDR2LINE="$PIO_HOME/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-addr2line"

if [ ! -x "$QEMU" ]; then
  [ "$(uname -sm)" = "Linux x86_64" ] || { echo "only the Linux x86_64 QEMU release is set up here"; exit 1; }
  mkdir -p "$CACHE/dl"
  echo "== downloading Espressif QEMU $QEMU_TAG into $CACHE"
  curl -fsSL -o "$CACHE/dl/$QEMU_TAR" "https://github.com/espressif/qemu/releases/download/$QEMU_TAG/$QEMU_TAR"
  echo "$QEMU_SHA  $CACHE/dl/$QEMU_TAR" | sha256sum -c -
  tar -xJf "$CACHE/dl/$QEMU_TAR" -C "$CACHE"
fi

# the image a first USB flash writes: bootloader 0x1000, partition table 0x8000, otadata (boot_app0) 0xE000,
# the app 0x10000; the rest erased (0xFF), as on a new board
merge() {
  local b=".pio/build/$1"
  "$PY" "$ESPTOOL" --chip esp32 merge_bin --fill-flash-size 4MB --flash_mode dio --flash_freq 40m --flash_size 4MB \
    -o "$b/qemu-flash.bin" 0x1000 "$b/bootloader.bin" 0x8000 "$b/partitions.bin" 0xe000 "$BOOT_APP0" \
    0x10000 "$b/firmware.bin" >/dev/null
}

DRIVE=("$PY" tools/qemu/drive.py --qemu "$QEMU" --addr2line "$ADDR2LINE")
if [ "${1:-}" = "--s3" ]; then
  shift
  "$PIO" run -e esp32s3_qemu
  S=.pio/build/esp32s3_qemu
  # the S3's bootloader sits at 0x0 (FLASH-MAP.md)
  "$PY" "$ESPTOOL" --chip esp32s3 merge_bin --fill-flash-size 4MB --flash_mode dio --flash_freq 80m --flash_size 4MB \
    -o "$S/qemu-flash.bin" 0x0 "$S/bootloader.bin" 0x8000 "$S/partitions.bin" 0xe000 "$BOOT_APP0" \
    0x10000 "$S/firmware.bin" >/dev/null
  exec "$PY" tools/qemu/drive.py --qemu "$QEMU" --machine esp32s3 --mcu esp32-s3 \
    --addr2line "$PIO_HOME/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line" smoke \
    --flash "$S/qemu-flash.bin" --elf "$S/firmware.elf" --log "$S/qemu-serial.log" "$@"
fi
if [ "${1:-}" = "--net" ] || [ "${1:-}" = "--serve" ]; then
  mode=${1#--}
  shift
  # the Arduino core as an ESP-IDF component with OpenCores Ethernet (first build: ESP-IDF 4.4.7 is downloaded
  # into $PIO_HOME, a few minutes)
  "$PIO" run -d tools/qemu/eth
  E=tools/qemu/eth/.pio/build/esp32dev_qemu_eth
  "$PY" "$ESPTOOL" --chip esp32 merge_bin --fill-flash-size 4MB --flash_mode dio --flash_freq 40m --flash_size 4MB \
    -o "$E/qemu-flash.bin" 0x1000 "$E/bootloader.bin" 0x8000 "$E/partitions.bin" 0xe000 "$BOOT_APP0" \
    0x10000 "$E/firmware.bin" >/dev/null
  exec "${DRIVE[@]}" "$mode" --flash "$E/qemu-flash.bin" --elf "$E/firmware.elf" --image "$E/firmware.bin" \
    --boot-timeout 90 --log "$E/qemu-serial.log" "$@"
fi

"$PIO" run -e esp32dev -e esp32dev_qemu
merge esp32dev
merge esp32dev_qemu

rc=0
echo "== stage 1: the board's own image (esp32dev)"
"${DRIVE[@]}" full --flash .pio/build/esp32dev/qemu-flash.bin --elf .pio/build/esp32dev/firmware.elf \
  --log .pio/build/esp32dev/qemu-serial.log || rc=1
echo "== stage 2: the same firmware without Wi-Fi (esp32dev_qemu)"
"${DRIVE[@]}" smoke --flash .pio/build/esp32dev_qemu/qemu-flash.bin --elf .pio/build/esp32dev_qemu/firmware.elf \
  --log .pio/build/esp32dev_qemu/qemu-serial.log "$@" || rc=1
exit $rc
