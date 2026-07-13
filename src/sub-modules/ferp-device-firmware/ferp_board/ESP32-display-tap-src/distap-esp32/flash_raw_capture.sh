#!/usr/bin/env bash
# flash_raw_capture.sh — Build and flash the raw-capture-only test firmware
# (display types 90+, version 99.x.y.z — see PLAN.md). Uses a separate build
# directory (build_raw_capture/) and sdkconfig
# (sdkconfig.raw_capture / sdkconfig.raw_capture.defaults) so it never
# touches the normal production build/binary produced by flash.sh.
#
# Usage:
#   ./flash_raw_capture.sh [--cleanall | --flashall | --flashapp | --console | --release]
#
# Flags:
#   --cleanall  Full clean, build everything, flash everything
#   --flashall  Build without clean, flash everything
#   --flashapp  Build without clean, flash app partition only
#   --console   Open serial monitor (idf.py monitor)
#   --release   Build paired release binaries (even=OTA-test, odd=production)
#               of the raw-capture firmware, mirroring flash.sh --release.
#               Reads CONFIG_APP_PROJECT_VER from sdkconfig.raw_capture.defaults
#               (always major.minor.patch = 99.x.y — see PLAN.md §2 — only the
#               trailing build number is bumped), computes the next odd build
#               number (production) and even = odd-1 (OTA-test), does a clean
#               rebuild for each, copies firmware + bootloader + partition-table
#               binaries into releases_raw_capture/<odd-version>/, creates OTA
#               bundles for all three binary types for both versions, then
#               leaves sdkconfig.raw_capture.defaults at the odd (production)
#               version. Kept in a separate releases_raw_capture/ directory
#               (not releases/) so raw-capture test bundles are never mixed up
#               with production ones.
#
# Variables (edit as needed):
COMPORT="/dev/tty.usbserial-A5069RR4"
IDF_PATH="/Users/chathuranga/DATA/esp/v5.5.3/esp-idf"

# Project root (same directory as this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_DIR/build_raw_capture"
SDKCONFIG_FILE="$PROJECT_DIR/sdkconfig.raw_capture"
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.raw_capture.defaults"

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
    echo "       Check IDF_PATH in flash_raw_capture.sh: $IDF_PATH"
    exit 1
fi

# ── Move to project directory ────────────────────────────────────────────────
cd "$PROJECT_DIR" || { echo "ERROR: Project directory not found: $PROJECT_DIR"; exit 1; }
echo "Project:    $PROJECT_DIR"
echo "Build dir:  $BUILD_DIR"
echo "Port:       $COMPORT"
echo ""

IDF_ARGS=(-B "$BUILD_DIR" -D "SDKCONFIG=$SDKCONFIG_FILE" -D "SDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS")

# ── Execute requested action ─────────────────────────────────────────────────

if $CLEANALL; then
    echo "=== Clean all (raw-capture build) ==="
    rm -rf "$BUILD_DIR" "$SDKCONFIG_FILE"

    echo "=== Build all (raw-capture build) ==="
    idf.py "${IDF_ARGS[@]}" build

    echo "=== Flash all ==="
    idf.py "${IDF_ARGS[@]}" -p "$COMPORT" flash

    echo "=== Done (clean + flash all, raw-capture build) ==="

elif $FLASHALL; then
    echo "=== Build all (raw-capture build) ==="
    idf.py "${IDF_ARGS[@]}" build

    echo "=== Flash all ==="
    idf.py "${IDF_ARGS[@]}" -p "$COMPORT" flash

    echo "=== Done (flash all, raw-capture build) ==="

elif $FLASHAPP; then
    echo "=== Build app (raw-capture build) ==="
    idf.py "${IDF_ARGS[@]}" build

    echo "=== Flash app partition ==="
    idf.py "${IDF_ARGS[@]}" -p "$COMPORT" app-flash

    echo "=== Done (flash app, raw-capture build) ==="

elif $CONSOLE; then
    echo "=== Serial monitor ($COMPORT) ==="
    idf.py "${IDF_ARGS[@]}" monitor -p "$COMPORT" -b 115200

elif $RELEASE; then
    VER_DEFAULTS="$PROJECT_DIR/sdkconfig.raw_capture.defaults"
    BUILD_FW="$BUILD_DIR/distap_esp32.bin"
    BUILD_BOOT="$BUILD_DIR/bootloader/bootloader.bin"
    BUILD_PART="$BUILD_DIR/partition_table/partition-table.bin"
    RELEASES_DIR="$PROJECT_DIR/releases_raw_capture"
    OTA_TOOLS="$PROJECT_DIR/tools/ota-bundle-tools"

    # ── Read current version from sdkconfig.raw_capture.defaults ─────────────
    CURRENT_VER=$(grep '^CONFIG_APP_PROJECT_VER=' "$VER_DEFAULTS" \
                  | sed 's/CONFIG_APP_PROJECT_VER="\(.*\)"/\1/')
    if [[ -z "$CURRENT_VER" ]]; then
        echo "ERROR: Could not parse CONFIG_APP_PROJECT_VER from $VER_DEFAULTS"
        exit 1
    fi
    echo "Current CONFIG_APP_PROJECT_VER: $CURRENT_VER"

    # Split into major.minor.patch.build (4-digit: X.Y.Z.N)
    IFS='.' read -r V1 V2 V3 V4 <<< "$CURRENT_VER"
    if [[ -z "$V4" ]]; then
        echo "ERROR: CONFIG_APP_PROJECT_VER must be in X.Y.Z.N format (got '$CURRENT_VER')"
        exit 1
    fi
    if [[ "$V1" != "99" ]]; then
        echo "WARNING: major version is '$V1', not '99' — see PLAN.md §2 on why"
        echo "         raw-capture builds are expected to stay on major version 99."
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
    echo "=== Release plan (raw-capture build) ==="
    echo "  Even (OTA-test):   $EVEN_VER"
    echo "  Odd  (production): $ODD_VER"
    echo "  Output dir:        $RELEASE_DIR"
    echo ""

    # ── Helper: copy binaries and run all three OTA bundle tools ─────────────
    # Output structure per version:
    #   releases_raw_capture/<odd>/<prefix>_v<ver>/bin/    ← .bin files
    #   releases_raw_capture/<odd>/<prefix>_v<ver>/bundle/ ← .bdl files
    copy_and_bundle() {
        local VER="$1"
        local VER_DIR="$RELEASE_DIR/ferp_dt_esp32_raw_v${VER}"
        local BIN_DIR="$VER_DIR/bin"
        local BDL_DIR="$VER_DIR/bundle"
        mkdir -p "$BIN_DIR" "$BDL_DIR"

        local FW_BIN="$BIN_DIR/distap_esp32_raw_v${VER}.bin"
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

    # ── Helper: set CONFIG_APP_PROJECT_VER in the defaults file and (if
    #    present) the generated sdkconfig — mirrors flash.sh, since an
    #    existing generated sdkconfig takes precedence over defaults and
    #    would otherwise mask the version bump. ────────────────────────────
    set_version() {
        local VER="$1"
        sed -i '' "s/^CONFIG_APP_PROJECT_VER=.*/CONFIG_APP_PROJECT_VER=\"$VER\"/" "$VER_DEFAULTS"
        if [[ -f "$SDKCONFIG_FILE" ]]; then
            sed -i '' "s/^CONFIG_APP_PROJECT_VER=.*/CONFIG_APP_PROJECT_VER=\"$VER\"/" "$SDKCONFIG_FILE"
        fi
    }

    # ── Build 1: even version (OTA-test) ─────────────────────────────────────
    echo "--- Setting CONFIG_APP_PROJECT_VER to $EVEN_VER ---"
    set_version "$EVEN_VER"

    echo "--- Building even version ($EVEN_VER) ---"
    rm -rf "$BUILD_DIR"
    idf.py "${IDF_ARGS[@]}" build
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Build failed for even version $EVEN_VER"
        set_version "$CURRENT_VER"
        exit 1
    fi
    copy_and_bundle "$EVEN_VER"

    # ── Build 2: odd version (production) ────────────────────────────────────
    echo ""
    echo "--- Setting CONFIG_APP_PROJECT_VER to $ODD_VER ---"
    set_version "$ODD_VER"

    echo "--- Building odd version ($ODD_VER) ---"
    rm -rf "$BUILD_DIR"
    idf.py "${IDF_ARGS[@]}" build
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Build failed for odd version $ODD_VER"
        exit 1
    fi
    copy_and_bundle "$ODD_VER"

    echo ""
    echo "=== Release complete: $ODD_VER ==="
    echo "  Output: $RELEASE_DIR/"
    echo "    ferp_dt_esp32_raw_v${EVEN_VER}/bin/    ← firmware, bootloader, partition-table .bin"
    echo "    ferp_dt_esp32_raw_v${EVEN_VER}/bundle/ ← OTA .bdl bundles (flash to test OTA upgrade)"
    echo "    ferp_dt_esp32_raw_v${ODD_VER}/bin/     ← firmware, bootloader, partition-table .bin"
    echo "    ferp_dt_esp32_raw_v${ODD_VER}/bundle/  ← OTA .bdl bundles (production)"
    echo ""
    echo "  sdkconfig.raw_capture.defaults left at: $ODD_VER"
fi
