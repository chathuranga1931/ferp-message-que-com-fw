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
# VALID_KEYS=(
#   "en_nid_prnt" "printer_url"
#   "fuel_type_str_for_print_0" "fuel_type_str_for_print_1"
#   "nozzel_id_for_print_0" "nozzel_id_for_print_1"
# )

# === Validate config key ===
# if [[ ! " ${VALID_KEYS[@]} " =~ " ${CONFIG_KEY} " ]]; then
#     echo "❌ Invalid config key: $CONFIG_KEY"
#     echo "✅ Allowed keys: ${VALID_KEYS[*]}"
#     exit 1
# fi

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
