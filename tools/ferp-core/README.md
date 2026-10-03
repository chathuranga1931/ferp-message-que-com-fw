# ferp-core

Shared FERP device protocol logic used by:

- `tools/ferp-device-tool` — Tk desktop tool (adds this folder to `sys.path`)
- `tools/ferp-device-web` — web app backend (`pip install -e ../../ferp-core`)

| Module | Purpose |
|---|---|
| `transports.py` | MQTT / WebAPI / UART config transports, value encode/decode, type ids |
| `mqtt_auth.py` | Command envelope signing (`hop_idx`, `hash`) — keep in sync with `module_mqtt/mqtt_auth.h` |
| `topics.py` | MQTT topic layout and topic-id normalisation |
| `msg_loader.py` | Loads message definitions from `src/app-messages/messages/**.json` |
| `config_keys.py` + `data/config_keys.json` | Config key definitions (edit the JSON to add keys) |
| `ota_bundle.py` | `.bdl` bundle header build / decode |
| `ota_session.py`, `mqtt_ota.py` | OTA over MQTT (`python -m ferp_core.mqtt_ota --help`) |
| `webapi_ota.py` | OTA over HTTP |

No GUI or web-framework code belongs here.
