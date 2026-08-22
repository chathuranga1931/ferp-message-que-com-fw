#!/usr/bin/env bash
# build-v2.mac.sh — macOS wrapper for build.py, ferp-com-v2 product (2404 / esp07)
#
# Identical to build.mac.sh but targets the ferp-com-v2-main project
# (esp32<>esp07) by forwarding --product v2 to build.py.
#
# Usage:
#   ./build-v2.mac.sh [--serial-port /dev/tty.usbserial-XXXXX] <action>
#
# Actions:
#   --cleanall       Remove build directory only
#   --buildall       Build app + SPIFFS
#   --buildapp       Build app binary only
#   --flashall       Build + flash all (app + SPIFFS)
#   --flashspiffs    Build + flash SPIFFS partition only
#   --flashapp       Build + flash app partition only
#   --console        Open serial monitor
#   --release        Full release workflow (commit + tag + push + GitHub release)
#   --create-bundle  Create OTA bundle from existing build artifacts
#
# Override the serial port at runtime:
#   ./build-v2.mac.sh --serial-port /dev/tty.usbserial-YYYYYY --flashapp

# ── macOS defaults (edit as needed) ──────────────────────────────────────────
IDF_PATH="/Users/chathuranga/.espressif/v6.0.1/esp-idf"
# DEFAULT_PORT="/dev/tty.usbserial-548B0018381"
DEFAULT_PORT="/dev/tty.usbserial-A5069RR4"

# ── Parse --serial-port override; forward everything else to build.py ────────
SERIAL_PORT="$DEFAULT_PORT"
PASS_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial-port)
            SERIAL_PORT="$2"
            shift 2
            ;;
        --serial-port=*)
            SERIAL_PORT="${1#*=}"
            shift
            ;;
        *)
            PASS_ARGS+=("$1")
            shift
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec python3 "$SCRIPT_DIR/build.py" \
    --product v2 \
    --serial-port "$SERIAL_PORT" \
    --idf-path "$IDF_PATH" \
    "${PASS_ARGS[@]}"
