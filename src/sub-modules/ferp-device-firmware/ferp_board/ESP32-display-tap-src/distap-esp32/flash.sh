
#!/usr/bin/env bash
# flash.sh — Build and flash helper for distap-esp32
#
# Usage:
#   ./flash.sh [--cleanall | --flashall | --flashapp | --console | --release]
#
# Flags:
#   --cleanall  Full clean, build everything, flash everything
#   --flashall  Build without clean, flash everything
#   --flashapp  Build without clean, flash app partition only
#   --console   Open serial monitor (idf.py monitor)
#   --release   Build paired release binaries (even=OTA-test, odd=production).
#               Reads CONFIG_APP_PROJECT_VER from sdkconfig.defaults, computes
#               the next odd build number (production) and even = odd-1 (test),
#               does a fullclean+build for each, copies firmware + bootloader +
#               partition-table binaries into releases/<odd-version>/, creates
#               OTA bundles for all three binary types for both versions, then
#               leaves sdkconfig.defaults at the odd (production) version.
#
# Variables (edit as needed):
COMPORT="/dev/tty.usbserial-A5069RR4"
IDF_PATH="/Users/chathuranga/DATA/esp/v5.5.3/esp-idf"

# Project root (same directory as this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"

# ── Parse flags ──────────────────────────────────────────────────────────────
CLEANALL=false
FLASHALL=false
FLASHAPP=false
CONSOLE=false
RELEASE=false

for arg in "$@"; do
    case "$arg" in
        --cleanall) CLEANALL=true ;;
        --flashall) FLASHALL=true ;;
        --flashapp) FLASHAPP=true ;;
        --console)  CONSOLE=true  ;;
        --release)  RELEASE=true  ;;
        *)
            echo "ERROR: Unknown flag: $arg"
            echo "Usage: $0 [--cleanall | --flashall | --flashapp | --console | --release]"
            exit 1
            ;;
    esac
done

if ! $CLEANALL && ! $FLASHALL && ! $FLASHAPP && ! $CONSOLE && ! $RELEASE; then
    echo "Usage: $0 [--cleanall | --flashall | --flashapp | --console | --release]"
    echo ""
    echo "  --cleanall  Clean, build all, flash all"
    echo "  --flashall  Build all (no clean), flash all"
    echo "  --flashapp  Build (no clean) + flash app partition only"
    echo "  --console   Open serial monitor"
    echo "  --release   Build paired release: even (OTA-test) + odd (production)"
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

    echo "=== Flash all ==="
    idf.py -p "$COMPORT" flash

    echo "=== Done (clean + flash all) ==="

elif $FLASHALL; then
    echo "=== Build all ==="
    idf.py build

    echo "=== Flash all ==="
    idf.py -p "$COMPORT" flash

    echo "=== Done (flash all) ==="

elif $FLASHAPP; then
    echo "=== Build app ==="
    idf.py build

    echo "=== Flash app partition ==="
    idf.py -p "$COMPORT" app-flash

    echo "=== Done (flash app) ==="

elif $CONSOLE; then
    echo "=== Serial monitor ($COMPORT) ==="
    idf.py monitor -p "$COMPORT" -b 115200

elif $RELEASE; then
    SDKCONFIG_DEFAULTS="$PROJECT_DIR/sdkconfig.defaults"
    BUILD_FW="$PROJECT_DIR/build/distap_esp32.bin"
    BUILD_BOOT="$PROJECT_DIR/build/bootloader/bootloader.bin"
    BUILD_PART="$PROJECT_DIR/build/partition_table/partition-table.bin"
    RELEASES_DIR="$PROJECT_DIR/releases"
    OTA_TOOLS="$PROJECT_DIR/tools/ota-bundle-tools"

    # ── Read current version from sdkconfig.defaults ─────────────────────────
    CURRENT_VER=$(grep '^CONFIG_APP_PROJECT_VER=' "$SDKCONFIG_DEFAULTS" \
                  | sed 's/CONFIG_APP_PROJECT_VER="\(.*\)"/\1/')
    if [[ -z "$CURRENT_VER" ]]; then
        echo "ERROR: Could not parse CONFIG_APP_PROJECT_VER from $SDKCONFIG_DEFAULTS"
        exit 1
    fi
    echo "Current CONFIG_APP_PROJECT_VER: $CURRENT_VER"

    # Split into major.minor.patch.build (4-digit: X.Y.Z.N)
    IFS='.' read -r V1 V2 V3 V4 <<< "$CURRENT_VER"
    if [[ -z "$V4" ]]; then
        echo "ERROR: CONFIG_APP_PROJECT_VER must be in X.Y.Z.N format (got '$CURRENT_VER')"
        exit 1
    fi

    # Compute next odd build number (production) and even = odd-1 (OTA-test)
    if (( V4 % 2 == 0 )); then
        ODD_V4=$((V4 + 1))
    else
        ODD_V4=$((V4 + 2))
    fi
    EVEN_V4=$((ODD_V4 - 1))

    EVEN_VER="$V1.$V2.$V3.$EVEN_V4"
    ODD_VER="$V1.$V2.$V3.$ODD_V4"

    RELEASE_DIR="$RELEASES_DIR/$ODD_VER"
    mkdir -p "$RELEASE_DIR"

    echo ""
    echo "=== Release plan ==="
    echo "  Even (OTA-test):   $EVEN_VER"
    echo "  Odd  (production): $ODD_VER"
    echo "  Output dir:        $RELEASE_DIR"
    echo ""

    # ── Helper: copy binaries and run all three OTA bundle tools ─────────────
    # Output structure per version:
    #   releases/<odd>/<prefix>_v<ver>/bin/    ← .bin files
    #   releases/<odd>/<prefix>_v<ver>/bundle/ ← .bdl files
    copy_and_bundle() {
        local VER="$1"
        local VER_DIR="$RELEASE_DIR/ferp_dt_esp32_v${VER}"
        local BIN_DIR="$VER_DIR/bin"
        local BDL_DIR="$VER_DIR/bundle"
        mkdir -p "$BIN_DIR" "$BDL_DIR"

        local FW_BIN="$BIN_DIR/distap_esp32_v${VER}.bin"
        local BOOT_BIN="$BIN_DIR/bootloader_v${VER}.bin"
        local PART_BIN="$BIN_DIR/partition_table_v${VER}.bin"

        echo "--- Copying binaries for $VER ---"
        cp "$BUILD_FW"   "$FW_BIN"
        cp "$BUILD_BOOT" "$BOOT_BIN"
        cp "$BUILD_PART" "$PART_BIN"
        echo "  Saved: $FW_BIN"
        echo "  Saved: $BOOT_BIN"
        echo "  Saved: $PART_BIN"

        echo "--- Creating OTA bundles for $VER ---"
        python3 "$OTA_TOOLS/OtaBundleCreate-DTEsp32_Firmware.py" \
            "$FW_BIN" "$VER" "$BDL_DIR"
        python3 "$OTA_TOOLS/OtaBundleCreate-DTEsp32_Bootloader.py" \
            "$BOOT_BIN" "$VER" "$BDL_DIR"
        python3 "$OTA_TOOLS/OtaBundleCreate-DTEsp32_Partitions.py" \
            "$PART_BIN" "$VER" "$BDL_DIR"
    }

    # ── Build 1: even version (OTA-test) ─────────────────────────────────────
    echo "--- Setting CONFIG_APP_PROJECT_VER to $EVEN_VER ---"
    sed -i '' "s/^CONFIG_APP_PROJECT_VER=.*/CONFIG_APP_PROJECT_VER=\"$EVEN_VER\"/" "$SDKCONFIG_DEFAULTS"
    sed -i '' "s/^CONFIG_APP_PROJECT_VER=.*/CONFIG_APP_PROJECT_VER=\"$EVEN_VER\"/" "$PROJECT_DIR/sdkconfig"

    echo "--- Building even version ($EVEN_VER) ---"
    rm -rf "$PROJECT_DIR/build"
    idf.py build
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Build failed for even version $EVEN_VER"
        sed -i '' "s/^CONFIG_APP_PROJECT_VER=.*/CONFIG_APP_PROJECT_VER=\"$CURRENT_VER\"/" "$SDKCONFIG_DEFAULTS"
        sed -i '' "s/^CONFIG_APP_PROJECT_VER=.*/CONFIG_APP_PROJECT_VER=\"$CURRENT_VER\"/" "$PROJECT_DIR/sdkconfig"
        exit 1
    fi
    copy_and_bundle "$EVEN_VER"

    # ── Build 2: odd version (production) ────────────────────────────────────
    echo ""
    echo "--- Setting CONFIG_APP_PROJECT_VER to $ODD_VER ---"
    sed -i '' "s/^CONFIG_APP_PROJECT_VER=.*/CONFIG_APP_PROJECT_VER=\"$ODD_VER\"/" "$SDKCONFIG_DEFAULTS"
    sed -i '' "s/^CONFIG_APP_PROJECT_VER=.*/CONFIG_APP_PROJECT_VER=\"$ODD_VER\"/" "$PROJECT_DIR/sdkconfig"

    echo "--- Building odd version ($ODD_VER) ---"
    rm -rf "$PROJECT_DIR/build"
    idf.py build
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Build failed for odd version $ODD_VER"
        exit 1
    fi
    copy_and_bundle "$ODD_VER"

    echo ""
    echo "=== Release complete: $ODD_VER ==="
    echo "  Output: $RELEASE_DIR/"
    echo "    ferp_dt_esp32_v${EVEN_VER}/bin/    ← firmware, bootloader, partition-table .bin"
    echo "    ferp_dt_esp32_v${EVEN_VER}/bundle/ ← OTA .bdl bundles (flash to test OTA upgrade)"
    echo "    ferp_dt_esp32_v${ODD_VER}/bin/     ← firmware, bootloader, partition-table .bin"
    echo "    ferp_dt_esp32_v${ODD_VER}/bundle/  ← OTA .bdl bundles (production)"
    echo ""
    echo "  sdkconfig.defaults left at: $ODD_VER"
fi

