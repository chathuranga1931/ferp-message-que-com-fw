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
VALID_KEYS=(display_type end_lines printer_dots en_cut lines_aftr_cut nzle_swap udp_srvr_ip udp_srvr_port en_udp_ser hb_interval dt_log_rate p_cpy_cnt theme en_nid_cloud en_nid_prnt en_retx en_cloud_print en_udp_ser mqtt_port mqtt_url)

# === Validate config key ===
key_is_valid=0
for k in "${VALID_KEYS[@]}"; do
    if [[ "$CONFIG_KEY" == "$k" ]]; then
        key_is_valid=1
        break
    fi
done

if [[ $key_is_valid -ne 1 ]]; then
    echo "Invalid config key: $CONFIG_KEY"
    echo "Allowed keys: ${VALID_KEYS[*]}"
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
echo "Sending printer config update to $POST_URL..."
echo "Payload: $JSON_PAYLOAD"

curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"

echo "Done."
