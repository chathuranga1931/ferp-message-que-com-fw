#!/usr/bin/env python3
"""
OtaBundleCreate-MainESP32.py
Create a FERP OTA bundle (.bdl) for the main ESP32 firmware target.

Usage:
    python OtaBundleCreate-MainESP32.py <firmware.bin> <version> [output_dir]

Example:
    python OtaBundleCreate-MainESP32.py ferp-com-v1.0.0.23.bin 1.0.0.23
    → ferp_main_v1.0.0.23.bdl

The bundle header embeds target name "main", which matches the OTA target
registered as OTA_TARGET_MAIN_IDX in the firmware.
"""

import sys
from pathlib import Path

# Import shared library from ferp-device-tool alongside this script's parent
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ferp-device-tool"))
from ota_bundle import build_bundle, bundle_output_name

FIRMWARE_NAME = "esp32-main"


def main() -> None:
    if len(sys.argv) < 3:
        print(f"Usage: {Path(sys.argv[0]).name} <firmware.bin> <version> [output_dir]")
        sys.exit(1)

    input_path = Path(sys.argv[1])
    version    = sys.argv[2]
    output_dir = Path(sys.argv[3]) if len(sys.argv) > 3 else input_path.parent

    if not input_path.is_file():
        print(f"ERROR: input file not found: {input_path}")
        sys.exit(1)

    binary   = input_path.read_bytes()
    bundle   = build_bundle(binary, FIRMWARE_NAME, version)
    out_name = bundle_output_name(FIRMWARE_NAME, version)
    out_path = output_dir / out_name
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bundle)

    print(f"Created : {out_path}")
    print(f"  Target  : {FIRMWARE_NAME}")
    print(f"  Version : {version}")
    print(f"  Payload : {len(binary):,} bytes")
    print(f"  Bundle  : {len(bundle):,} bytes  (84-byte header + payload)")


if __name__ == "__main__":
    main()
