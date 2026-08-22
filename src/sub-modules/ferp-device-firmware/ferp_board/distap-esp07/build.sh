#!/usr/bin/env bash
# build.sh — wrapper for build.py (distap-esp07)
#
# Sets this board's default serial port and ESP8266_RTOS_SDK path, then
# delegates all work to build.py. All logic lives in build.py; this file only
# exists so the familiar `./build.sh --release` invocation keeps working.
#
# Usage:
#   ./build.sh [--cleanall | --flashall | --flashapp | --console | --release] [--out <output_dir>]
#
# Override the serial port or SDK path at runtime (last occurrence wins):
#   ./build.sh --serial-port /dev/tty.usbserial-YYYYYY --flashapp
#
# ─────────────────────────────────────────────────────────────────────────────
# ONE-TIME TOOLCHAIN SETUP — this board uses a DIFFERENT SDK/toolchain than the
# esp32 boards in this repo (main/distap-esp32). The ESP07 is an ESP8266, built
# against ESP8266_RTOS_SDK with the xtensa-lx106-elf toolchain — neither is
# provided by the mainline esp-idf install already used elsewhere.
#
#   1. Clone the SDK (pick a stable release branch, e.g. release/v3.4):
#        git clone -b release/v3.4 --recursive \
#          https://github.com/espressif/ESP8266_RTOS_SDK.git \
#          ~/DATA/esp/ESP8266_RTOS_SDK
#
#   2. Install its toolchain + Python deps (downloads xtensa-lx106-elf into
#      ~/.espressif, separate from the esp32 toolchain — no conflict):
#        cd ~/DATA/esp/ESP8266_RTOS_SDK
#        ./install.sh
#
#   3. Update IDF_PATH below to match the clone location from step 1.
#
# If your SDK checkout predates idf.py support, fall back to the Make flow
# documented in this board's README.md (`make menuconfig`, `make -j6`,
# `make flash`) instead of this script.
# ─────────────────────────────────────────────────────────────────────────────

# ── Defaults (edit as needed) ────────────────────────────────────────────────
COMPORT="/dev/tty.usbserial-A5069RR4"
IDF_PATH="/Users/chathuranga/DATA/esp/ESP8266_RTOS_SDK"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec python3 "$SCRIPT_DIR/build.py" \
    --serial-port "$COMPORT" \
    --idf-path "$IDF_PATH" \
    "$@"
