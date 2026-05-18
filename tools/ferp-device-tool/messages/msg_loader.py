"""
msg_loader.py  — Dynamically load message definitions from the shared JSON files
                 in  src/app-messages/messages/.

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

Exports
-------
MSG_DEFS  — full metadata dict: { class_name -> {msg_id, direction, fields} }
CMD_MSGS  — compat shim for direction "cmd" or "any"  → {class_name: {fields: {name: pytype}}}
RESP_MSGS — compat shim for direction "resp" or "any" → same shape
ALL_MSGS  — all messages, same shape
"""

import json
import os
import warnings

# ---------------------------------------------------------------------------
# Locate the messages JSON directory (bundled alongside this tool)
# ---------------------------------------------------------------------------
# Layout:
#   <tool>/messages/msg_loader.py   ← this file
#   <tool>/messages-json/           ← JSON message definitions (local copy)
#
_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
MSGS_DIR  = os.path.normpath(
    os.path.join(_THIS_DIR, "..", "messages-json")
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _snake_to_class(stem: str) -> str:
    """'msg_config_mqtt' → 'MsgConfigMqtt'"""
    return "".join(w.capitalize() for w in stem.split("_"))


def _json_type_to_py(jtype: str) -> type:
    """Return the Python type that best represents a JSON field type."""
    if jtype in ("uint8", "uint16", "uint32", "int8", "int16", "int32"):
        return int
    if jtype in ("float", "double"):
        return float
    if jtype == "bool":
        return bool
    if jtype == "enum":
        # enum values may be int or str — use str as the safe common type;
        # callers that care will look at the options list in MSG_DEFS.
        return str
    return str   # "string" and anything else


# ---------------------------------------------------------------------------
# Load all definitions from JSON
# ---------------------------------------------------------------------------

#: Full per-message metadata:
#:   { "MsgConfigMqtt": { "msg_id": 0x0309, "direction": "resp",
#:                         "fields": [ {name, label, type, default?, options?, ...} ] } }
MSG_DEFS: dict[str, dict] = {}


def reload(msgs_dir: str = MSGS_DIR) -> None:
    """(Re)load all JSON definitions from *msgs_dir* into MSG_DEFS."""
    MSG_DEFS.clear()

    if not os.path.isdir(msgs_dir):
        warnings.warn(f"msg_loader: messages directory not found: {msgs_dir}")
        return

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

            raw_id    = data.get("msg_id", "0x0000")
            msg_id    = int(raw_id, 16) if isinstance(raw_id, str) else int(raw_id)
            direction = data.get("direction", "any")
            fields    = data.get("fields", [])

            group = os.path.basename(root)   # subdirectory name = logical group
            MSG_DEFS[class_name] = {
                "msg_id":    msg_id,
                "direction": direction,
                "fields":    fields,
                "group":     group,
            }


reload()


# ---------------------------------------------------------------------------
# Backward-compatible compat dicts
# Shape: { class_name: {"fields": {field_name: python_type}} }
# ---------------------------------------------------------------------------

def _compat_entry(defn: dict) -> dict:
    return {
        "fields": {
            f["name"]: _json_type_to_py(f.get("type", "string"))
            for f in defn["fields"]
        }
    }


def _build_compat() -> tuple[dict, dict, dict]:
    cmd, resp, all_ = {}, {}, {}
    for name, defn in MSG_DEFS.items():
        entry = _compat_entry(defn)
        all_[name] = entry
        if defn["direction"] in ("cmd", "any"):
            cmd[name] = entry
        if defn["direction"] in ("resp", "any"):
            resp[name] = entry
    return cmd, resp, all_


CMD_MSGS, RESP_MSGS, ALL_MSGS = _build_compat()
