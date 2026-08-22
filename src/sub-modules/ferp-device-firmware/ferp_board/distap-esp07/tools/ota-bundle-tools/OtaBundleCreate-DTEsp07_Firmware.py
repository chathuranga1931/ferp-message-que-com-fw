#!/usr/bin/env python3
"""
OtaBundleCreate-DTEsp07_Firmware.py
Create a FERP OTA bundle (.bdl) for the DT ESP07 application firmware target.

Usage:
    python OtaBundleCreate-DTEsp07_Firmware.py <rtos_dis_tap_esp07.bin> <version> [output_dir]

Example:
    python OtaBundleCreate-DTEsp07_Firmware.py rtos_dis_tap_esp07.bin 1.3.4.5
    → ferp_esp07_dt_fw_v1.3.4.5.bdl

The bundle header embeds target name "esp07-dt-fw", which matches the OTA
target registered as OTA_TARGET_DT_FW_IDX in the v2 (esp07) firmware and the
spiffs_path "esp07/rtos_dis_tap_esp07.bin" expected by serial_flasher.cpp.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ota_bundle import build_bundle, bundle_output_name

FIRMWARE_NAME = "esp07-dt-fw"


def main() -> None:
    if len(sys.argv) < 3:
        print(f"Usage: {Path(sys.argv[0]).name} <rtos_dis_tap_esp07.bin> <version> [output_dir]")
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
