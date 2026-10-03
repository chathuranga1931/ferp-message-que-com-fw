"""
config_keys.py — Device configuration key definitions.

Keys are defined in data/config_keys.json (bundled with this package) so new
keys need no code change. Callers may pass another file to override.

Optional per-key limits used for validation in the UIs:
    "min" / "max"   numeric range for UINT32 keys
    "max_len"       maximum length for STRING keys
"""

import json
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

from .transports import type_id_from_name

DEFAULT_CONFIG_KEYS_FILE = Path(__file__).parent / "data" / "config_keys.json"


@dataclass(frozen=True)
class ConfigKey:
    key_id:      int
    name:        str
    type_id:     int
    group:       str
    label:       str = ""
    description: str = ""
    min:         Optional[int] = None
    max:         Optional[int] = None
    max_len:     Optional[int] = None


def load_config_keys(path: Optional[Path] = None) -> List[ConfigKey]:
    path = Path(path) if path else DEFAULT_CONFIG_KEYS_FILE
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return []
    result = []
    for grp in data.get("groups", []):
        grp_label = grp.get("group", "Other")
        for k in grp.get("keys", []):
            try:
                result.append(ConfigKey(
                    key_id      = int(k["key"], 16),
                    name        = k["name"],
                    type_id     = type_id_from_name(k.get("type", "STRING")),
                    group       = grp_label,
                    label       = k.get("label", ""),
                    description = k.get("description", ""),
                    min         = k.get("min"),
                    max         = k.get("max"),
                    max_len     = k.get("max_len"),
                ))
            except Exception:
                pass
    return result
