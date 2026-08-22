#!/usr/bin/env bash
# build-esp07-dt.win.sh — Windows (MSYS2 / Git Bash) wrapper for the
# distap-esp07 board's own build.py.
#
# The ESP07 (ESP8266) display-tap board builds against ESP8266_RTOS_SDK
# (release/v3.4) with the xtensa-lx106-elf toolchain — a completely separate
# SDK/toolchain from the mainline ESP-IDF used by the esp32 products in this
# repo (see build.win.bat / build-v2.mac.sh / build-v3.mac.sh for those).
# Unlike mainline ESP-IDF, this SDK version ships NO export.bat/install.bat
# for native Windows — only export.sh/install.sh. This board's own build.py
# already handles that by sourcing export.sh via `bash -c` itself, so this
# wrapper only needs to run from a shell that has bash.exe on PATH (true for
# both MSYS2 and Git for Windows) — it does not need to run "inside" MSYS2's
# own isolated environment.
#
# ── ONE-TIME TOOLCHAIN SETUP (run from an MSYS2 or Git Bash shell) ──────────
#   1. Clone the SDK — a FULL clone, not --depth 1 (idf_tools.py calls
#      `git describe --tags`, which fails on a shallow clone):
#        git clone -b release/v3.4 --recursive \
#          https://github.com/espressif/ESP8266_RTOS_SDK.git \
#          /d/esp/ESP8266_RTOS_SDK
#   2. Install its toolchain + Python env (downloads xtensa-lx106-elf into
#      ~/.espressif, alongside — not conflicting with — the xtensa-esp32-elf
#      toolchain already used by the esp32 products here):
#        cd /d/esp/ESP8266_RTOS_SDK
#        ./install.sh
#   3. If pkg_resources import fails the first time export.sh runs (recent
#      setuptools dropped it from the venv by default):
#        <path shown by install.sh for the venv>/bin/python -m pip install "setuptools<81"
#   4. If a recent CMake (3.31+/4.x) errors on the vendored mbedtls's
#      `cmake_minimum_required(VERSION < 3.5)`, export this before building:
#        export CMAKE_POLICY_VERSION_MINIMUM=3.5
#   5. Update IDF_PATH below if your SDK checkout path differs.
#
# See claud-context-esp07-build.md for the full writeup (this board also
# needed a handful of CMakeLists.txt REQUIRES/PRIV_REQUIRES fixes to build
# via idf.py at all — already committed to the project, nothing to redo).
#
# Usage:
#   ./build-esp07-dt.win.sh [--cleanall | --flashall | --flashapp | --console | --release] [--out <output_dir>]
#
# Override the serial port at runtime:
#   ./build-esp07-dt.win.sh --serial-port COM5 --flashapp
#
# --release output location: defaults to releases/dt-esp07/ at the repo
# root (pass your own --out to override) — matching where the root build.py's
# own --release-dt-esp32/--release-dt-esp07 delegation already documents esp07
# releases as landing. distap-esp07/build.py's OWN default (no --out at all)
# would instead nest output inside distap-esp07/releases/, which is easy to
# miss if you're used to checking the repo-root releases/ folder.

# ── Windows defaults (edit as needed) ────────────────────────────────────────
COMPORT="COM3"
IDF_PATH="/d/esp/ESP8266_RTOS_SDK"

# ── Parse --serial-port override; forward everything else to build.py ───────
SERIAL_PORT="$COMPORT"
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
DT_ESP07_DIR="$SCRIPT_DIR/src/sub-modules/ferp-device-firmware/ferp_board/distap-esp07"

# Use python3 if available, else fall back to python (whichever this shell
# resolves — both are fine, the SDK env sourced by build.py needs its own
# `python` on PATH regardless of which one invokes this script).
PYTHON=python3
command -v python3 >/dev/null 2>&1 || PYTHON=python

exec "$PYTHON" "$DT_ESP07_DIR/build.py" \
    --serial-port "$SERIAL_PORT" \
    --idf-path "$IDF_PATH" \
    --out "$SCRIPT_DIR/releases/dt-esp07" \
    "${PASS_ARGS[@]}"
