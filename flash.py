#!/usr/bin/env python3
"""flash.py — Build and flash helper for ferp-com-esp32-idf

Usage:
    python3 flash.py --<action> [--serial-port PORT] [--idf-path PATH]

Actions:
  --cleanall       Remove build directory only (no build, no flash)
  --buildall       Build app + SPIFFS (no clean, no flash)
  --buildapp       Build app binary only (no clean, no flash)
  --flashall       Build all (no clean), flash all (app + SPIFFS)
  --flashspiffs    Build + flash SPIFFS partition only
  --flashapp       Build (no clean) + flash app partition only
  --console        Open serial monitor
  --release        Full release workflow (delegates to release.py)
  --create-bundle  Create OTA bundle from existing build artifacts (delegates to release.py)

Typically called via flash.mac.sh or flash.win.bat which supply the
default --serial-port and --idf-path for each platform.
"""

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR / "src/product/ferp-com-main/ferp-com-esp32-idf"
BAUD_RATE = 230400


def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def setup_idf(idf_path: str) -> None:
    if shutil.which("idf.py"):
        return

    if not idf_path:
        die("idf.py not found in PATH. Provide --idf-path to locate the ESP-IDF installation.")

    system = platform.system()
    if system in ("Darwin", "Linux"):
        export_sh = os.path.join(idf_path, "export.sh")
        if not os.path.isfile(export_sh):
            die(f"IDF not found at: {idf_path}\n       (export.sh missing)")
        print("Setting up IDF environment...")
        cmd = (
            f'source "{export_sh}" > /dev/null 2>&1 && '
            'python3 -c "import os, json; print(json.dumps(dict(os.environ)))"'
        )
        result = subprocess.run(["bash", "-c", cmd], capture_output=True, text=True)
        if result.returncode != 0 or not result.stdout.strip():
            die("Failed to set up IDF environment. Check --idf-path.")
        os.environ.update(json.loads(result.stdout.strip()))

    elif system == "Windows":
        export_bat = os.path.join(idf_path, "export.bat")
        if not os.path.isfile(export_bat):
            die(f"IDF not found at: {idf_path}\n       (export.bat missing)")
        print("Setting up IDF environment...")
        cmd = (
            f'call "{export_bat}" > NUL 2>&1 && '
            'python -c "import os, json; print(json.dumps(dict(os.environ)))"'
        )
        result = subprocess.run(["cmd", "/c", cmd], capture_output=True, text=True)
        if result.returncode != 0 or not result.stdout.strip():
            die("Failed to set up IDF environment. Check --idf-path.")
        os.environ.update(json.loads(result.stdout.strip()))

    else:
        die(f"Unsupported platform: {system}")

    if not shutil.which("idf.py"):
        die("idf.py not found after sourcing IDF environment. Check --idf-path.")


def clean_spiffs_data() -> None:
    data_dir = PROJECT_DIR / "data"
    if data_dir.is_dir():
        print(f"Removing macOS ._* sidecar files from {data_dir} ...")
        for f in data_dir.rglob("._*"):
            f.unlink()


def idf(*args: str, port: str = "", baud: int = 0) -> None:
    cmd = ["idf.py"]
    if port:
        cmd += ["-p", port]
    if baud:
        cmd += ["-b", str(baud)]
    cmd += list(args)
    result = subprocess.run(cmd, cwd=str(PROJECT_DIR),
                            shell=(platform.system() == "Windows"))
    if result.returncode != 0:
        sys.exit(result.returncode)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build and flash helper for ferp-com-esp32-idf",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--cleanall", action="store_true", help="Remove build directory only")
    group.add_argument("--buildall", action="store_true", help="Build app + SPIFFS")
    group.add_argument("--buildapp", action="store_true", help="Build app binary only")
    group.add_argument("--flashall", action="store_true", help="Build + flash all")
    group.add_argument("--flashspiffs", action="store_true", help="Build + flash SPIFFS only")
    group.add_argument("--flashapp", action="store_true", help="Build + flash app only")
    group.add_argument("--console", action="store_true", help="Open serial monitor")
    group.add_argument("--release", action="store_true", help="Full release workflow")
    group.add_argument(
        "--create-bundle",
        action="store_true",
        dest="create_bundle",
        help="Create OTA bundle from existing build artifacts",
    )
    parser.add_argument("--serial-port", help="Serial port (e.g. /dev/tty.usbserial-... or COM3)")
    parser.add_argument("--idf-path", help="Path to ESP-IDF installation")
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    # --release and --create-bundle delegate to release.py
    if args.release or args.create_bundle:
        release_py = SCRIPT_DIR / "release.py"
        if not release_py.is_file():
            die(f"release.py not found at: {release_py}")
        cmd = [sys.executable, str(release_py)]
        cmd.append("--release" if args.release else "--create-bundle")
        if args.idf_path:
            cmd += ["--idf-path", args.idf_path]
        result = subprocess.run(cmd)
        sys.exit(result.returncode)

    serial_port = args.serial_port or ""
    idf_path = args.idf_path or ""

    setup_idf(idf_path)

    if not PROJECT_DIR.is_dir():
        die(f"Project directory not found: {PROJECT_DIR}")

    print(f"Project: {PROJECT_DIR}")
    if serial_port:
        print(f"Port:    {serial_port}")
    print()

    if args.cleanall:
        print("=== Clean all ===")
        build_dir = PROJECT_DIR / "build"
        if build_dir.exists():
            shutil.rmtree(str(build_dir))
        print("=== Done (build directory removed) ===")

    elif args.buildall:
        clean_spiffs_data()
        print("=== Build app + SPIFFS ===")
        idf("build")
        print("=== Done (build complete — use --flashall or --flashapp to flash) ===")

    elif args.buildapp:
        print("=== Build app ===")
        idf("app")
        print("=== Done (app build complete — use --flashapp to flash) ===")

    elif args.flashall:
        if not serial_port:
            die("--serial-port is required for --flashall")
        clean_spiffs_data()
        print("=== Build all ===")
        idf("build")
        print("=== Flash all (app + SPIFFS) ===")
        idf("flash", port=serial_port, baud=BAUD_RATE)
        print("=== Done (flash all) ===")

    elif args.flashspiffs:
        if not serial_port:
            die("--serial-port is required for --flashspiffs")
        clean_spiffs_data()
        print("=== Build SPIFFS image ===")
        idf("build", "spiffs")
        print("=== Flash SPIFFS partition ===")
        idf("spiffs", port=serial_port, baud=BAUD_RATE)
        print("=== Done (flash SPIFFS) ===")

    elif args.flashapp:
        if not serial_port:
            die("--serial-port is required for --flashapp")
        print("=== Build app ===")
        idf("build")
        print("=== Flash app partition ===")
        idf("app-flash", port=serial_port, baud=BAUD_RATE)
        print("=== Done (flash app) ===")

    elif args.console:
        if not serial_port:
            die("--serial-port is required for --console")
        print(f"=== Serial monitor ({serial_port}) ===")
        idf("monitor", port=serial_port, baud=BAUD_RATE)


if __name__ == "__main__":
    main()
