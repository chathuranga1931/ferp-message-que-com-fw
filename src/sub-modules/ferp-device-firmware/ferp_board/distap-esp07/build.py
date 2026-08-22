#!/usr/bin/env python3
"""build.py — Build and flash helper for distap-esp07

Usage:
    python3 build.py [--cleanall | --flashall | --flashapp | --console | --release]
                      [--out OUTPUT_DIR] [--serial-port PORT] [--idf-path PATH]

Actions:
  --cleanall  Full clean, build everything, flash everything
  --flashall  Build without clean, flash everything
  --flashapp  Build without clean, flash app partition only
  --console   Open serial monitor (idf.py monitor)
  --release   Build a release from the CURRENT CONFIG_APP_PROJECT_VER in
              sdkconfig.defaults — no odd/even version pair, just the one
              version as configured. Copies firmware + bootloader +
              partition-table binaries into releases/<version>/ and creates
              OTA bundles for all three. If a git tag "dt-esp07-v<version>"
              already exists (this version has been released before), warns
              and asks for y/n confirmation before continuing WITHOUT
              creating a duplicate tag; otherwise tags the release locally
              after a successful build (not pushed — push manually).

  --out <dir> Only meaningful with --release: write the releases/<version>/
              output tree into <dir> instead of the default releases/ next to
              this script. Relative paths are resolved against the directory
              this script was invoked from.

Typically called via build.sh, which supplies the default --serial-port and
--idf-path for this board.

NOTE — separate toolchain required:
  This is an ESP8266 (ESP07 module) project built against ESP8266_RTOS_SDK,
  NOT mainline ESP-IDF used by the esp32 boards in this repo. --idf-path here
  must point at an ESP8266_RTOS_SDK checkout (github.com/espressif/ESP8266_RTOS_SDK),
  which provides its own idf.py/export.sh and manages its own xtensa-lx106-elf
  toolchain (separate from the xtensa-esp32-elf toolchain used elsewhere).
  See build.sh for the one-time setup steps.
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
# distap-esp07 lives at:
#   ferp-message-library/src/sub-modules/ferp-device-firmware/ferp_board/distap-esp07
# — part of the same monorepo/git repo (no separate submodule), 5 levels up.
REPO_ROOT = SCRIPT_DIR.parents[4]
DEFAULT_COMPORT = "/dev/tty.usbserial-A5069RR4"
DEFAULT_IDF_PATH = "/Users/chathuranga/DATA/esp/ESP8266_RTOS_SDK"
MONITOR_BAUD = 115200


def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def ensure_python_shim() -> str:
    """
    ESP8266_RTOS_SDK's export.sh and tools/idf_tools.py invoke a bare `python`
    (not `python3`) internally. Modern macOS (and many Linux distros) ship
    only `python3`. Create a one-time `python -> python3` symlink in a
    dedicated directory and return that directory, to be prepended to PATH
    for the SDK subprocess only — this never touches the user's real PATH,
    shell profile, or system Python.
    """
    if shutil.which("python"):
        return ""

    python3_path = shutil.which("python3")
    if not python3_path:
        die("Neither 'python' nor 'python3' found in PATH.")

    shim_dir = Path.home() / ".espressif" / "python-shim"
    shim_dir.mkdir(parents=True, exist_ok=True)
    shim = shim_dir / "python"
    if not shim.exists():
        shim.symlink_to(python3_path)
    return str(shim_dir)


def patch_apple_silicon_platform_map(idf_path: str) -> None:
    """
    This SDK release's tools/idf_tools.py has no 'Darwin-arm64' entry in its
    platform map, so it refuses to run on Apple Silicon even though the
    listed macOS xtensa-lx106-elf toolchain build (x86_64, runs fine under
    Rosetta 2) would otherwise work. Add the missing mapping once, in place,
    if not already present. No-op on non-Darwin/non-arm64 hosts.
    """
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        return

    idf_tools_py = Path(idf_path) / "tools" / "idf_tools.py"
    if not idf_tools_py.is_file():
        return

    text = idf_tools_py.read_text()
    if "Darwin-arm64" in text:
        return

    patched = text.replace(
        "    'Darwin-x86_64': PLATFORM_MACOS,\n",
        "    'Darwin-x86_64': PLATFORM_MACOS,\n"
        "    'Darwin-arm64': PLATFORM_MACOS,  # x86_64 toolchain runs fine under Rosetta 2\n",
        1,
    )
    if patched != text:
        idf_tools_py.write_text(patched)
        print("Patched idf_tools.py: added Darwin-arm64 -> macOS platform mapping")


def find_windows_venv_scripts_dir(idf_path: str) -> str:
    """
    This SDK release's export.sh calls bare `python` (via
    check_python_dependencies.py, and internally within idf_tools.py) without
    ever adding its own dedicated venv's Scripts dir to PATH — unlike mainline
    ESP-IDF's newer export scripts. On a dev machine with some *other* Python
    already resolving on PATH (PlatformIO's env, a pyenv shim, etc.), that
    means export.sh silently checks/uses the WRONG interpreter — one that
    was never `pip install -r requirements.txt`'d for this SDK at all.

    The venv itself (created by install.sh) is real and correctly populated;
    it just needs to be put in front of PATH ourselves. Its location follows
    idf_tools.py's own get_python_env_path() naming:
        <IDF_TOOLS_PATH>/python_env/rtos<sdk_ver>_py<major.minor>_env/Scripts
    where <IDF_TOOLS_PATH> defaults to ~/.espressif but is commonly
    overridden globally (e.g. the official ESP-IDF Windows installer sets it
    to C:\\Espressif) — so read the env var rather than assuming the default.
    Rather than re-deriving the exact venv name (which depends on whichever
    Python happened to run install.sh), just glob for any "rtos*_env" venv
    under python_env — mainline ESP-IDF's own venvs are named "idf<ver>_..."
    so this can't accidentally pick one of those instead.
    """
    tools_path = os.environ.get("IDF_TOOLS_PATH") or os.path.expanduser(os.path.join("~", ".espressif"))
    matches = sorted(Path(tools_path).glob("python_env/rtos*_env/Scripts"))
    return str(matches[-1]) if matches else ""


def setup_idf(idf_path: str) -> None:
    if shutil.which("idf.py"):
        return

    if not idf_path:
        die("idf.py not found in PATH. Provide --idf-path to locate the ESP8266_RTOS_SDK installation.")

    export_sh = os.path.join(idf_path, "export.sh")
    if not os.path.isfile(export_sh):
        die(f"ESP8266_RTOS_SDK not found at: {idf_path}\n       (export.sh missing)")

    patch_apple_silicon_platform_map(idf_path)
    shim_dir = ensure_python_shim()
    env = os.environ.copy()
    if shim_dir:
        env["PATH"] = shim_dir + os.pathsep + env.get("PATH", "")

    if platform.system() == "Windows":
        venv_scripts_dir = find_windows_venv_scripts_dir(idf_path)
        if venv_scripts_dir:
            env["PATH"] = venv_scripts_dir + os.pathsep + env.get("PATH", "")

        # This SDK's tools/idf.py re-execs itself under `winpty` whenever
        # MSYSTEM is set (MSYS2/Git Bash), for proper Ctrl+C handling — but
        # winpty needs a real attached console and refuses (prints just
        # "stdin is not a tty" and exits 1) when run from anything that
        # isn't one: piped/redirected output, a script runner, some IDE
        # terminals. idf.py itself already honors WINPTY as an explicit
        # "skip the winpty re-exec" escape hatch — set it globally here
        # (not just for the export.sh subprocess) since it also has to
        # cover every later `idf.py` invocation from idf() below.
        os.environ["WINPTY"] = "1"

    print("Setting up ESP8266_RTOS_SDK environment...")
    cmd = (
        f'source "{export_sh}" > /dev/null 2>&1 && '
        'python3 -c "import os, json; print(json.dumps(dict(os.environ)))"'
    )
    result = subprocess.run(["bash", "-c", cmd], capture_output=True, text=True, env=env)
    if result.returncode != 0 or not result.stdout.strip():
        die("Failed to set up ESP8266_RTOS_SDK environment. Check --idf-path.\n"
            f"{result.stderr}")
    os.environ.update(json.loads(result.stdout.strip()))

    if not shutil.which("idf.py"):
        die("idf.py not found after sourcing the SDK environment. Check --idf-path.")


def idf(*args: str, port: str = "", baud: int = 0) -> None:
    # -D PROJECT_VER=... : this SDK's CMake build (unlike its legacy Make
    # build) does NOT honor CONFIG_APP_PROJECT_VER_FROM_CONFIG/
    # CONFIG_APP_PROJECT_VER at all — see project.cmake's __project_get_revision(),
    # which only checks version.txt then falls back to `git describe` of
    # whatever repo happens to contain this directory (the outer monorepo,
    # since distap-esp07 has no .git of its own), producing a garbage version
    # string embedded in esp_app_desc_t. Passing PROJECT_VER explicitly here
    # short-circuits that fallback chain (project.cmake only computes its own
    # value `if(NOT DEFINED PROJECT_VER)`), keeping sdkconfig.defaults as the
    # single source of truth with no extra version file needed.
    cmd = ["idf.py", "-D", f"PROJECT_VER={read_version(PROJECT_DIR / 'sdkconfig.defaults')}"]
    if port:
        cmd += ["-p", port]
    if baud:
        cmd += ["-b", str(baud)]
    cmd += list(args)
    # idf.py is a Python script, not a native executable — CreateProcess()
    # can't launch it directly on Windows (WinError 193), only cmd.exe's
    # associated-file-type handling can, so route through the shell there.
    # Matches the same shell=... pattern already used by the root build.py's
    # idf() for the esp32 products.
    result = subprocess.run(cmd, cwd=str(PROJECT_DIR), shell=(platform.system() == "Windows"))
    if result.returncode != 0:
        sys.exit(result.returncode)


def read_version(defaults_file: Path) -> str:
    text = defaults_file.read_text()
    m = re.search(r'^CONFIG_APP_PROJECT_VER="([^"]*)"', text, re.MULTILINE)
    if not m:
        die(f"Could not parse CONFIG_APP_PROJECT_VER from {defaults_file}")
    return m.group(1)


def git_tag_exists(repo_root: Path, tag: str) -> bool:
    result = subprocess.run(
        ["git", "tag", "-l", tag], cwd=str(repo_root), capture_output=True, text=True
    )
    return bool(result.stdout.strip())


def git_create_tag(repo_root: Path, tag: str) -> None:
    subprocess.run(["git", "tag", tag], cwd=str(repo_root), check=True)


def copy_and_bundle(ver: str, build_fw: Path, build_boot: Path, build_part: Path,
                     release_dir: Path, ota_tools: Path) -> None:
    ver_dir = release_dir / f"ferp_dt_esp07_v{ver}"
    bin_dir = ver_dir / "bin"
    bdl_dir = ver_dir / "bundle"
    bin_dir.mkdir(parents=True, exist_ok=True)
    bdl_dir.mkdir(parents=True, exist_ok=True)

    fw_bin = bin_dir / f"rtos_dis_tap_esp07_v{ver}.bin"
    boot_bin = bin_dir / f"bootloader_v{ver}.bin"
    part_bin = bin_dir / f"partitions_table_v{ver}.bin"

    print(f"--- Copying binaries for {ver} ---")
    shutil.copyfile(build_fw, fw_bin)
    shutil.copyfile(build_boot, boot_bin)
    shutil.copyfile(build_part, part_bin)
    print(f"  Saved: {fw_bin}")
    print(f"  Saved: {boot_bin}")
    print(f"  Saved: {part_bin}")

    print(f"--- Creating OTA bundles for {ver} ---")
    for tool, bin_path in (
        ("OtaBundleCreate-DTEsp07_Firmware.py", fw_bin),
        ("OtaBundleCreate-DTEsp07_Bootloader.py", boot_bin),
        ("OtaBundleCreate-DTEsp07_Partitions.py", part_bin),
    ):
        result = subprocess.run(
            [sys.executable, str(ota_tools / tool), str(bin_path), ver, str(bdl_dir)]
        )
        if result.returncode != 0:
            sys.exit(result.returncode)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build and flash helper for distap-esp07",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--cleanall", action="store_true", help="Full clean, build all, flash all")
    group.add_argument("--flashall", action="store_true", help="Build (no clean), flash all")
    group.add_argument("--flashapp", action="store_true", help="Build (no clean) + flash app partition only")
    group.add_argument("--console", action="store_true", help="Open serial monitor")
    group.add_argument("--release", action="store_true", help="Build a release from the current CONFIG_APP_PROJECT_VER")
    parser.add_argument("--out", dest="out_dir", help="With --release, write output tree here instead of releases/")
    parser.add_argument("--serial-port", default=DEFAULT_COMPORT, help="Serial port")
    parser.add_argument("--idf-path", default=DEFAULT_IDF_PATH, help="Path to ESP8266_RTOS_SDK installation")
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

    print(f"Project: {PROJECT_DIR}")
    print(f"Port:    {args.serial_port}")
    print()

    if args.cleanall:
        print("=== Clean all ===")
        idf("fullclean")
        print("=== Build all ===")
        idf("build")
        print("=== Flash all ===")
        idf("flash", port=args.serial_port)
        print("=== Done (clean + flash all) ===")

    elif args.flashall:
        print("=== Build all ===")
        idf("build")
        print("=== Flash all ===")
        idf("flash", port=args.serial_port)
        print("=== Done (flash all) ===")

    elif args.flashapp:
        print("=== Build app ===")
        idf("build")
        print("=== Flash app partition ===")
        idf("app-flash", port=args.serial_port)
        print("=== Done (flash app) ===")

    elif args.console:
        print(f"=== Serial monitor ({args.serial_port}) ===")
        idf("monitor", port=args.serial_port, baud=MONITOR_BAUD)

    elif args.release:
        sdkconfig_defaults = PROJECT_DIR / "sdkconfig.defaults"
        build_dir = PROJECT_DIR / "build"
        # Verified against an actual `idf.py build` output tree: the app binary
        # is flat under build/, but the partition table binary lands under
        # build/partition_table/ — same convention as the esp32 boards. (The
        # project's own README documents the *legacy Make* build's output
        # layout, which differs from idf.py/CMake's.)
        build_fw = build_dir / "rtos_dis_tap_esp07.bin"
        build_boot = build_dir / "bootloader" / "bootloader.bin"
        build_part = build_dir / "partition_table" / "partition-table.bin"
        releases_dir = out_dir if out_dir else PROJECT_DIR / "releases"
        ota_tools = PROJECT_DIR / "tools" / "ota-bundle-tools"

        version = read_version(sdkconfig_defaults)
        print(f"Release version (CONFIG_APP_PROJECT_VER): {version}")

        tag = f"dt-esp07-v{version}"
        create_tag = True
        if git_tag_exists(REPO_ROOT, tag):
            print(f"WARNING: tag '{tag}' already exists — this version has already been released.")
            answer = input("Continue building WITHOUT creating a new tag? [y/N]: ").strip().lower()
            if answer != "y":
                die("Aborted. Bump CONFIG_APP_PROJECT_VER in sdkconfig.defaults before releasing again.")
            create_tag = False

        release_dir = releases_dir / version
        release_dir.mkdir(parents=True, exist_ok=True)

        print()
        print("=== Release plan ===")
        print(f"  Version:    {version}")
        print(f"  Output dir: {release_dir}")
        print()

        print(f"--- Building {version} ---")
        shutil.rmtree(build_dir, ignore_errors=True)
        result = subprocess.run(["idf.py", "-D", f"PROJECT_VER={version}", "build"],
                                cwd=str(PROJECT_DIR), shell=(platform.system() == "Windows"))
        if result.returncode != 0:
            die(f"Build failed for version {version}")
        copy_and_bundle(version, build_fw, build_boot, build_part, release_dir, ota_tools)

        if create_tag:
            git_create_tag(REPO_ROOT, tag)
            print(f"Tagged: {tag} (local only — push manually: git push origin {tag})")

        print()
        print(f"=== Release complete: {version} ===")
        print(f"  Output: {release_dir}/")
        # Plain ASCII arrows, not "->" the box/pretty kind — Windows' console
        # defaults to a legacy codepage (e.g. cp1252) that can't encode most
        # Unicode arrow glyphs, and print() crashes with UnicodeEncodeError
        # rather than falling back.
        print(f"    ferp_dt_esp07_v{version}/bin/    -> firmware, bootloader, partition-table .bin")
        print(f"    ferp_dt_esp07_v{version}/bundle/ -> OTA .bdl bundles")


if __name__ == "__main__":
    main()
