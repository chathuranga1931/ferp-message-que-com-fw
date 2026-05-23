#!/usr/bin/env bash
# run.mac.sh — FERP Serial Log Viewer launcher for macOS

set -e

REQUIRED_MAJOR=3
REQUIRED_MINOR=10
VENV_DIR=".env"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$SCRIPT_DIR"

# ── 1. Find python3 ────────────────────────────────────────────────────────────
PYTHON=""
for candidate in python3.13 python3.12 python3.11 python3.10 python3 python; do
    if command -v "$candidate" &>/dev/null; then
        PYTHON="$candidate"
        break
    fi
done

if [[ -z "$PYTHON" ]]; then
    echo "ERROR: Python not found on this system."
    echo "Please install Python 3.10 or above from https://www.python.org/downloads/"
    exit 1
fi

# ── 2. Check version ───────────────────────────────────────────────────────────
PYTHON_VERSION=$("$PYTHON" -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
PYTHON_MAJOR=$("$PYTHON" -c "import sys; print(sys.version_info.major)")
PYTHON_MINOR=$("$PYTHON" -c "import sys; print(sys.version_info.minor)")

if [[ "$PYTHON_MAJOR" -lt "$REQUIRED_MAJOR" ]] || \
   { [[ "$PYTHON_MAJOR" -eq "$REQUIRED_MAJOR" ]] && \
     [[ "$PYTHON_MINOR" -lt "$REQUIRED_MINOR" ]]; }; then
    echo "ERROR: Python $PYTHON_VERSION found, but Python 3.10 or above is required."
    echo ""
    echo "Please upgrade Python:"
    echo "  • Download from https://www.python.org/downloads/"
    echo "  • Or via Homebrew:  brew install python@3.13"
    exit 1
fi

echo "Python $PYTHON_VERSION found."

# ── 3. Verify tkinter is available (macOS system Python often lacks it) ────────
if ! "$PYTHON" -c "import tkinter" &>/dev/null 2>&1; then
    echo ""
    echo "ERROR: tkinter is not available in the selected Python ($PYTHON)."
    echo ""
    echo "On macOS, install a Python build that includes Tk:"
    echo "  • Homebrew:    brew install python-tk@3.13"
    echo "  • python.org:  https://www.python.org/downloads/"
    exit 1
fi

# ── 4. Create / validate virtual environment ───────────────────────────────────
if [[ -d "$VENV_DIR" ]]; then
    if ! "$VENV_DIR/bin/python" -c "" &>/dev/null 2>&1; then
        echo "Virtual environment is stale — recreating..."
        rm -rf "$VENV_DIR"
    fi
fi

if [[ ! -d "$VENV_DIR" ]]; then
    echo "Creating virtual environment..."
    "$PYTHON" -m venv "$VENV_DIR"
fi

# ── 5. Install / update dependencies ──────────────────────────────────────────
echo "Installing requirements..."
"$VENV_DIR/bin/pip" install --quiet --upgrade pip
"$VENV_DIR/bin/pip" install --quiet -r requirements.txt

# ── 6. Launch the viewer ───────────────────────────────────────────────────────
echo "Starting FERP Serial Log Viewer..."
"$VENV_DIR/bin/python" serial_log_viewer.py "$@"
