#!/usr/bin/env bash
# build.sh — wrapper for build.py (distap-esp32)
#
# Sets this board's default serial port and ESP-IDF path, then delegates
# all work to build.py. All logic lives in build.py; this file only exists
# so the familiar `./build.sh --release` invocation keeps working.
#
# Usage:
#   ./build.sh [--cleanall | --flashall | --flashapp | --console | --release] [--out <output_dir>]
#
# Override the serial port or ESP-IDF path at runtime (last occurrence wins):
#   ./build.sh --serial-port /dev/tty.usbserial-YYYYYY --flashapp

# ── Defaults (edit as needed) ────────────────────────────────────────────────
COMPORT="/dev/tty.usbserial-A5069RR4"
IDF_PATH="/Users/chathuranga/DATA/esp/v5.5.3/esp-idf"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec python3 "$SCRIPT_DIR/build.py" \
    --serial-port "$COMPORT" \
    --idf-path "$IDF_PATH" \
    "$@"
