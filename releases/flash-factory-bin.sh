
#!/usr/bin/env bash
# flash.sh — Build and flash helper for ferp-com-esp32-idf
#
# Usage:
#   ./flash.sh [--cleanall | --flashall | --flashspiffs | --flashapp | --console | --release]
#
# Flags:
#   --cleanall    Full clean, build everything, flash everything (app + SPIFFS)
#   --flashall    Build without clean, flash everything (app + SPIFFS)
#   --flashspiffs Build SPIFFS image only, flash SPIFFS partition only
#   --flashapp    Build without clean, flash app partition only
#   --console     Open serial monitor (idf.py monitor)
#   --release     Build paired release binaries (even=OTA-test, odd=production)
#                 Reads FW_VERSION from version.h, computes next odd build as the
#                 production release, pairs it with the preceding even build,
#                 copies both into releases/<odd-version>/, then leaves version.h
#                 at the odd version.
#
# Variables (edit as needed):
COMPORT=$1
FACTORY_BIN=$2

IDF_PATH="/Volumes/Data/esp/.espressif/v5.5.3/esp-idf"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RELEASES_DIR="$SCRIPT_DIR/releases"

echo "Setting up IDF environment..."
source "$IDF_PATH/export.sh"
if ! command -v idf.py &>/dev/null; then
    echo "ERROR: idf.py not found after sourcing IDF environment."
    echo "       Check IDF_PATH in flash.sh: $IDF_PATH"
    exit 1
fi

echo "Flashing factory $FACTORY_BIN to $COMPORT..."

esptool.py --chip esp32 -p "$COMPORT" -b 1152000 write_flash 0x0 "$FACTORY_BIN"