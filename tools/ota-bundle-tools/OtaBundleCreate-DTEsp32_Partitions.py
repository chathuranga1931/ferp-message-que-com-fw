#!/usr/bin/env python3
"""
OtaBundleCreate-DTEsp32_Partitions.py
Create a FERP OTA bundle (.bdl) for the DT ESP32 partition table target.

Usage:
    python OtaBundleCreate-DTEsp32_Partitions.py <partition_table.bin> <version> [output_dir]

Example:
    python OtaBundleCreate-DTEsp32_Partitions.py partition_table.bin 1.0.0
    → ferp_dt_esp32_partitions_v1.0.0.bdl

The bundle header embeds target name "dt-partitions", which matches the OTA
target registered as OTA_TARGET_DT_PART_IDX in the firmware.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ferp-device-tool"))
from ota_bundle import build_bundle, bundle_output_name

FIRMWARE_NAME = "dt-partitions"


def main() -> None:
    if len(sys.argv) < 3:
        print(f"Usage: {Path(sys.argv[0]).name} <partition_table.bin> <version> [output_dir]")
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
