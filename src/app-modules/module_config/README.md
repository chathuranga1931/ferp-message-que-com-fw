# ModuleConfig

**Module ID:** `MODULE_CONFIG_ID` (6)  
**Task:** `config_task` — priority 5, 4 KB stack  
**Status:** ✅ Implemented

## Purpose

Owns the device configuration. Loads `DeviceConfigs.json` from SPIFFS, exposes
the parsed values as a typed `app_config_t` struct, and re-publishes the full
snapshot whenever the config changes.

## Messages

| Direction | Message | ID | Notes |
|-----------|---------|----|-------|
| Subscribes | `MsgSpiffsReady` | `0x0201` | Triggers file read |
| Subscribes | `MsgConfigSet`   | `0x0301` | Update one field by key |
| Subscribes | `MsgConfigGet`   | `0x0302` | Re-publish current snapshot |
| Publishes  | `MsgConfigReady` | `0x0300` | Full `app_config_t` snapshot |

## Behaviour

1. Waits for `MsgSpiffsReady`.
2. Reads `Configs/DeviceConfigs.json` from SPIFFS.
3. Parses JSON using `hsys_config_load_from_json()` (ArduinoJson on both
   platforms — platform-independent).
4. Publishes `MsgConfigReady` with the full config snapshot.
5. On `MsgConfigSet`: updates the named field in `app_config_t`, persists the
   updated JSON back to SPIFFS, publishes `MsgConfigReady` again.
6. On `MsgConfigGet`: re-publishes `MsgConfigReady` immediately (no file read).

## Configuration fields (`app_config_t`)

| Key | Type | Description |
|-----|------|-------------|
| `ssid` | string | WiFi SSID |
| `password` | string | WiFi password |
| `cloud_url` | string | Cloud endpoint |
| `cloud_secret` | string | Cloud auth token |
| `mqtt_host` | string | MQTT broker hostname |
| `mqtt_port` | uint32 | MQTT broker port |
| `device_uuid` | string | Device identity UUID |
| … | … | (see `app_config.h` for full list) |

## Dependencies

- `hsys_config` (middleware) — JSON serialiser/deserialiser (ArduinoJson).
- `ModuleSpiffs` — must mount before config can load.
- `pal_spiffs` (indirectly via `app_spiffs`).

## Pending

- `MsgConfigResetRequest` — factory-reset to compiled-in defaults (Sprint 12).
