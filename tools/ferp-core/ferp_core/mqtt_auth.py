"""
mqtt_auth.py — FERP MQTT application-level authentication.

Shared key table and hash algorithm — MUST be kept in sync with
src/app-modules/module_mqtt/mqtt_auth.h on the firmware side.

Every outbound MQTT command envelope is extended with two fields:
    "hop_idx" : int   — index (0–15) of the key used to sign this message
    "hash"    : str   — 16-char lowercase hex of mqtt_auth_hash(keys[hop_idx], seq)

The device verifies these fields on receipt and silently discards any
command that fails authentication.
"""

import random

# ---------------------------------------------------------------------------
# Key table (64-bit unsigned integers, indices 0–15)
# Must match k_mqtt_auth_keys[] in mqtt_auth.h exactly.
# ---------------------------------------------------------------------------

MQTT_AUTH_KEY_COUNT: int = 16

_KEYS: tuple = (
    0xA3F2E1B0C4D5E6F7, 0x1B2C3D4E5F607182,
    0x9A8B7C6D5E4F3021, 0xFEDCBA9876543210,
    0x0F1E2D3C4B5A6978, 0x7E6F5D4C3B2A1908,
    0xC0FFEE1234567890, 0xDEADBEEF0BADF00D,
    0x0123456789ABCDEF, 0xFEDCBA9812345678,
    0x2468ACE013579BDF, 0x1357924680BDFACE,
    0xABCDEF0123456789, 0x9876543210FEDCBA,
    0x5A5A5A5A5A5A5A5A, 0xA5A5A5A5A5A5A5A5,
)

_MASK64 = 0xFFFF_FFFF_FFFF_FFFF

# ---------------------------------------------------------------------------
# Hash function — splitmix64-style keyed finalizer
# ---------------------------------------------------------------------------

def _hash(key: int, seq: int) -> int:
    """
    Compute a 64-bit authentication hash.

    Identical algorithm to mqtt_auth_hash() in mqtt_auth.h:
        h = key XOR (seq * 0x9e3779b97f4a7c15)
        h ^= h >> 30
        h  = h * 0xbf58476d1ce4e5b9
        h ^= h >> 27
        h  = h * 0x94d049bb133111eb
        h ^= h >> 31
    """
    h = (key ^ (seq * 0x9E3779B97F4A7C15)) & _MASK64
    h ^= h >> 30
    h  = (h * 0xBF58476D1CE4E5B9) & _MASK64
    h ^= h >> 27
    h  = (h * 0x94D049BB133111EB) & _MASK64
    h ^= h >> 31
    return h

# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def sign(seq: int) -> tuple:
    """
    Choose a random key from the table, compute the authentication hash for
    *seq*, and return ``(hop_idx, hash_hex)`` to embed in the outbound MQTT
    command envelope.

    Args:
        seq: The ``"seq"`` value that will be placed in the envelope.

    Returns:
        (hop_idx, hash_hex) — hop_idx is 0–15; hash_hex is a 16-char
        lowercase hex string.
    """
    hop_idx = random.randint(0, MQTT_AUTH_KEY_COUNT - 1)
    h = _hash(_KEYS[hop_idx], seq & 0xFFFF_FFFF)
    return hop_idx, f"{h:016x}"
