
#!/usr/bin/env bash
# flash.sh — Build and flash helper for ferp-com-esp32-idf
#
# Usage:
#   ./flash.sh [--cleanall | --buildall | --flashall | --flashspiffs | --flashapp | --console | --release | --create-bundle]
#
# Flags:
#   --cleanall       Remove the build directory only (no build, no flash)
#   --buildall       Build app + SPIFFS image (no clean, no flash)
#   --buildapp       Build app binary only (no clean, no flash)
#   --flashall       Build without clean, flash everything (app + SPIFFS)
#   --flashspiffs    Build SPIFFS image only, flash SPIFFS partition only
#   --flashapp       Build without clean, flash app partition only
#   --console        Open serial monitor (idf.py monitor)
#   --release        Full release: git checks → odd build → commit+tag+push →
#                    even build → factory image → tar.gz → GitHub release.
#                    Requires clean git working tree and remote access.
#   --create-bundle  Create OTA bundle from existing build artifacts.
#                    No build, no git. Version: (255-V1).V2.V3.V4.
#                    Output goes to the build directory.
#
# Variables (edit as needed):

# if virtual is not working, run this once
# /Users/chathuranga/.espressif/v6.0.1/esp-idf/install.sh esp32

# COMPORT="/dev/tty.usbserial-A5069RR4"
COMPORT="/dev/tty.usbserial-548B0018381"

# /Users/chathuranga/.espressif/tools/activate_idf_v6.0.1.sh'
# /Users/chathuranga/.espressif/v6.0.1/esp-idf
IDF_PATH="/Users/chathuranga/.espressif/v6.0.1/esp-idf"

# Project root (relative to this script)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/src/product/ferp-com-main/ferp-com-esp32-idf"

# ── Parse flags ──────────────────────────────────────────────────────────────
CLEANALL=false
BUILDALL=false
BUILDAPP=false
FLASHALL=false
FLASHSPIFFS=false
FLASHAPP=false
CONSOLE=false
RELEASE=false
CREATE_BUNDLE=false

for arg in "$@"; do
    case "$arg" in
        --cleanall)       CLEANALL=true       ;;
        --buildall)       BUILDALL=true       ;;
        --buildapp)       BUILDAPP=true       ;;
        --flashall)       FLASHALL=true       ;;
        --flashspiffs)    FLASHSPIFFS=true    ;;
        --flashapp)       FLASHAPP=true       ;;
        --console)        CONSOLE=true        ;;
        --release)        RELEASE=true        ;;
        --create-bundle)  CREATE_BUNDLE=true  ;;
        *)
            echo "ERROR: Unknown flag: $arg"
            echo "Usage: $0 [--cleanall | --buildall | --buildapp | --flashall | --flashspiffs | --flashapp | --console | --release | --create-bundle]"
            exit 1
            ;;
    esac
done

if ! $CLEANALL && ! $BUILDALL && ! $BUILDAPP && ! $FLASHALL && ! $FLASHSPIFFS && ! $FLASHAPP && ! $CONSOLE && ! $RELEASE && ! $CREATE_BUNDLE; then
    echo "Usage: $0 [--cleanall | --buildall | --buildapp | --flashall | --flashspiffs | --flashapp | --console | --release | --create-bundle]"
    echo ""
    echo "  --cleanall       Remove build directory only (no build, no flash)"
    echo "  --buildall       Build app + SPIFFS (no clean, no flash)"
    echo "  --buildapp       Build app binary only (no clean, no flash)"
    echo "  --flashall       Build all (no clean), flash all (app + SPIFFS)"
    echo "  --flashspiffs    Build + flash SPIFFS partition only"
    echo "  --flashapp       Build (no clean) + flash app partition only"
    echo "  --console        Open serial monitor"
    echo "  --release        Full release: git checks → odd build → commit+tag+push → even build → GitHub release"
    echo "  --create-bundle  Bundle existing build artifacts; version (255-V1).V2.V3.V4; output to build dir"
    exit 1
fi

# ── Release and bundle modes are delegated to release.sh ─────────────────────
# release.sh sets up IDF itself when needed, so we skip the IDF block below.
if $RELEASE; then
    exec bash "$SCRIPT_DIR/release.sh" --release
fi
if $CREATE_BUNDLE; then
    exec bash "$SCRIPT_DIR/release.sh" --create-bundle
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

# ── Helper: strip macOS resource-fork sidecar files from the SPIFFS data dir ─
# macOS creates "._filename" Apple Double files alongside every file it writes
# to non-APFS volumes.  These leak into the SPIFFS image if not removed first.
clean_spiffs_data() {
    local DATA_DIR="$PROJECT_DIR/data"
    if [[ -d "$DATA_DIR" ]]; then
        echo "Removing macOS ._* sidecar files from $DATA_DIR ..."
        find "$DATA_DIR" -name "._*" -delete
    fi
}

# ── Move to project directory ────────────────────────────────────────────────
cd "$PROJECT_DIR" || { echo "ERROR: Project directory not found: $PROJECT_DIR"; exit 1; }
echo "Project: $PROJECT_DIR"
echo "Port:    $COMPORT"
echo ""

# ── Execute requested action ─────────────────────────────────────────────────

if $CLEANALL; then
    echo "=== Clean all ==="
    # Use rm -rf directly to avoid idf.py fullclean crashing on stale macOS temp files
    rm -rf "$PROJECT_DIR/build"
    echo "=== Done (build directory removed) ==="

elif $BUILDALL; then
    clean_spiffs_data
    echo "=== Build app + SPIFFS ==="
    idf.py build

    echo "=== Done (build complete — use --flashall or --flashapp to flash) ==="

elif $BUILDAPP; then
    echo "=== Build app ==="
    idf.py app

    echo "=== Done (app build complete — use --flashapp to flash) ==="

elif $FLASHALL; then
    clean_spiffs_data
    echo "=== Build all ==="
    idf.py build

    echo "=== Flash all (app + SPIFFS) ==="
    idf.py -p "$COMPORT" -b 230400 flash

    echo "=== Done (flash all) ==="

elif $FLASHSPIFFS; then
    clean_spiffs_data
    echo "=== Build SPIFFS image ==="
    idf.py build spiffs

    echo "=== Flash SPIFFS partition ==="
    idf.py -p "$COMPORT" -b 230400 spiffs

    echo "=== Done (flash SPIFFS) ==="

elif $FLASHAPP; then
    echo "=== Build app ==="
    idf.py build

    echo "=== Flash app partition ==="
    idf.py -p "$COMPORT" -b 230400 app-flash

    echo "=== Done (flash app) ==="

elif $CONSOLE; then
    echo "=== Serial monitor ($COMPORT) ==="
    idf.py monitor -p "$COMPORT" -b 230400
fi

