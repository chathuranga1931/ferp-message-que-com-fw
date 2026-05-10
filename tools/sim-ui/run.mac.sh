#!/usr/bin/env bash
# run.mac.sh — FERP Device Tool launcher for macOS

set -e

REQUIRED_MAJOR=3
REQUIRED_MINOR=13
VENV_DIR=".env"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$SCRIPT_DIR"

# ── 1. Find python3 ────────────────────────────────────────────────────────────
PYTHON=""
for candidate in python3.13 python3 python; do
    if command -v "$candidate" &>/dev/null; then
        PYTHON="$candidate"
        break
    fi
done

if [[ -z "$PYTHON" ]]; then
    echo "ERROR: Python not found on this system."
    echo "Please install Python 3.13 or above from https://www.python.org/downloads/"
    exit 1
fi

# ── 2. Check version ───────────────────────────────────────────────────────────
PYTHON_VERSION=$("$PYTHON" -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
PYTHON_MAJOR=$("$PYTHON" -c "import sys; print(sys.version_info.major)")
PYTHON_MINOR=$("$PYTHON" -c "import sys; print(sys.version_info.minor)")

if [[ "$PYTHON_MAJOR" -lt "$REQUIRED_MAJOR" ]] || \
   { [[ "$PYTHON_MAJOR" -eq "$REQUIRED_MAJOR" ]] && [[ "$PYTHON_MINOR" -lt "$REQUIRED_MINOR" ]]; }; then
    echo "ERROR: Python $PYTHON_VERSION is installed, but Python 3.13 or above is required."
    echo ""
    echo "Please upgrade Python:"
    echo "  • Download from https://www.python.org/downloads/"
    echo "  • Or via Homebrew:  brew install python@3.13"
    exit 1
fi

echo "Python $PYTHON_VERSION found."

# ── 3. Create virtual environment if needed ────────────────────────────────────
if [[ ! -d "$VENV_DIR" ]]; then
    echo "Creating virtual environment..."
    "$PYTHON" -m venv "$VENV_DIR"
fi

# ── 4. Install / update dependencies ──────────────────────────────────────────
echo "Installing requirements..."
"$VENV_DIR/bin/pip" install --quiet --upgrade pip
"$VENV_DIR/bin/pip" install --quiet -r requirements.txt

# ── 5. Run the tool ────────────────────────────────────────────────────────────
echo "Starting FERP Device Tool..."
"$VENV_DIR/bin/python" sim_ui.py
