# ferp-mqtt-tool

Python CLI tools for communicating with FERP devices over MQTT.

## Setup

```bash
pip install -r requirements.txt
```

Python 3.10+ required (uses `match`-free walrus operator, union types via `|`).

---

## ferp_mqtt_tool.py — Command / Response

Send a typed HSYS message command to a device and print the response.

### Read MQTT config

```bash
python ferp_mqtt_tool.py \
  --broker 192.168.1.100 --port 1883 \
  --dev-type ferp-fuel --group site_a --device-id AA:BB:CC:DD:EE:FF \
  --cmd MSG_CONFIG_GET_MQTT
```

### Set a config value

```bash
python ferp_mqtt_tool.py \
  --broker 192.168.1.100 --port 1883 \
  --dev-type ferp-fuel --group site_a --device-id AA:BB:CC:DD:EE:FF \
  --cmd MSG_CONFIG_SET \
  --data '{"key":"mqtt_host","value":"broker.local"}'
```

### Listen for events only

```bash
python ferp_mqtt_tool.py \
  --broker 192.168.1.100 --port 1883 \
  --dev-type ferp-fuel --group site_a --device-id AA:BB:CC:DD:EE:FF \
  --listen
```

### All options

| Option | Default | Description |
|---|---|---|
| `--broker` | *required* | Broker IP or hostname |
| `--port` | `1883` | Broker port |
| `--dev-type` | *required* | Device type string |
| `--group` | *required* | Device group |
| `--device-id` | *required* | Device ID / MAC |
| `--cmd` | — | Message ID to send |
| `--data` | `{}` | JSON data object for the command |
| `--listen` | false | Subscribe and print all events, no command sent |
| `--timeout` | `10.0` | Response wait timeout in seconds |
| `--client-id` | auto | MQTT client ID |

---

## ferp_mqtt_ota.py — OTA Firmware Update

Stream a firmware binary to a device over MQTT.

```bash
python ferp_mqtt_ota.py \
  --broker 192.168.1.100 --port 1883 \
  --dev-type ferp-fuel --group site_a --device-id AA:BB:CC:DD:EE:FF \
  --target main \
  --firmware firmware_v1.2.3.bin \
  --version 1.2.3 \
  --chunk-size 4096
```

Targets: `main` (ESP32 main), `sub1` (sub-processor / display tap)

### All options

| Option | Default | Description |
|---|---|---|
| `--broker` | *required* | Broker IP or hostname |
| `--port` | `1883` | Broker port |
| `--dev-type` | *required* | Device type string |
| `--group` | *required* | Device group |
| `--device-id` | *required* | Device ID / MAC |
| `--target` | *required* | `main` or `sub1` |
| `--firmware` | *required* | Path to `.bin` firmware file |
| `--version` | `unknown` | Version string (informational) |
| `--chunk-size` | `4096` | Bytes per OTA chunk |
| `--ctrl-timeout` | `15.0` | Timeout for ctrl command responses (s) |
| `--chunk-timeout` | `10.0` | Timeout per chunk ack (s) |
| `--max-retries` | `3` | Max chunk retries before aborting |

---

## Topic layout

```
ferp/{dev-type}/{group}/{device_id}/cmd         ← this tool publishes here
ferp/{dev-type}/{group}/{device_id}/resp        ← this tool subscribes here
ferp/{dev-type}/{group}/{device_id}/evt         ← this tool subscribes here
ferp/{dev-type}/{group}/{device_id}/ota/ctrl    ← OTA tool publishes here
ferp/{dev-type}/{group}/{device_id}/ota/data    ← OTA tool publishes here
ferp/{dev-type}/{group}/{device_id}/ota/resp    ← OTA tool subscribes here
```
