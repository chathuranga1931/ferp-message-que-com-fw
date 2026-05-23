#!/usr/bin/env bash
# flash.sh — Build and flash helper for disp_emulator_mega (Arduino Mega 2560)
#
# Usage:
#   ./flash.sh [--build | --flash | --upload | --console | --clean]
#
# Flags:
#   --build    Compile the sketch only (no upload)
#   --flash    Compile + upload to the board
#   --console  Open serial monitor (press Ctrl-C to exit)
#   --clean    Delete the build cache, then compile + upload
#
# Configuration is loaded from the ENVIRONMENT file in the same directory.
# ─────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKETCH_DIR="$SCRIPT_DIR"
ENV_FILE="$SCRIPT_DIR/ENVIRONMENT"

# ── Load ENVIRONMENT ──────────────────────────────────────────────────────────
if [[ ! -f "$ENV_FILE" ]]; then
    echo "ERROR: ENVIRONMENT file not found at: $ENV_FILE"
    exit 1
fi
# shellcheck source=ENVIRONMENT
source "$ENV_FILE"

# ── Validate arduino-cli ──────────────────────────────────────────────────────
if [[ ! -x "$ARDUINO_CLI" ]]; then
    echo "ERROR: arduino-cli not found or not executable at:"
    echo "       $ARDUINO_CLI"
    echo "       Edit ARDUINO_CLI in the ENVIRONMENT file."
    exit 1
fi

# ── Parse flags ───────────────────────────────────────────────────────────────
BUILD=false
FLASH=false
CONSOLE=false
CLEAN=false

for arg in "$@"; do
    case "$arg" in
        --build)   BUILD=true   ;;
        --flash)   FLASH=true   ;;
        --console) CONSOLE=true ;;
        --clean)   CLEAN=true   ;;
        *)
            echo "ERROR: Unknown flag: $arg"
            echo "Usage: $0 [--build | --flash | --console | --clean]"
            exit 1
            ;;
    esac
done

if ! $BUILD && ! $FLASH && ! $CONSOLE && ! $CLEAN; then
    echo "Usage: $0 [--build | --flash | --console | --clean]"
    echo ""
    echo "  --build    Compile only"
    echo "  --flash    Compile + upload to board"
    echo "  --console  Open serial monitor (Ctrl-C to exit)"
    echo "  --clean    Wipe build cache, then compile + upload"
    echo ""
    echo "Configuration: $ENV_FILE"
    exit 1
fi

# ── Clean ─────────────────────────────────────────────────────────────────────
if $CLEAN; then
    echo "Cleaning build cache: $BUILD_PATH"
    rm -rf "$BUILD_PATH"
    FLASH=true   # clean implies full rebuild + upload
fi

# ── Build ─────────────────────────────────────────────────────────────────────
if $BUILD || $FLASH; then
    echo "Compiling for $FQBN ..."
    "$ARDUINO_CLI" compile \
        --fqbn "$FQBN" \
        --build-path "$BUILD_PATH" \
        "$SKETCH_DIR" \
        2>&1

    if [[ $? -ne 0 ]]; then
        echo ""
        echo "ERROR: Compilation failed."
        exit 1
    fi
    echo "Compilation successful."
fi

# ── Upload ────────────────────────────────────────────────────────────────────
if $FLASH; then
    if [[ -z "$COMPORT" ]]; then
        echo "ERROR: COMPORT is not set in ENVIRONMENT."
        exit 1
    fi

    # ⚠ NOTE: The board is programmed via TX0/RX0 through an external USB-serial
    # adapter. There is no auto-reset (DTR is not wired to RESET).
    #
    # Recommended method — hold-then-release RESET:
    #   Wire a button (or jumper) between Mega RESET pin and GND.
    #   Holding it keeps the board in reset indefinitely.
    #   Release exactly when avrdude starts (after the prompt below).
    #
    # Alternative: press the on-board RESET button when prompted and
    #   release it immediately — you have ~5 seconds before the bootloader exits.
    echo ""
    echo "┌──────────────────────────────────────────────────────────────┐"
    echo "│  HOLD RESET (RESET pin → GND) or press the RESET button     │"
    echo "│  then press Enter — avrdude will start and you RELEASE reset │"
    echo "│  The bootloader stays active as long as RESET is held low.   │"
    echo "└──────────────────────────────────────────────────────────────┘"
    read -r -p "  → Hold RESET on the board, then press Enter: "

    echo "Uploading to $COMPORT ..."
    "$ARDUINO_CLI" upload \
        --fqbn "$FQBN" \
        --port "$COMPORT" \
        --input-dir "$BUILD_PATH" \
        "$SKETCH_DIR" \
        2>&1

    if [[ $? -ne 0 ]]; then
        echo ""
        echo "ERROR: Upload failed. Tips:"
        echo "  - Press RESET on the board and retry within 5 seconds"
        echo "  - Confirm TX0→RX (adapter) and RX0→TX (adapter) are crossed correctly"
        echo "  - Check nothing else is using $COMPORT"
        exit 1
    fi
    echo "Upload complete."
fi

# ── Console ───────────────────────────────────────────────────────────────────
if $CONSOLE; then
    if [[ -z "$COMPORT" ]]; then
        echo "ERROR: COMPORT is not set in ENVIRONMENT."
        exit 1
    fi
    echo "Opening serial monitor on $COMPORT at ${BAUD} baud (Ctrl-C to exit)..."
    "$ARDUINO_CLI" monitor \
        --port "$COMPORT" \
        --config baudrate="$BAUD" \
        2>&1
fi
