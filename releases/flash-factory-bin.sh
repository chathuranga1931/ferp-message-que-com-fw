#!/usr/bin/env bash
# flash-factory-bin.sh — Flash a combined factory .bin using the bundled esptool.exe
#
# Usage:
#   ./flash-factory-bin.sh <COM-port> <path-to-factory-bin>
#
# No ESP-IDF or Python environment is required — this uses the esptool.exe
# bundled in this same folder and writes the combined factory image starting
# at address 0x0.

COMPORT=$1
FACTORY_BIN=$2

if [[ -z "$COMPORT" || -z "$FACTORY_BIN" ]]; then
    echo "Usage: $0 <COM-port> <path-to-factory-bin>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ESPTOOL="$SCRIPT_DIR/esptool.exe"

if [[ ! -f "$ESPTOOL" ]]; then
    echo "ERROR: esptool.exe not found at: $ESPTOOL"
    exit 1
fi

echo "Flashing factory $FACTORY_BIN to $COMPORT..."

"$ESPTOOL" --chip esp32 -p "$COMPORT" -b 96000 write_flash 0x0 "$FACTORY_BIN"
