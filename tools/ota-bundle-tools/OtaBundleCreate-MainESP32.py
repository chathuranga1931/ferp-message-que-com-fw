#!/usr/bin/env python3
"""
OtaBundleCreate-MainESP32.py
Create a FERP OTA bundle (.bdl) for a main application image.

Usage:
    python OtaBundleCreate-MainESP32.py <firmware.bin> <version> [output_dir] [--target NAME]

Example:
    python OtaBundleCreate-MainESP32.py ferp-com-v1.0.0.23.bin 1.0.0.23
    → ferp_esp32_main_v1.0.0.23.bdl

    python OtaBundleCreate-MainESP32.py ferp-printer-com-v44.0.0.1.bin 44.0.0.1 --target printer-com-main
    → ferp_printer_com_main_v44.0.0.1.bdl

The bundle header embeds the target name, which must match an OTA target
registered by the firmware: "esp32-main" for the ferp-com products (default),
"printer-com-main" / "printer-usb-main" for the printers.  A device rejects a
bundle whose target it does not know, so an image can only reach its product.
"""

import argparse
import sys
from pathlib import Path

# Shared library: tools/ferp-core/ferp_core/ota_bundle.py
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ferp-core"))
from ferp_core.ota_bundle import build_bundle, bundle_output_name

DEFAULT_TARGET = "esp32-main"


def main() -> None:
    ap = argparse.ArgumentParser(description="Create a FERP OTA bundle for a main application image")
    ap.add_argument("firmware", help="application .bin")
    ap.add_argument("version", help="version string, e.g. 44.0.0.1")
    ap.add_argument("output_dir", nargs="?", help="output folder (default: next to the .bin)")
    ap.add_argument("--target", default=DEFAULT_TARGET, help=f"OTA target name (default: {DEFAULT_TARGET})")
    args = ap.parse_args()

    input_path = Path(args.firmware)
    output_dir = Path(args.output_dir) if args.output_dir else input_path.parent

    if not input_path.is_file():
        print(f"ERROR: input file not found: {input_path}")
        sys.exit(1)

    binary   = input_path.read_bytes()
    bundle   = build_bundle(binary, args.target, args.version)
    out_name = bundle_output_name(args.target, args.version)
    out_path = output_dir / out_name
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bundle)

    print(f"Created : {out_path}")
    print(f"  Target  : {args.target}")
    print(f"  Version : {args.version}")
    print(f"  Payload : {len(binary):,} bytes")
    print(f"  Bundle  : {len(bundle):,} bytes  (84-byte header + payload)")


if __name__ == "__main__":
    main()
