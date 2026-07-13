#!/usr/bin/env python3
"""release.py — Release and bundle creation for ferp-com-esp32-idf

Modes:
  --release        Full release: git checks → odd build → commit+tag+push →
                   even build → factory image → tar.gz → GitHub release.
                   Output defaults to releases/main-esp32/<version>/ — pass
                   --out <dir> to override (relative paths resolve against
                   the invocation directory).
  --create-bundle  Bundle only from existing build artifacts (no build, no git).
                   Bundle version is (255-V1).V2.V3.V4 of current version.h.
                   Output goes to the build directory.
"""

import atexit
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR / "src/product/ferp-com-main/ferp-com-esp32-idf"
VERSION_FILE = SCRIPT_DIR / "src/product/ferp-com-main/app/version.h"
USER_CONFIG_FILE = SCRIPT_DIR / "src/product/ferp-com-main/app/user_config.h"
BUILD_DIR = PROJECT_DIR / "build"
BUILD_BIN = BUILD_DIR / "ferp-com.bin"
BUILD_ELF = BUILD_DIR / "ferp-com.elf"
RELEASES_DIR = SCRIPT_DIR / "releases" / "main-esp32"
BUNDLE_TOOL = SCRIPT_DIR / "tools/ota-bundle-tools/OtaBundleCreate-MainESP32.py"


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
            die(f"IDF not found at: {idf_path}")
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
            die(f"IDF not found at: {idf_path}")
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


def read_version() -> str:
    content = VERSION_FILE.read_text()
    m = re.search(r'#define FW_VERSION\s+"([^"]+)"', content)
    if not m:
        die(f"Could not parse FW_VERSION from {VERSION_FILE}")
    return m.group(1)


def set_version(version: str) -> None:
    content = VERSION_FILE.read_text()
    new_content = re.sub(
        r'(#define FW_VERSION\s+")[^"]*(")',
        rf'\g<1>{version}\2',
        content,
    )
    VERSION_FILE.write_text(new_content)


def build() -> None:
    result = subprocess.run(["idf.py", "build"], cwd=str(PROJECT_DIR),
                            shell=(platform.system() == "Windows"))
    if result.returncode != 0:
        sys.exit(result.returncode)


def copy_artifacts(version: str, dest: Path) -> None:
    shutil.copy2(str(BUILD_BIN), str(dest / f"ferp-com-v{version}.bin"))
    shutil.copy2(str(BUILD_ELF), str(dest / f"ferp-com-v{version}.elf"))
    print(f"  Saved: ferp-com-v{version}.bin")
    print(f"  Saved: ferp-com-v{version}.elf")


def create_bundle(bin_path: Path, version: str, outdir: Path) -> None:
    subprocess.run(
        [sys.executable, str(BUNDLE_TOOL), str(bin_path), version, str(outdir)],
        check=True,
    )


def git(*args: str) -> None:
    subprocess.run(["git", "-C", str(SCRIPT_DIR), *args], check=True)


def git_output(*args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(SCRIPT_DIR), *args],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def mode_create_bundle() -> None:
    print("=== Create bundle from existing build artifacts ===")

    if not BUILD_BIN.is_file():
        die(f"Build binary not found: {BUILD_BIN}\n       Run --buildall first.")

    current_ver = read_version()
    parts = current_ver.split(".")
    if len(parts) != 4:
        die(f"FW_VERSION must be in X.Y.Z.N format (got '{current_ver}')")
    v1, v2, v3, v4 = parts
    bundle_ver = f"{255 - int(v1)}.{v2}.{v3}.{v4}"

    print(f"  Current version : {current_ver}")
    print(f"  Bundle version  : {bundle_ver}  (255-{v1} downgrade marker)")
    print(f"  Output dir      : {BUILD_DIR}")
    print()

    create_bundle(BUILD_BIN, bundle_ver, BUILD_DIR)

    print()
    print("=== Bundle created in build dir ===")


def mode_release(idf_path: str, out_dir: str = "") -> None:
    releases_dir = Path(out_dir).expanduser().resolve() if out_dir else RELEASES_DIR

    # Pre-release git checks
    print("=== Pre-release checks ===")

    print("Checking git status...")
    result = subprocess.run(
        ["git", "-C", str(SCRIPT_DIR), "status", "--porcelain"],
        capture_output=True,
        text=True,
        check=True,
    )
    if result.stdout.strip():
        print()
        print("ERROR: Working tree has uncommitted changes. Commit or stash before releasing.")
        print()
        subprocess.run(["git", "-C", str(SCRIPT_DIR), "status", "--short"])
        sys.exit(1)
    print("  ✓ Working tree is clean")

    print("Checking git remote access...")
    r = subprocess.run(
        ["git", "-C", str(SCRIPT_DIR), "ls-remote", "origin", "HEAD"],
        capture_output=True,
    )
    if r.returncode != 0:
        die("Cannot reach git remote 'origin'. Check SSH key / network connection.")
    print("  ✓ Remote 'origin' is reachable")

    # Check / install GitHub CLI
    print("Checking for GitHub CLI (gh)...")
    if not shutil.which("gh"):
        print("  gh not found — installing via Homebrew...")
        if not shutil.which("brew"):
            die("Homebrew not found. Install gh manually: https://cli.github.com")
        subprocess.run(["brew", "install", "gh"], check=True)
    if not shutil.which("gh"):
        die("gh install failed. Install manually: https://cli.github.com")
    gh_ver = subprocess.run(["gh", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    print(f"  ✓ gh {gh_ver}")

    r = subprocess.run(["gh", "auth", "status"], capture_output=True)
    if r.returncode != 0:
        print()
        die("gh is not authenticated.\n       Run:  gh auth login\n       Then re-run the release.")
    print("  ✓ gh authenticated")

    # Compute release versions
    current_ver = read_version()
    print()
    print(f"Current FW_VERSION: {current_ver}")

    parts = current_ver.split(".")
    if len(parts) != 4:
        die(f"FW_VERSION must be in X.Y.Z.N format (got '{current_ver}')")
    v1, v2, v3, v4_str = parts
    v4 = int(v4_str)

    # ODD = production, EVEN = OTA-test.
    # If version.h was left at even (interrupted release), step +3 to skip the gap.
    if v4 % 2 == 0:
        odd_build = v4 + 3
    else:
        odd_build = v4 + 2
    even_build = odd_build + 1

    odd_ver = f"{v1}.{v2}.{v3}.{odd_build}"
    even_ver = f"{v1}.{v2}.{v3}.{even_build}"
    release_dir = releases_dir / odd_ver
    release_dir.mkdir(parents=True, exist_ok=True)

    print()
    print("=== Release plan ===")
    print(f"  Odd  (production): {odd_ver}")
    print(f"  Even (OTA-test):   {even_ver}")
    print(f"  Output dir:        {release_dir}")
    print()

    # Set up IDF
    setup_idf(idf_path)

    # Disable TEST_PUMP_BUILD if active; restore on exit via closure
    test_pump_was_active = False
    user_config_content = USER_CONFIG_FILE.read_text()
    if re.search(r"^#define TEST_PUMP_BUILD", user_config_content, re.MULTILINE):
        test_pump_was_active = True
        USER_CONFIG_FILE.write_text(
            re.sub(
                r"^#define TEST_PUMP_BUILD",
                "//#define TEST_PUMP_BUILD",
                user_config_content,
                flags=re.MULTILINE,
            )
        )
        print("NOTE: TEST_PUMP_BUILD disabled for release builds (restored on exit)")

    def _cleanup() -> None:
        try:
            set_version(odd_ver)
        except Exception:
            pass
        if test_pump_was_active:
            try:
                content = USER_CONFIG_FILE.read_text()
                USER_CONFIG_FILE.write_text(
                    re.sub(
                        r"^//#define TEST_PUMP_BUILD",
                        "#define TEST_PUMP_BUILD",
                        content,
                        flags=re.MULTILINE,
                    )
                )
                print("NOTE: TEST_PUMP_BUILD restored in user_config.h")
            except Exception:
                pass

    atexit.register(_cleanup)

    # Build 1: ODD version (production)
    print(f"--- Setting FW_VERSION to {odd_ver} ---")
    set_version(odd_ver)

    print(f"--- Building odd version ({odd_ver}) ---")
    build()

    print()
    print("--- Saving ODD artifacts ---")
    copy_artifacts(odd_ver, release_dir)
    create_bundle(release_dir / f"ferp-com-v{odd_ver}.bin", odd_ver, release_dir)

    # Commit, tag, push ODD
    print()
    print(f"--- Committing version.h at {odd_ver} ---")
    git("add", str(VERSION_FILE))
    git("commit", "-m", "version bumped")

    print(f"--- Tagging v{odd_ver} ---")
    git("tag", f"v{odd_ver}")
    print(f"  Tagged: v{odd_ver}")

    current_branch = git_output("rev-parse", "--abbrev-ref", "HEAD")
    print(f"--- Pushing branch '{current_branch}' and tag v{odd_ver} to origin ---")
    git("push", "origin", current_branch)
    git("push", "origin", f"v{odd_ver}")
    print("  ✓ Pushed")

    # Build 2: EVEN version (OTA-test)
    print()
    print(f"--- Setting FW_VERSION to {even_ver} ---")
    set_version(even_ver)

    print(f"--- Building even version ({even_ver}) ---")
    build()

    print()
    print("--- Saving EVEN artifacts ---")
    copy_artifacts(even_ver, release_dir)
    create_bundle(release_dir / f"ferp-com-v{even_ver}.bin", even_ver, release_dir)

    # Factory image: ODD app + shared build artifacts
    # Bootloader, partition table, ota_data, and SPIFFS come from the last build
    # (identical across both); app bin is taken from the saved ODD artifact.
    print()
    print(f"--- Creating 4MB factory image ({odd_ver}) ---")
    factory_bin = release_dir / f"ferp-esp32-factory-v{odd_ver}.bin"
    esptool_result = subprocess.run(
        [
            "esptool.py",
            "--chip", "esp32",
            "merge-bin",
            "--flash-mode", "dio",
            "--flash-freq", "40m",
            "--flash-size", "4MB",
            "--pad-to-size", "4MB",
            "-o", str(factory_bin),
            "0x1000",   str(BUILD_DIR / "bootloader/bootloader.bin"),
            "0x8000",   str(BUILD_DIR / "partition_table/partition-table.bin"),
            "0xe000",   str(BUILD_DIR / "ota_data_initial.bin"),
            "0x10000",  str(release_dir / f"ferp-com-v{odd_ver}.bin"),
            "0x370000", str(BUILD_DIR / "spiffs.bin"),
        ],
    )
    if esptool_result.returncode == 0:
        print(f"  Saved: {factory_bin}")
    else:
        die("Failed to create factory image — aborting release")

    # Package into tar.gz
    # Archive is created outside release_dir first so tar never includes itself,
    # then moved in.
    print()
    print("--- Creating release archive ---")
    tar_name = f"ferp-com-release-v{odd_ver}.tar.gz"
    tar_path = release_dir / tar_name
    temp_tar = releases_dir / tar_name
    with tarfile.open(str(temp_tar), "w:gz") as tar:
        tar.add(str(release_dir), arcname=odd_ver)
    shutil.move(str(temp_tar), str(tar_path))
    print(f"  Saved: {tar_path}")

    # GitHub release
    print()
    print(f"--- Creating GitHub release v{odd_ver} ---")

    remote_url = git_output("remote", "get-url", "origin")
    repo = remote_url.replace("git@github.com:", "")
    if repo.endswith(".git"):
        repo = repo[:-4]

    release_notes = (
        f"Production release v{odd_ver}\n\n"
        f"- **Production binary**: ferp-com-v{odd_ver}.bin\n"
        f"- **OTA-test binary**:   ferp-com-v{even_ver}.bin  "
        f"← flash odd first, then OTA to even\n"
        f"- **Factory image**:     ferp-esp32-factory-v{odd_ver}.bin  (flash to 0x0)\n"
        f"- **ELF files** included for crash backtrace decoding (addr2line)"
    )

    gh_files = [
        str(tar_path),
        str(release_dir / f"ferp-com-v{odd_ver}.bin"),
        str(release_dir / f"ferp-com-v{odd_ver}.elf"),
        str(release_dir / f"ferp-com-v{even_ver}.bin"),
        str(release_dir / f"ferp-com-v{even_ver}.elf"),
        str(factory_bin),
        str(release_dir / f"ferp_esp32_main_v{odd_ver}.bdl"),
        str(release_dir / f"ferp_esp32_main_v{even_ver}.bdl"),
    ]

    subprocess.run(
        [
            "gh", "release", "create", f"v{odd_ver}",
            "--repo", repo,
            "--title", f"v{odd_ver}",
            "--notes", release_notes,
            *gh_files,
        ],
        check=True,
    )

    print()
    print("=" * 56)
    print(f"=== Release v{odd_ver} complete ===")
    print("=" * 56)
    print()
    print(f"  Production    : ferp-com-v{odd_ver}.bin")
    print(f"  OTA-test      : ferp-com-v{even_ver}.bin")
    print(f"  ELF (debug)   : ferp-com-v{odd_ver}.elf + ferp-com-v{even_ver}.elf")
    print(f"  Factory image : ferp-esp32-factory-v{odd_ver}.bin")
    print(f"  Archive       : {tar_name}")
    print(f"  git tag       : v{odd_ver} (pushed to origin/{current_branch})")
    print(f"  GitHub release: https://github.com/{repo}/releases/tag/v{odd_ver}")
    print()
    print(f"  version.h left at: {odd_ver}")
    print()


def parse_args() -> "argparse.Namespace":
    import argparse

    parser = argparse.ArgumentParser(description="Release and bundle creation for ferp-com-esp32-idf")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--release", action="store_true", help="Full release workflow")
    group.add_argument(
        "--create-bundle",
        action="store_true",
        dest="create_bundle",
        help="Create OTA bundle from existing build artifacts",
    )
    parser.add_argument("--idf-path", default="", help="Path to ESP-IDF installation")
    parser.add_argument(
        "--out",
        dest="out_dir",
        default="",
        help="With --release, output dir override (default: releases/main-esp32)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.create_bundle:
        mode_create_bundle()
    else:
        mode_release(args.idf_path, args.out_dir)


if __name__ == "__main__":
    main()
