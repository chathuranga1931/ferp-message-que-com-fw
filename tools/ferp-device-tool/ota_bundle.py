"""
ota_bundle.py — FERP OTA Bundle (.bdl) create / decode library.

Bundle header (84 bytes, little-endian):

  Offset  Size  Field
  ──────  ────  ─────────────────────────────────────────────────────────────
  0       4     Magic: b'FERP'
  4       2     Format version: 1  (uint16 LE)
  6       2     Reserved (0x0000)
  8       32    Firmware name      (null-padded, max 31 chars)
  40      32    Firmware version   (null-padded, max 31 chars)
  72      8     Timestamp          (uint64 LE, Unix epoch seconds)
  80      4     Header CRC32       (CRC of bytes 0..79, uint32 LE)
  ──────  ────  Total: 84 bytes

Immediately followed by the raw firmware binary payload.

Bundle file naming convention:
  ferp_main_v{version}.bdl
  ferp_dt_esp32_bootloader_v{version}.bdl
  ferp_dt_esp32_partitions_v{version}.bdl
  ferp_dt_esp32_firmware_v{version}.bdl

Firmware name strings match the OTA target names registered in the firmware:
  "main"           → OTA_TARGET_MAIN_IDX
  "dt-bootloader"  → OTA_TARGET_DT_BOOT_IDX
  "dt-partitions"  → OTA_TARGET_DT_PART_IDX
  "dt-app"         → OTA_TARGET_DT_FW_IDX
"""

import struct
import time
import zlib
from dataclasses import dataclass
from typing import Tuple

MAGIC           = b'FERP'
FORMAT_VERSION  = 1
HEADER_SIZE     = 84      # bytes

# Struct for the first 80 bytes (preceding the CRC field):
#   4s  magic
#   H   format version (uint16)
#   H   reserved
#   32s name field
#   32s version field
#   Q   timestamp (uint64)
_HDR_STRUCT = struct.Struct('<4sHH32s32sQ')
assert _HDR_STRUCT.size == 80

# Map from firmware name → output filename prefix
_NAME_MAP = {
    "main":          "main",
    "dt-bootloader": "dt_esp32_bootloader",
    "dt-partitions": "dt_esp32_partitions",
    "dt-app":        "dt_esp32_firmware",
}


@dataclass
class BundleHeader:
    name:      str   # firmware target name, e.g. "main", "dt-bootloader"
    version:   str   # firmware version string, e.g. "1.0.0.23"
    timestamp: int   # Unix epoch seconds


def build_bundle(binary: bytes, name: str, version: str) -> bytes:
    """
    Prepend an 84-byte bundle header to *binary* and return the combined bytes.

    Args:
        binary:  Raw firmware binary data.
        name:    Firmware target name (e.g. "main", "dt-bootloader").
        version: Firmware version string (e.g. "1.0.0.23").

    Returns:
        header (84 bytes) + binary
    """
    ts        = int(time.time())
    name_b    = name.encode()[:31].ljust(32, b'\x00')
    version_b = version.encode()[:31].ljust(32, b'\x00')
    hdr_body  = _HDR_STRUCT.pack(MAGIC, FORMAT_VERSION, 0, name_b, version_b, ts)
    crc       = zlib.crc32(hdr_body) & 0xFFFFFFFF
    header    = hdr_body + struct.pack('<I', crc)
    assert len(header) == HEADER_SIZE
    return header + binary


def decode_bundle(data: bytes) -> Tuple[BundleHeader, bytes]:
    """
    Parse a .bdl bundle file.

    Args:
        data:  Full contents of a .bdl file (header + firmware payload).

    Returns:
        (BundleHeader, raw_firmware_bytes)

    Raises:
        ValueError: on magic mismatch, unsupported version, or CRC error.
    """
    if len(data) < HEADER_SIZE:
        raise ValueError(
            f"File too short: {len(data)} bytes (minimum {HEADER_SIZE})"
        )

    hdr_body   = data[:80]
    crc_stored = struct.unpack_from('<I', data, 80)[0]
    crc_calc   = zlib.crc32(hdr_body) & 0xFFFFFFFF

    if crc_stored != crc_calc:
        raise ValueError(
            f"Header CRC mismatch: stored=0x{crc_stored:08X}  "
            f"calculated=0x{crc_calc:08X}"
        )

    magic, fmt_ver, _rsvd, name_b, ver_b, ts = _HDR_STRUCT.unpack(hdr_body)

    if magic != MAGIC:
        raise ValueError(f"Not a FERP bundle — bad magic: {magic!r}")
    if fmt_ver != FORMAT_VERSION:
        raise ValueError(f"Unsupported bundle format version: {fmt_ver}")

    name    = name_b.rstrip(b'\x00').decode(errors='replace')
    version = ver_b.rstrip(b'\x00').decode(errors='replace')

    return BundleHeader(name=name, version=version, timestamp=ts), data[HEADER_SIZE:]


def bundle_output_name(firmware_name: str, version: str) -> str:
    """
    Return the conventional output filename for a bundle.

    Examples:
        "main"           → "ferp_main_v1.0.0.23.bdl"
        "dt-bootloader"  → "ferp_dt_esp32_bootloader_v1.0.0.bdl"
        "dt-partitions"  → "ferp_dt_esp32_partitions_v1.0.0.bdl"
        "dt-app"         → "ferp_dt_esp32_firmware_v1.0.0.bdl"
    """
    safe = _NAME_MAP.get(firmware_name, firmware_name.replace("-", "_"))
    ver  = version.lstrip("vV")
    return f"ferp_{safe}_v{ver}.bdl"
