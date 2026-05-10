
#!/usr/bin/env bash
# flash.sh — Build and flash helper for ferp-com-esp32-idf
#
# Usage:
#   ./flash.sh [--cleanall | --flashall | --flashspiffs | --flashapp | --console]
#
# Flags:
#   --cleanall    Full clean, build everything, flash everything (app + SPIFFS)
#   --flashall    Build without clean, flash everything (app + SPIFFS)
#   --flashspiffs Build SPIFFS image only, flash SPIFFS partition only
#   --flashapp    Build without clean, flash app partition only
#   --console     Open serial monitor (idf.py monitor)
#
# Variables (edit as needed):
COMPORT="/dev/tty.usbserial-A5069RR4"
IDF_PATH="/Volumes/Data/esp/.espressif/v5.5.3/esp-idf"

# Project root (relative to this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/src/product/ferp-com-main/ferp-com-esp32-idf"

# ── Parse flags ──────────────────────────────────────────────────────────────
CLEANALL=false
FLASHALL=false
FLASHSPIFFS=false
FLASHAPP=false
CONSOLE=false

for arg in "$@"; do
    case "$arg" in
        --cleanall)    CLEANALL=true    ;;
        --flashall)    FLASHALL=true    ;;
        --flashspiffs) FLASHSPIFFS=true ;;
        --flashapp)    FLASHAPP=true    ;;
        --console)     CONSOLE=true     ;;
        *)
            echo "ERROR: Unknown flag: $arg"
            echo "Usage: $0 [--cleanall | --flashall | --flashspiffs | --flashapp | --console]"
            exit 1
            ;;
    esac
done

if ! $CLEANALL && ! $FLASHALL && ! $FLASHSPIFFS && ! $FLASHAPP && ! $CONSOLE; then
    echo "Usage: $0 [--cleanall | --flashall | --flashspiffs | --flashapp | --console]"
    echo ""
    echo "  --cleanall    Clean, build all, flash all (app + SPIFFS)"
    echo "  --flashall    Build all (no clean), flash all (app + SPIFFS)"
    echo "  --flashspiffs Build + flash SPIFFS partition only"
    echo "  --flashapp    Build (no clean) + flash app partition only"
    echo "  --console     Open serial monitor"
    exit 1
fi

# ── Set up IDF environment ───────────────────────────────────────────────────
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "ERROR: IDF not found at: $IDF_PATH"
    exit 1
fi
echo "Setting up IDF environment..."
source "$IDF_PATH/export.sh"
if ! command -v idf.py &>/dev/null; then
    echo "ERROR: idf.py not found after sourcing IDF environment."
    echo "       Check IDF_PATH in flash.sh: $IDF_PATH"
    exit 1
fi

# ── Move to project directory ────────────────────────────────────────────────
cd "$PROJECT_DIR" || { echo "ERROR: Project directory not found: $PROJECT_DIR"; exit 1; }
echo "Project: $PROJECT_DIR"
echo "Port:    $COMPORT"
echo ""

# ── Execute requested action ─────────────────────────────────────────────────

if $CLEANALL; then
    echo "=== Clean all ==="
    idf.py fullclean

    echo "=== Build all ==="
    idf.py build

    echo "=== Flash all (app + SPIFFS) ==="
    idf.py -p "$COMPORT" flash

    echo "=== Done (clean + flash all) ==="

elif $FLASHALL; then
    echo "=== Build all ==="
    idf.py build

    echo "=== Flash all (app + SPIFFS) ==="
    idf.py -p "$COMPORT" flash

    echo "=== Done (flash all) ==="

elif $FLASHSPIFFS; then
    echo "=== Build SPIFFS image ==="
    idf.py build spiffs

    echo "=== Flash SPIFFS partition ==="
    idf.py -p "$COMPORT" spiffs

    echo "=== Done (flash SPIFFS) ==="

elif $FLASHAPP; then
    echo "=== Build app ==="
    idf.py build

    echo "=== Flash app partition ==="
    idf.py -p "$COMPORT" app-flash

    echo "=== Done (flash app) ==="

elif $CONSOLE; then
    echo "=== Serial monitor ($COMPORT) ==="
    idf.py monitor -p "$COMPORT" -b 115200
fi

