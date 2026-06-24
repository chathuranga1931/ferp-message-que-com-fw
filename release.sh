#!/usr/bin/env bash
# release.sh — Release and bundle creation for ferp-com-esp32-idf
#
# Invoked by flash.sh via --release or --create-bundle flags.
# Can also be run directly.
#
# Modes:
#   --release        Full release: git checks → odd build → commit+tag+push →
#                    even build → factory image → tar.gz → GitHub release
#   --create-bundle  Bundle only from existing build artifacts (no build, no git).
#                    Bundle version is (255-V1).V2.V3.V4 of the current version.h.
#                    Output goes to the build directory.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/src/product/ferp-com-main/ferp-com-esp32-idf"
IDF_PATH="/Users/chathuranga/.espressif/v6.0.1/esp-idf"
VERSION_FILE="$SCRIPT_DIR/src/product/ferp-com-main/app/version.h"
USER_CONFIG_FILE="$SCRIPT_DIR/src/product/ferp-com-main/app/user_config.h"
BUILD_DIR="$PROJECT_DIR/build"
BUILD_BIN="$BUILD_DIR/ferp-com.bin"
BUILD_ELF="$BUILD_DIR/ferp-com.elf"
RELEASES_DIR="$SCRIPT_DIR/releases"
BUNDLE_TOOL="$SCRIPT_DIR/tools/ota-bundle-tools/OtaBundleCreate-MainESP32.py"

# ── Parse mode ────────────────────────────────────────────────────────────────
MODE="${1:-}"
if [[ "$MODE" != "--release" && "$MODE" != "--create-bundle" ]]; then
    echo "Usage: $0 [--release | --create-bundle]"
    exit 1
fi

# ── Helpers ───────────────────────────────────────────────────────────────────

_read_version() {
    local ver
    ver=$(grep '#define FW_VERSION' "$VERSION_FILE" | sed 's/.*"\(.*\)".*/\1/')
    if [[ -z "$ver" ]]; then
        echo "ERROR: Could not parse FW_VERSION from $VERSION_FILE" >&2
        exit 1
    fi
    echo "$ver"
}

_set_version() {
    sed -i '' "s/#define FW_VERSION[[:space:]]*\"[^\"]*\"/#define FW_VERSION          \"$1\"/" "$VERSION_FILE"
}

_setup_idf() {
    if command -v idf.py &>/dev/null; then return; fi
    if [[ ! -f "$IDF_PATH/export.sh" ]]; then
        echo "ERROR: IDF not found at: $IDF_PATH" >&2
        exit 1
    fi
    echo "Setting up IDF environment..."
    # shellcheck source=/dev/null
    source "$IDF_PATH/export.sh"
    if ! command -v idf.py &>/dev/null; then
        echo "ERROR: idf.py not found after sourcing IDF." >&2
        exit 1
    fi
}

_build() {
    cd "$PROJECT_DIR"
    idf.py build
    cd "$SCRIPT_DIR"
}

_copy_artifacts() {
    local ver="$1" dest="$2"
    cp "$BUILD_BIN" "$dest/ferp-com-v${ver}.bin"
    cp "$BUILD_ELF" "$dest/ferp-com-v${ver}.elf"
    echo "  Saved: ferp-com-v${ver}.bin"
    echo "  Saved: ferp-com-v${ver}.elf"
}

_create_bundle() {
    local bin="$1" ver="$2" outdir="$3"
    python3 "$BUNDLE_TOOL" "$bin" "$ver" "$outdir"
}

# ╔══════════════════════════════════════════════════════════════════════════════
# ║  --create-bundle
# ╚══════════════════════════════════════════════════════════════════════════════
if [[ "$MODE" == "--create-bundle" ]]; then
    echo "=== Create bundle from existing build artifacts ==="

    if [[ ! -f "$BUILD_BIN" ]]; then
        echo "ERROR: Build binary not found: $BUILD_BIN"
        echo "       Run --buildall first."
        exit 1
    fi

    CURRENT_VER=$(_read_version)
    IFS='.' read -r V1 V2 V3 V4 <<< "$CURRENT_VER"
    BUNDLE_VER="$((255 - V1)).$V2.$V3.$V4"

    echo "  Current version : $CURRENT_VER"
    echo "  Bundle version  : $BUNDLE_VER  (255-$V1 downgrade marker)"
    echo "  Output dir      : $BUILD_DIR"
    echo ""

    _create_bundle "$BUILD_BIN" "$BUNDLE_VER" "$BUILD_DIR"

    echo ""
    echo "=== Bundle created in build dir ==="
    exit 0
fi

# ╔══════════════════════════════════════════════════════════════════════════════
# ║  --release
# ╚══════════════════════════════════════════════════════════════════════════════

# ── Pre-release git checks ────────────────────────────────────────────────────
echo "=== Pre-release checks ==="

echo "Checking git status..."
UNCOMMITTED=$(git -C "$SCRIPT_DIR" status --porcelain)
if [[ -n "$UNCOMMITTED" ]]; then
    echo ""
    echo "ERROR: Working tree has uncommitted changes. Commit or stash before releasing."
    echo ""
    git -C "$SCRIPT_DIR" status --short
    exit 1
fi
echo "  ✓ Working tree is clean"

echo "Checking git remote access..."
if ! git -C "$SCRIPT_DIR" ls-remote origin HEAD &>/dev/null; then
    echo "ERROR: Cannot reach git remote 'origin'. Check SSH key / network connection."
    exit 1
fi
echo "  ✓ Remote 'origin' is reachable"

# ── Check / install GitHub CLI ────────────────────────────────────────────────
echo "Checking for GitHub CLI (gh)..."
if ! command -v gh &>/dev/null; then
    echo "  gh not found — installing via Homebrew..."
    if ! command -v brew &>/dev/null; then
        echo "ERROR: Homebrew not found. Install gh manually: https://cli.github.com"
        exit 1
    fi
    brew install gh
fi
if ! command -v gh &>/dev/null; then
    echo "ERROR: gh install failed. Install manually: https://cli.github.com"
    exit 1
fi
echo "  ✓ gh $(gh --version | head -1)"

if ! gh auth status &>/dev/null; then
    echo ""
    echo "ERROR: gh is not authenticated."
    echo "       Run:  gh auth login"
    echo "       Then: ./flash.sh --release"
    exit 1
fi
echo "  ✓ gh authenticated"

# ── Compute release versions ──────────────────────────────────────────────────
CURRENT_VER=$(_read_version)
echo ""
echo "Current FW_VERSION: $CURRENT_VER"

IFS='.' read -r V1 V2 V3 V4 <<< "$CURRENT_VER"
if [[ -z "${V4:-}" ]]; then
    echo "ERROR: FW_VERSION must be in X.Y.Z.N format (got '$CURRENT_VER')"
    exit 1
fi

# ODD = production release, EVEN = OTA-test (one higher than odd so OTA
# accepts the upgrade when testing the update path before full production deploy).
# If version.h was left at even (interrupted release), step +3 to skip the gap.
if (( V4 % 2 == 0 )); then
    ODD_BUILD=$((V4 + 3))
else
    ODD_BUILD=$((V4 + 2))
fi
EVEN_BUILD=$((ODD_BUILD + 1))

ODD_VER="$V1.$V2.$V3.$ODD_BUILD"
EVEN_VER="$V1.$V2.$V3.$EVEN_BUILD"
RELEASE_DIR="$RELEASES_DIR/$ODD_VER"
mkdir -p "$RELEASE_DIR"

echo ""
echo "=== Release plan ==="
echo "  Odd  (production): $ODD_VER"
echo "  Even (OTA-test):   $EVEN_VER"
echo "  Output dir:        $RELEASE_DIR"
echo ""

# ── Set up IDF environment ────────────────────────────────────────────────────
_setup_idf

# ── Disable TEST_PUMP_BUILD if active ─────────────────────────────────────────
_test_pump_was_active=false
if grep -q '^#define TEST_PUMP_BUILD' "$USER_CONFIG_FILE" 2>/dev/null; then
    _test_pump_was_active=true
    sed -i '' 's/^#define TEST_PUMP_BUILD/\/\/#define TEST_PUMP_BUILD/' "$USER_CONFIG_FILE"
    echo "NOTE: TEST_PUMP_BUILD disabled for release builds (restored on exit)"
fi

# Cleanup trap: always restore version.h to ODD_VER and TEST_PUMP_BUILD.
# Even on failure this leaves the workspace in the committed state.
_cleanup() {
    _set_version "$ODD_VER" 2>/dev/null || true
    if $_test_pump_was_active; then
        sed -i '' 's/^\/\/#define TEST_PUMP_BUILD/#define TEST_PUMP_BUILD/' "$USER_CONFIG_FILE" 2>/dev/null || true
        echo "NOTE: TEST_PUMP_BUILD restored in user_config.h"
    fi
}
trap _cleanup EXIT

# ── Build 1: ODD version (production) ────────────────────────────────────────
echo "--- Setting FW_VERSION to $ODD_VER ---"
_set_version "$ODD_VER"

echo "--- Building odd version ($ODD_VER) ---"
_build

echo ""
echo "--- Saving ODD artifacts ---"
_copy_artifacts "$ODD_VER" "$RELEASE_DIR"
_create_bundle "$RELEASE_DIR/ferp-com-v${ODD_VER}.bin" "$ODD_VER" "$RELEASE_DIR"

# ── Commit, tag, and push ODD ─────────────────────────────────────────────────
echo ""
echo "--- Committing version.h at $ODD_VER ---"
git -C "$SCRIPT_DIR" add "$VERSION_FILE"
git -C "$SCRIPT_DIR" commit -m "version bumped"

echo "--- Tagging v${ODD_VER} ---"
git -C "$SCRIPT_DIR" tag "v${ODD_VER}"
echo "  Tagged: v${ODD_VER}"

CURRENT_BRANCH=$(git -C "$SCRIPT_DIR" rev-parse --abbrev-ref HEAD)
echo "--- Pushing branch '$CURRENT_BRANCH' and tag v${ODD_VER} to origin ---"
git -C "$SCRIPT_DIR" push origin "$CURRENT_BRANCH"
git -C "$SCRIPT_DIR" push origin "v${ODD_VER}"
echo "  ✓ Pushed"

# ── Build 2: EVEN version (OTA-test) ─────────────────────────────────────────
echo ""
echo "--- Setting FW_VERSION to $EVEN_VER ---"
_set_version "$EVEN_VER"

echo "--- Building even version ($EVEN_VER) ---"
_build

echo ""
echo "--- Saving EVEN artifacts ---"
_copy_artifacts "$EVEN_VER" "$RELEASE_DIR"
_create_bundle "$RELEASE_DIR/ferp-com-v${EVEN_VER}.bin" "$EVEN_VER" "$RELEASE_DIR"

# ── Factory image (ODD app + shared build artifacts) ─────────────────────────
# Bootloader, partition table, ota_data, and SPIFFS are identical across both
# builds — only ferp-com.bin differs.  Use the saved ODD bin for the app slot.
echo ""
echo "--- Creating 4MB factory image ($ODD_VER) ---"
FACTORY_BIN="$RELEASE_DIR/ferp-esp32-factory-v${ODD_VER}.bin"
python3 "$IDF_PATH/components/esptool_py/esptool/esptool.py" --chip esp32 merge_bin \
    --flash_mode  dio \
    --flash_freq  40m \
    --flash_size  4MB \
    --fill-flash-size 4MB \
    -o "$FACTORY_BIN" \
    0x1000    "$BUILD_DIR/bootloader/bootloader.bin" \
    0x8000    "$BUILD_DIR/partition_table/partition-table.bin" \
    0xe000    "$BUILD_DIR/ota_data_initial.bin" \
    0x10000   "$RELEASE_DIR/ferp-com-v${ODD_VER}.bin" \
    0x370000  "$BUILD_DIR/spiffs.bin" \
    && echo "  Saved: $FACTORY_BIN" \
    || echo "WARNING: Failed to create factory image (check esptool.py)"

# ── Package into tar.gz ───────────────────────────────────────────────────────
echo ""
echo "--- Creating release archive ---"
TAR_NAME="ferp-com-release-v${ODD_VER}.tar.gz"
TAR_PATH="$RELEASE_DIR/$TAR_NAME"
tar -czf "$TAR_PATH" -C "$RELEASES_DIR" "$ODD_VER" --exclude="$TAR_NAME"
echo "  Saved: $TAR_PATH"

# ── GitHub release ────────────────────────────────────────────────────────────
echo ""
echo "--- Creating GitHub release v${ODD_VER} ---"

REPO=$(git -C "$SCRIPT_DIR" remote get-url origin \
    | sed 's|git@github.com:||;s|\.git$||')

RELEASE_NOTES="Production release v${ODD_VER}

- **Production binary**: ferp-com-v${ODD_VER}.bin
- **OTA-test binary**:   ferp-com-v${EVEN_VER}.bin  ← flash odd first, then OTA to even
- **Factory image**:     ferp-esp32-factory-v${ODD_VER}.bin  (flash to 0x0)
- **ELF files** included for crash backtrace decoding (addr2line)"

gh release create "v${ODD_VER}" \
    --repo "$REPO" \
    --title "v${ODD_VER}" \
    --notes "$RELEASE_NOTES" \
    "$TAR_PATH" \
    "$RELEASE_DIR/ferp-com-v${ODD_VER}.bin" \
    "$RELEASE_DIR/ferp-com-v${ODD_VER}.elf" \
    "$RELEASE_DIR/ferp-com-v${EVEN_VER}.bin" \
    "$RELEASE_DIR/ferp-com-v${EVEN_VER}.elf" \
    "$FACTORY_BIN" \
    "$RELEASE_DIR/ferp_esp32_main_v${ODD_VER}.bdl" \
    "$RELEASE_DIR/ferp_esp32_main_v${EVEN_VER}.bdl"

echo ""
echo "========================================================"
echo "=== Release v${ODD_VER} complete                     ==="
echo "========================================================"
echo ""
echo "  Production    : ferp-com-v${ODD_VER}.bin"
echo "  OTA-test      : ferp-com-v${EVEN_VER}.bin"
echo "  ELF (debug)   : ferp-com-v${ODD_VER}.elf + ferp-com-v${EVEN_VER}.elf"
echo "  Factory image : ferp-esp32-factory-v${ODD_VER}.bin"
echo "  Archive       : $TAR_NAME"
echo "  git tag       : v${ODD_VER} (pushed to origin/$CURRENT_BRANCH)"
echo "  GitHub release: https://github.com/$REPO/releases/tag/v${ODD_VER}"
echo ""
echo "  version.h left at: $ODD_VER"
echo ""
