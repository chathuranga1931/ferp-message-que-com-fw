#!/usr/bin/env bash
# build_raw_capture.sh — wrapper for build_raw_capture.py (distap-esp32,
# raw-capture-only test firmware)
#
# Sets this board's default serial port and ESP-IDF path, then delegates
# all work to build_raw_capture.py. All logic lives in build_raw_capture.py;
# this file only exists so the familiar `./build_raw_capture.sh --release`
# invocation keeps working.
#
# Usage:
#   ./build_raw_capture.sh [--cleanall | --flashall | --flashapp | --console | --release] [--out <output_dir>]
#
# Override the serial port or ESP-IDF path at runtime (last occurrence wins):
#   ./build_raw_capture.sh --serial-port /dev/tty.usbserial-YYYYYY --flashapp

# ── Defaults (edit as needed) ────────────────────────────────────────────────
COMPORT="/dev/tty.usbserial-A5069RR4"
IDF_PATH="/Users/chathuranga/DATA/esp/v5.5.3/esp-idf"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec python3 "$SCRIPT_DIR/build_raw_capture.py" \
    --serial-port "$COMPORT" \
    --idf-path "$IDF_PATH" \
    "$@"
