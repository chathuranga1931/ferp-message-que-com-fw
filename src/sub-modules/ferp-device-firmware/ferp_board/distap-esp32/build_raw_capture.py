#!/usr/bin/env python3
"""build_raw_capture.py — Build and flash the raw-capture-only test firmware
(display types 90+, version 99.x.y.z — see PLAN.md). Uses a separate build
directory (build_raw_capture/) and sdkconfig
(sdkconfig.raw_capture / sdkconfig.raw_capture.defaults) so it never touches
the normal production build/binary produced by build.py.

Usage:
    python3 build_raw_capture.py [--cleanall | --flashall | --flashapp | --console | --release]
                                  [--out OUTPUT_DIR] [--serial-port PORT] [--idf-path PATH]

Actions:
  --cleanall  Full clean, build everything, flash everything
  --flashall  Build without clean, flash everything
  --flashapp  Build without clean, flash app partition only
  --console   Open serial monitor (idf.py monitor)
  --release   Build paired release binaries (even=OTA-test, odd=production)
              of the raw-capture firmware, mirroring build.py --release.
              Reads CONFIG_APP_PROJECT_VER from sdkconfig.raw_capture.defaults
              (always major.minor.patch = 99.x.y — see PLAN.md §2 — only the
              trailing build number is bumped), computes the next odd build
              number (production) and even = odd+1 (OTA-test), does a clean
              rebuild for each, copies firmware + bootloader + partition-table
              binaries into releases_raw_capture/<odd-version>/, creates OTA
              bundles for all three binary types for both versions, then
              leaves sdkconfig.raw_capture.defaults at the odd (production)
              version. Kept in a separate releases_raw_capture/ directory
              (not releases/) so raw-capture test bundles are never mixed up
              with production ones.

  --out <dir> Only meaningful with --release: write the
              releases_raw_capture/<odd-version>/ output tree into <dir>
              instead of the default releases_raw_capture/ next to this
              script. Relative paths are resolved against the directory this
              script was invoked from.

Typically called via build_raw_capture.sh, which supplies the default
--serial-port and --idf-path for this board.
"""

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR
BUILD_DIR = PROJECT_DIR / "build_raw_capture"
SDKCONFIG_FILE = PROJECT_DIR / "sdkconfig.raw_capture"
SDKCONFIG_DEFAULTS_ARG = "sdkconfig.defaults;sdkconfig.raw_capture.defaults"
DEFAULT_COMPORT = "/dev/tty.usbserial-A5069RR4"
DEFAULT_IDF_PATH = "/Users/chathuranga/DATA/esp/v5.5.3/esp-idf"
MONITOR_BAUD = 115200


def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def setup_idf(idf_path: str) -> None:
    if shutil.which("idf.py"):
        return

    if not idf_path:
        die("idf.py not found in PATH. Provide --idf-path to locate the ESP-IDF installation.")

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

    if not shutil.which("idf.py"):
        die("idf.py not found after sourcing IDF environment. Check --idf-path.")


def idf_args() -> list:
    return ["-B", str(BUILD_DIR), "-D", f"SDKCONFIG={SDKCONFIG_FILE}",
            "-D", f"SDKCONFIG_DEFAULTS={SDKCONFIG_DEFAULTS_ARG}"]


def idf(*args: str, port: str = "", baud: int = 0) -> None:
    cmd = ["idf.py"] + idf_args()
    if port:
        cmd += ["-p", port]
    if baud:
        cmd += ["-b", str(baud)]
    cmd += list(args)
    result = subprocess.run(cmd, cwd=str(PROJECT_DIR))
    if result.returncode != 0:
        sys.exit(result.returncode)


def read_version(defaults_file: Path) -> str:
    text = defaults_file.read_text()
    m = re.search(r'^CONFIG_APP_PROJECT_VER="([^"]*)"', text, re.MULTILINE)
    if not m:
        die(f"Could not parse CONFIG_APP_PROJECT_VER from {defaults_file}")
    return m.group(1)


def set_version(defaults_file: Path, sdkconfig_file: Path, ver: str) -> None:
    for f in (defaults_file, sdkconfig_file):
        if f.is_file():
            text = f.read_text()
            text = re.sub(
                r'^CONFIG_APP_PROJECT_VER=.*$',
                f'CONFIG_APP_PROJECT_VER="{ver}"',
                text,
                flags=re.MULTILINE,
            )
            f.write_text(text)


def copy_and_bundle(ver: str, build_fw: Path, build_boot: Path, build_part: Path,
                     release_dir: Path, ota_tools: Path) -> None:
    ver_dir = release_dir / f"ferp_dt_esp32_raw_v{ver}"
    bin_dir = ver_dir / "bin"
    bdl_dir = ver_dir / "bundle"
    bin_dir.mkdir(parents=True, exist_ok=True)
    bdl_dir.mkdir(parents=True, exist_ok=True)

    fw_bin = bin_dir / f"distap_esp32_raw_v{ver}.bin"
    boot_bin = bin_dir / f"bootloader_v{ver}.bin"
    part_bin = bin_dir / f"partition_table_v{ver}.bin"

    print(f"--- Copying binaries for {ver} ---")
    shutil.copyfile(build_fw, fw_bin)
    shutil.copyfile(build_boot, boot_bin)
    shutil.copyfile(build_part, part_bin)
    print(f"  Saved: {fw_bin}")
    print(f"  Saved: {boot_bin}")
    print(f"  Saved: {part_bin}")

    print(f"--- Creating OTA bundles for {ver} ---")
    for tool, bin_path in (
        ("OtaBundleCreate-DTEsp32_Firmware.py", fw_bin),
        ("OtaBundleCreate-DTEsp32_Bootloader.py", boot_bin),
        ("OtaBundleCreate-DTEsp32_Partitions.py", part_bin),
    ):
        result = subprocess.run(
            [sys.executable, str(ota_tools / tool), str(bin_path), ver, str(bdl_dir)]
        )
        if result.returncode != 0:
            sys.exit(result.returncode)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build and flash the raw-capture-only test firmware for distap-esp32",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--cleanall", action="store_true", help="Full clean, build all, flash all")
    group.add_argument("--flashall", action="store_true", help="Build (no clean), flash all")
    group.add_argument("--flashapp", action="store_true", help="Build (no clean) + flash app partition only")
    group.add_argument("--console", action="store_true", help="Open serial monitor")
    group.add_argument("--release", action="store_true", help="Build paired release: even (OTA-test) + odd (production)")
    parser.add_argument("--out", dest="out_dir", help="With --release, write output tree here instead of releases_raw_capture/")
    parser.add_argument("--serial-port", default=DEFAULT_COMPORT, help="Serial port")
    parser.add_argument("--idf-path", default=DEFAULT_IDF_PATH, help="Path to ESP-IDF installation")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    invoke_dir = Path.cwd()

    out_dir = None
    if args.out_dir:
        out_dir = Path(args.out_dir)
        if not out_dir.is_absolute():
            out_dir = invoke_dir / out_dir

    setup_idf(args.idf_path)

    if not PROJECT_DIR.is_dir():
        die(f"Project directory not found: {PROJECT_DIR}")

    print(f"Project:    {PROJECT_DIR}")
    print(f"Build dir:  {BUILD_DIR}")
    print(f"Port:       {args.serial_port}")
    print()

    if args.cleanall:
        print("=== Clean all (raw-capture build) ===")
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
        if SDKCONFIG_FILE.is_file():
            SDKCONFIG_FILE.unlink()

        print("=== Build all (raw-capture build) ===")
        idf("build")
        print("=== Flash all ===")
        idf("flash", port=args.serial_port)
        print("=== Done (clean + flash all, raw-capture build) ===")

    elif args.flashall:
        print("=== Build all (raw-capture build) ===")
        idf("build")
        print("=== Flash all ===")
        idf("flash", port=args.serial_port)
        print("=== Done (flash all, raw-capture build) ===")

    elif args.flashapp:
        print("=== Build app (raw-capture build) ===")
        idf("build")
        print("=== Flash app partition ===")
        idf("app-flash", port=args.serial_port)
        print("=== Done (flash app, raw-capture build) ===")

    elif args.console:
        print(f"=== Serial monitor ({args.serial_port}) ===")
        idf("monitor", port=args.serial_port, baud=MONITOR_BAUD)

    elif args.release:
        ver_defaults = PROJECT_DIR / "sdkconfig.raw_capture.defaults"
        build_fw = BUILD_DIR / "distap_esp32.bin"
        build_boot = BUILD_DIR / "bootloader" / "bootloader.bin"
        build_part = BUILD_DIR / "partition_table" / "partition-table.bin"
        releases_dir = out_dir if out_dir else PROJECT_DIR / "releases_raw_capture"
        ota_tools = PROJECT_DIR / "tools" / "ota-bundle-tools"

        current_ver = read_version(ver_defaults)
        print(f"Current CONFIG_APP_PROJECT_VER: {current_ver}")

        parts = current_ver.split(".")
        if len(parts) != 4:
            die(f"CONFIG_APP_PROJECT_VER must be in X.Y.Z.N format (got '{current_ver}')")
        v1, v2, v3, v4 = parts[0], parts[1], parts[2], int(parts[3])

        if v1 != "99":
            print(f"WARNING: major version is '{v1}', not '99' — see PLAN.md §2 on why")
            print("         raw-capture builds are expected to stay on major version 99.")

        # Compute next odd build number (production) and even = odd+1 (OTA-test)
        odd_v4 = v4 + 1 if v4 % 2 == 0 else v4 + 2
        even_v4 = odd_v4 + 1

        even_ver = f"{v1}.{v2}.{v3}.{even_v4}"
        odd_ver = f"{v1}.{v2}.{v3}.{odd_v4}"

        release_dir = releases_dir / odd_ver
        release_dir.mkdir(parents=True, exist_ok=True)

        print()
        print("=== Release plan (raw-capture build) ===")
        print(f"  Even (OTA-test):   {even_ver}")
        print(f"  Odd  (production): {odd_ver}")
        print(f"  Output dir:        {release_dir}")
        print()

        # ── Build 1: even version (OTA-test) ─────────────────────────────
        print(f"--- Setting CONFIG_APP_PROJECT_VER to {even_ver} ---")
        set_version(ver_defaults, SDKCONFIG_FILE, even_ver)

        print(f"--- Building even version ({even_ver}) ---")
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
        result = subprocess.run(["idf.py"] + idf_args() + ["build"], cwd=str(PROJECT_DIR))
        if result.returncode != 0:
            print(f"ERROR: Build failed for even version {even_ver}")
            set_version(ver_defaults, SDKCONFIG_FILE, current_ver)
            sys.exit(1)
        copy_and_bundle(even_ver, build_fw, build_boot, build_part, release_dir, ota_tools)

        # ── Build 2: odd version (production) ────────────────────────────
        print()
        print(f"--- Setting CONFIG_APP_PROJECT_VER to {odd_ver} ---")
        set_version(ver_defaults, SDKCONFIG_FILE, odd_ver)

        print(f"--- Building odd version ({odd_ver}) ---")
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
        result = subprocess.run(["idf.py"] + idf_args() + ["build"], cwd=str(PROJECT_DIR))
        if result.returncode != 0:
            print(f"ERROR: Build failed for odd version {odd_ver}")
            sys.exit(1)
        copy_and_bundle(odd_ver, build_fw, build_boot, build_part, release_dir, ota_tools)

        print()
        print(f"=== Release complete: {odd_ver} ===")
        print(f"  Output: {release_dir}/")
        print(f"    ferp_dt_esp32_raw_v{even_ver}/bin/    ← firmware, bootloader, partition-table .bin")
        print(f"    ferp_dt_esp32_raw_v{even_ver}/bundle/ ← OTA .bdl bundles (flash to test OTA upgrade)")
        print(f"    ferp_dt_esp32_raw_v{odd_ver}/bin/     ← firmware, bootloader, partition-table .bin")
        print(f"    ferp_dt_esp32_raw_v{odd_ver}/bundle/  ← OTA .bdl bundles (production)")
        print()
        print(f"  sdkconfig.raw_capture.defaults left at: {odd_ver}")


if __name__ == "__main__":
    main()
