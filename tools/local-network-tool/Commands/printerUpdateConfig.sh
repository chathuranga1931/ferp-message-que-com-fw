#!/bin/bash

# === Validate parameters ===
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <device_ip> <key> <value>"
    echo "Example: $0 192.168.1.100 name_l1 \"ARADANA\""
    exit 1
fi

DEVICE_IP="$1"
CONFIG_KEY="$2"
CONFIG_VALUE="$3"

# === Allowed config keys ===
VALID_KEYS=(
  "name_l1" "name_l2" "name_l3"
  "add_l1" "add_l2" "add_l3"
  "tele"
  "thank_l1" "thank_l2" "thank_l3" "thank_l4" "thank_l5" "thank_l6"
  "end_lines"
  "baud_rate"
  "en_print_t"
  "theme"
  "udp_srvr_ip"
  "udp_srvr_port"
  "en_udp_ser"
  "en_cloud_print"
  "en_signature"
  "printer_dots"
)

# === Validate config key ===
if [[ ! " ${VALID_KEYS[@]} " =~ " ${CONFIG_KEY} " ]]; then
    echo "❌ Invalid config key: $CONFIG_KEY"
    echo "✅ Allowed keys: ${VALID_KEYS[*]}"
    exit 1
fi

POST_URL="http://$DEVICE_IP/setDeviceConfigurationsPost"

# === Detect value type ===
if [[ "$CONFIG_VALUE" =~ ^[0-9]+$ ]] || [[ "$CONFIG_VALUE" == "true" ]] || [[ "$CONFIG_VALUE" == "false" ]]; then
    JSON_PAYLOAD="{ \"$CONFIG_KEY\": $CONFIG_VALUE }"
else
    JSON_PAYLOAD="{ \"$CONFIG_KEY\": \"${CONFIG_VALUE}\" }"
fi

# === Send request ===
echo "📡 Sending printer config update to $POST_URL..."
echo "📦 Payload: $JSON_PAYLOAD"

curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"

echo "✅ Done."
