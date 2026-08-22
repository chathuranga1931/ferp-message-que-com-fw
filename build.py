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
  --release-flash  Flash a pre-built release factory image (0x0) — no build step.
                    Requires --serial-port and --bin-path.
  --console        Open serial monitor
  --release        Full release workflow (delegates to release.py).
                    Output defaults to releases/<product>-esp32/<version>/ —
                    pass --out <dir> to override. --product selects v3
                    (default) or v2.
  --create-bundle  Create OTA bundle from existing build artifacts (delegates to release.py)
  --release-dt-esp32      Release the distap-esp32 (DT board) production firmware
                          (delegates to distap-esp32/build.py --release,
                          output under releases/dt-esp32/)
  --release-dt-esp32-raw  Release the distap-esp32 raw-capture test firmware
                          (delegates to distap-esp32/build_raw_capture.py --release,
                          output under releases/dt-esp32/)

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

# ── Product selection ────────────────────────────────────────────────────────
#   v3 : ferp-com-v3-main       (esp32<>esp32, HW 2602)
#   v2 : ferp-com-v2-main       (esp32<>esp07, HW 2404)
PRODUCTS = {
    "v3": "src/product/ferp-com-v3-main/ferp-com-v3-esp32-idf",
    "v2": "src/product/ferp-com-v2-main/ferp-com-v2-esp32-idf",
}

# Defaults to "v3" for backward compatibility; overridden in main() via --product.
PROJECT_DIR = SCRIPT_DIR / PRODUCTS["v3"]
BAUD_RATE = 230400
# Matches releases/flash-factory-bin.sh — factory images are flashed directly
# with esptool (not idf.py) at a higher baud since there's no app-partition
# offset math involved, just one raw write at 0x0.
FACTORY_BAUD_RATE = 1152000
FACTORY_FLASH_OFFSET = "0x0"


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
    group.add_argument(
        "--release-flash",
        action="store_true",
        dest="release_flash",
        help="Flash a pre-built release factory image (0x0) — no build step",
    )
    group.add_argument("--console", action="store_true", help="Open serial monitor")
    group.add_argument("--release", action="store_true", help="Full release workflow")
    group.add_argument(
        "--create-bundle",
        action="store_true",
        dest="create_bundle",
        help="Create OTA bundle from existing build artifacts",
    )
    group.add_argument(
        "--release-dt-esp32",
        action="store_true",
        dest="release_dt_esp32",
        help="Release the distap-esp32 (DT board) production firmware",
    )
    group.add_argument(
        "--release-dt-esp32-raw",
        action="store_true",
        dest="release_dt_esp32_raw",
        help="Release the distap-esp32 raw-capture test firmware",
    )
    parser.add_argument("--serial-port", help="Serial port (e.g. /dev/tty.usbserial-... or COM3)")
    parser.add_argument("--idf-path", help="Path to ESP-IDF installation")
    parser.add_argument(
        "--product",
        default="v3",
        choices=list(PRODUCTS),
        help="Which product to build/flash/release (default: v3)",
    )
    parser.add_argument(
        "--out",
        dest="out_dir",
        help="With --release, output dir override (default: releases/<product>-esp32)",
    )
    parser.add_argument(
        "--bin-path",
        dest="bin_path",
        help="Path to the release factory .bin file (required for --release-flash)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    global PROJECT_DIR
    PROJECT_DIR = SCRIPT_DIR / PRODUCTS[args.product]

    # --release-dt-esp32 and --release-dt-esp32-raw delegate to the
    # distap-esp32 board's own build.py / build_raw_capture.py, which is a
    # separate ESP-IDF project tree requiring its own toolchain version
    # (v5.5.3, vs this project's own --idf-path default). --idf-path is
    # deliberately NOT forwarded here: build.mac.sh/build.win.bat always
    # pass --idf-path explicitly for *this* project, so "was it explicitly
    # given" can't be used to distinguish a real override from the
    # wrapper's own default — forwarding it unconditionally silently broke
    # the distap-esp32 build the first time this shipped (wrong IDF version,
    # missing 'json' component). To point distap-esp32's release at a
    # different IDF install, invoke its own build.sh/build.py directly.
    if args.release_dt_esp32 or args.release_dt_esp32_raw:
        dt_dir = SCRIPT_DIR / "src/sub-modules/ferp-device-firmware/ferp_board/distap-esp32"
        dt_script = dt_dir / ("build_raw_capture.py" if args.release_dt_esp32_raw else "build.py")
        if not dt_script.is_file():
            die(f"Script not found: {dt_script}")
        out_dir = SCRIPT_DIR / "releases" / "dt-esp32"
        cmd = [sys.executable, str(dt_script), "--release", "--out", str(out_dir)]
        result = subprocess.run(cmd)
        sys.exit(result.returncode)

    # --release and --create-bundle delegate to release.py
    if args.release or args.create_bundle:
        release_py = SCRIPT_DIR / "release.py"
        if not release_py.is_file():
            die(f"release.py not found at: {release_py}")
        cmd = [sys.executable, str(release_py)]
        cmd.append("--release" if args.release else "--create-bundle")
        cmd += ["--product", args.product]
        if args.idf_path:
            cmd += ["--idf-path", args.idf_path]
        if args.release and args.out_dir:
            cmd += ["--out", args.out_dir]
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

    elif args.release_flash:
        if not serial_port:
            die("--serial-port is required for --release-flash")
        if not args.bin_path:
            die("--bin-path is required for --release-flash")
        bin_path = Path(args.bin_path).expanduser().resolve()
        if not bin_path.is_file():
            die(f"Release bin file not found: {bin_path}")
        print(f"=== Flashing release image {bin_path.name} to {serial_port} ===")
        result = subprocess.run(
            [
                "esptool.py", "--chip", "esp32",
                "-p", serial_port, "-b", str(FACTORY_BAUD_RATE),
                "write_flash", FACTORY_FLASH_OFFSET, str(bin_path),
            ],
            shell=(platform.system() == "Windows"),
        )
        if result.returncode != 0:
            sys.exit(result.returncode)
        print("=== Done (release image flashed) ===")

    elif args.console:
        if not serial_port:
            die("--serial-port is required for --console")
        print(f"=== Serial monitor ({serial_port}) ===")
        idf("monitor", port=serial_port, baud=BAUD_RATE)


if __name__ == "__main__":
    main()
