"""
msg_loader.py  — Load HSYS message definitions from the shared JSON files
                 (src/app-messages/messages/ in the firmware repo, or a local
                 copy synced next to each tool as  messages-json/).

Every JSON file under that directory tree represents one HSYS message class.
File names follow the pattern  msg_snake_name.json  which maps to the C++ class
name  MsgSnakeName  (each word capitalised, underscores removed).

JSON schema per file:
    {
      "msg_id":    "0x0309",
      "direction": "cmd",        # "cmd" | "resp" | "any"  (default: "any")
      "fields": [
        {
          "name":    "host",
          "label":   "Broker Host",       # human label (optional, falls back to name)
          "type":    "string",            # string|uint8|uint16|uint32|int8|int32|bool|float|enum
          "default": "mqtt.example.com",  # optional
          "min":     0,                   # optional (numeric fields)
          "max":     65535,               # optional (numeric fields)
          "options": [                    # only for "enum" type
            {"label": "Option A", "value": 0},
            ...
          ]
        },
        ...
      ]
    }

Usage
-----
    from ferp_core.msg_loader import load_message_defs, group_messages, devinfo_keys
    defs = load_message_defs("path/to/messages-json")
    # defs = { "MsgConfigMqtt": {msg_id, direction, fields, group}, ... }
"""

import json
import os
import warnings
from typing import Dict, List, Tuple


def _snake_to_class(stem: str) -> str:
    """'msg_config_mqtt' → 'MsgConfigMqtt'"""
    return "".join(w.capitalize() for w in stem.split("_"))


def load_message_defs(msgs_dir: str) -> Dict[str, dict]:
    """Load all JSON definitions under *msgs_dir*.

    Returns { class_name: {"msg_id", "direction", "fields", "group"} }.
    The group is the name of the sub-directory the file lives in.
    """
    defs: Dict[str, dict] = {}
    msgs_dir = os.fspath(msgs_dir)

    if not os.path.isdir(msgs_dir):
        warnings.warn(f"msg_loader: messages directory not found: {msgs_dir}")
        return defs

    for root, dirs, files in os.walk(msgs_dir):
        dirs.sort()
        for fname in sorted(files):
            if fname.startswith("._") or not fname.endswith(".json"):
                continue
            stem       = fname[:-5]                # "msg_config_mqtt"
            class_name = _snake_to_class(stem)     # "MsgConfigMqtt"
            fpath      = os.path.join(root, fname)
            try:
                with open(fpath, "r", encoding="utf-8") as fh:
                    data = json.load(fh)
            except Exception as exc:
                warnings.warn(f"msg_loader: could not load {fpath}: {exc}")
                continue

            raw_id = data.get("msg_id", "0x0000")
            defs[class_name] = {
                "msg_id":    int(raw_id, 16) if isinstance(raw_id, str) else int(raw_id),
                "direction": data.get("direction", "any"),
                "fields":    data.get("fields", []),
                "group":     os.path.basename(root),
            }
    return defs


def group_messages(defs: Dict[str, dict]) -> Dict[str, List[str]]:
    """{ group: [class_name, ...] } sorted by group and name."""
    groups: Dict[str, List[str]] = {}
    for name in sorted(defs):
        groups.setdefault(defs[name].get("group", "Other"), []).append(name)
    return dict(sorted(groups.items()))


def devinfo_keys(defs: Dict[str, dict]) -> List[Tuple[int, str, str]]:
    """
    Returns [(key_id, bar_label, field), ...] for the Device Info bar.
    Derived from the options of the 'key' field in MsgDevInfoRead, using the
    'bar_label' and 'field' metadata on each option entry.
    """
    opts = (defs.get("MsgDevInfoRead", {})
                .get("fields", [{}])[0]
                .get("options", []))
    result = []
    for o in opts:
        try:
            result.append((int(o["value"]), o.get("bar_label") or o.get("label", ""), o.get("field", "")))
        except Exception:
            pass
    return result
