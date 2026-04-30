"""
msg_defs.py

Mapping of MQTT message class names to their JSON payload schemas.

The wire envelope is:  { "seq": N, "msg": "<ClassName>", "data": { ... } }

"msg" is the C++ class name (e.g. "MsgConfigGetMqtt"), NOT the numeric ID.
This makes the protocol human-readable and backward-compatible — if message
numeric IDs change, the string names remain stable.

Each entry:
  ClassName -> { "fields": { field_name: python_type } }
"""

# ---------------------------------------------------------------------------
# Command messages (host → device)  — class names match C++ exactly
# ---------------------------------------------------------------------------
CMD_MSGS = {
    # Config read requests — no data payload needed
    "MsgConfigGetMqtt":      {"fields": {}},
    "MsgConfigGetWifi":      {"fields": {}},
    "MsgConfigGetCloud":     {"fields": {}},
    "MsgConfigGetOta":       {"fields": {}},

    # Generic config set — key/type/value
    "MsgConfigSet": {
        "fields": {
            "key":   str,
            "type":  str,   # "string" | "uint32" | "bool"
            "value": str,
        }
    },
}

# ---------------------------------------------------------------------------
# Response / event messages (device → host)  — class names match C++ exactly
# ---------------------------------------------------------------------------
RESP_MSGS = {
    "MsgConfigMqtt": {
        "fields": {
            "host":     str,
            "port":     int,
            "user":     str,
            "password": str,
        }
    },
    "MsgConfigWifi": {
        "fields": {
            "ssid":     str,
            "password": str,
        }
    },
    "MsgSensorData": {
        "fields": {
            "counter":     int,
            "temperature": float,
        }
    },
    "MsgFuelPumped": {
        "fields": {
            "nozzle_idx":      int,
            "vol_lx1000":      int,
            "unit_pricex100":  int,
            "total_pricex100": int,
        }
    },
    "MsgNozzleState": {
        "fields": {
            "nozzle_idx": int,
            "state":      int,
        }
    },
    "MsgInternetStatus": {
        "fields": {
            "connected": bool,
        }
    },
    "MsgOtaEvent": {
        "fields": {
            "event":      int,
            "target_idx": int,
            "version":    str,
        }
    },
    "MsgOtaProgress": {
        "fields": {
            "target_idx":    int,
            "percent":       int,
            "bytes_written": int,
            "total_bytes":   int,
        }
    },
    "MsgMqttStatus": {
        "fields": {
            "connected": bool,
        }
    },
}

# Combined lookup
ALL_MSGS = {**CMD_MSGS, **RESP_MSGS}
