"""
ota_bundle.py — FERP OTA Bundle (.bdl) create / decode library.

Copy of distap-esp32/tools/ota-bundle-tools/ota_bundle.py — kept in sync
manually since each board's release script imports its own local copy.

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

Firmware name strings match the OTA target names registered in the firmware
(see ferp-com-v2-main/app/app.cpp k_ota_targets[] / k_mqtt_ota_targets[]):
  "esp07-dt-boot"  → OTA_TARGET_DT_BOOT_IDX
  "esp07-dt-part"  → OTA_TARGET_DT_PART_IDX
  "esp07-dt-fw"    → OTA_TARGET_DT_FW_IDX
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

# Map from firmware name → output filename prefix. Names not listed here fall
# back to firmware_name.replace("-", "_") — see bundle_output_name().
_NAME_MAP = {
    "main": "main",
}


@dataclass
class BundleHeader:
    name:      str   # firmware target name, e.g. "esp07-dt-fw"
    version:   str   # firmware version string, e.g. "1.3.4.5"
    timestamp: int   # Unix epoch seconds


def build_bundle(binary: bytes, name: str, version: str) -> bytes:
    """
    Prepend an 84-byte bundle header to *binary* and return the combined bytes.

    Args:
        binary:  Raw firmware binary data.
        name:    Firmware target name (e.g. "esp07-dt-fw").
        version: Firmware version string (e.g. "1.3.4.5").

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
        "main"          → "ferp_main_v1.0.0.23.bdl"
        "esp07-dt-boot" → "ferp_esp07_dt_boot_v1.3.4.5.bdl"
        "esp07-dt-part" → "ferp_esp07_dt_part_v1.3.4.5.bdl"
        "esp07-dt-fw"   → "ferp_esp07_dt_fw_v1.3.4.5.bdl"
    """
    safe = _NAME_MAP.get(firmware_name, firmware_name.replace("-", "_"))
    ver  = version.lstrip("vV")
    return f"ferp_{safe}_v{ver}.bdl"
