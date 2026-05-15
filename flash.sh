
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
#                 Reads FW_VERSION from version.h, produces two builds (+1 even,
#                 +2 odd), copies both into releases/<odd-version>/, then leaves
#                 version.h at the odd version.
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
RELEASE=false

for arg in "$@"; do
    case "$arg" in
        --cleanall)    CLEANALL=true    ;;
        --flashall)    FLASHALL=true    ;;
        --flashspiffs) FLASHSPIFFS=true ;;
        --flashapp)    FLASHAPP=true    ;;
        --console)     CONSOLE=true     ;;
        --release)     RELEASE=true     ;;
        *)
            echo "ERROR: Unknown flag: $arg"
            echo "Usage: $0 [--cleanall | --flashall | --flashspiffs | --flashapp | --console | --release]"
            exit 1
            ;;
    esac
done

if ! $CLEANALL && ! $FLASHALL && ! $FLASHSPIFFS && ! $FLASHAPP && ! $CONSOLE && ! $RELEASE; then
    echo "Usage: $0 [--cleanall | --flashall | --flashspiffs | --flashapp | --console | --release]"
    echo ""
    echo "  --cleanall    Clean, build all, flash all (app + SPIFFS)"
    echo "  --flashall    Build all (no clean), flash all (app + SPIFFS)"
    echo "  --flashspiffs Build + flash SPIFFS partition only"
    echo "  --flashapp    Build (no clean) + flash app partition only"
    echo "  --console     Open serial monitor"
    echo "  --release     Build paired release: even (OTA-test) + odd (production)"
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
    idf.py -p "$COMPORT" -b 1152000 flash

    echo "=== Done (clean + flash all) ==="

elif $FLASHALL; then
    echo "=== Build all ==="
    idf.py build

    echo "=== Flash all (app + SPIFFS) ==="
    idf.py -p "$COMPORT" -b 1152000 flash

    echo "=== Done (flash all) ==="

elif $FLASHSPIFFS; then
    echo "=== Build SPIFFS image ==="
    idf.py build spiffs

    echo "=== Flash SPIFFS partition ==="
    idf.py -p "$COMPORT" -b 1152000 spiffs

    echo "=== Done (flash SPIFFS) ==="

elif $FLASHAPP; then
    echo "=== Build app ==="
    idf.py build

    echo "=== Flash app partition ==="
    idf.py -p "$COMPORT" -b 1152000 app-flash

    echo "=== Done (flash app) ==="

elif $CONSOLE; then
    echo "=== Serial monitor ($COMPORT) ==="
    idf.py monitor -p "$COMPORT" -b 115200

elif $RELEASE; then
    VERSION_FILE="$SCRIPT_DIR/src/product/ferp-com-main/app/version.h"
    BUILD_BIN="$PROJECT_DIR/build/ferp-com.bin"
    RELEASES_DIR="$SCRIPT_DIR/releases"

    # ── Read current version ─────────────────────────────────────────────────
    CURRENT_VER=$(grep '#define FW_VERSION' "$VERSION_FILE" | sed 's/.*"\(.*\)".*/\1/')
    if [[ -z "$CURRENT_VER" ]]; then
        echo "ERROR: Could not parse FW_VERSION from $VERSION_FILE"
        exit 1
    fi
    echo "Current FW_VERSION: $CURRENT_VER"

    # Split into major.minor.patch.build
    IFS='.' read -r V1 V2 V3 V4 <<< "$CURRENT_VER"
    if [[ -z "$V4" ]]; then
        echo "ERROR: FW_VERSION must be in X.Y.Z.N format (got '$CURRENT_VER')"
        exit 1
    fi

    # Even  = current + 1  (OTA-test binary — same code, different version string)
    EVEN_VER="$V1.$V2.$V3.$((V4 + 1))"
    # Odd   = current + 2  (production binary — this is the real release)
    ODD_VER="$V1.$V2.$V3.$((V4 + 2))"

    RELEASE_DIR="$RELEASES_DIR/$ODD_VER"
    mkdir -p "$RELEASE_DIR"

    echo ""
    echo "=== Release plan ==="
    echo "  Even (OTA-test):   $EVEN_VER"
    echo "  Odd  (production): $ODD_VER"
    echo "  Output dir:        $RELEASE_DIR"
    echo ""

    # ── Build 1: even version ────────────────────────────────────────────────
    echo "--- Setting FW_VERSION to $EVEN_VER ---"
    sed -i '' "s/#define FW_VERSION[[:space:]]*\"[^\"]*\"/#define FW_VERSION          \"$EVEN_VER\"/" "$VERSION_FILE"

    echo "--- Building even version ($EVEN_VER) ---"
    idf.py build
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Build failed for even version $EVEN_VER"
        exit 1
    fi
    cp "$BUILD_BIN" "$RELEASE_DIR/ferp-com-v${EVEN_VER}.bin"
    echo "Saved: $RELEASE_DIR/ferp-com-v${EVEN_VER}.bin"
    python3 "$SCRIPT_DIR/tools/ota-bundle-tools/OtaBundleCreate-MainESP32.py" \
        "$RELEASE_DIR/ferp-com-v${EVEN_VER}.bin" "$EVEN_VER" "$RELEASE_DIR"

    # ── Build 2: odd version ─────────────────────────────────────────────────
    echo ""
    echo "--- Setting FW_VERSION to $ODD_VER ---"
    sed -i '' "s/#define FW_VERSION[[:space:]]*\"[^\"]*\"/#define FW_VERSION          \"$ODD_VER\"/" "$VERSION_FILE"

    echo "--- Building odd version ($ODD_VER) ---"
    idf.py build
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Build failed for odd version $ODD_VER"
        exit 1
    fi
    cp "$BUILD_BIN" "$RELEASE_DIR/ferp-com-v${ODD_VER}.bin"
    echo "Saved: $RELEASE_DIR/ferp-com-v${ODD_VER}.bin"
    python3 "$SCRIPT_DIR/tools/ota-bundle-tools/OtaBundleCreate-MainESP32.py" \
        "$RELEASE_DIR/ferp-com-v${ODD_VER}.bin" "$ODD_VER" "$RELEASE_DIR"

    echo ""
    echo "=== Release complete: $ODD_VER ==="
    echo "  $RELEASE_DIR/ferp-com-v${EVEN_VER}.bin  ← flash this to test OTA upgrade"
    echo "  $RELEASE_DIR/ferp-com-v${ODD_VER}.bin   ← production binary"
    echo ""
    echo "  version.h left at: $ODD_VER"
fi

