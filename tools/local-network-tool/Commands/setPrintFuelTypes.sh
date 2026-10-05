#!/bin/sh

# === Validate parameters ===
if [ "$#" -ne 6 ]; then
    echo "Usage: $0 <device_ip> <display_type> <nozzel_id_0> <fuel_type_0> <nozzel_id_1> <fuel_type_1>"
    exit 1
fi

DEVICE_IP="$1"
DISPLAY_TYPE="$2"
NOZZLE_ID_0="$3"
FUEL_TYPE_0="$4"
NOZZLE_ID_1="$5"
FUEL_TYPE_1="$6"

POST_URL="http://$DEVICE_IP/setDeviceConfigurationsPost"

# === Build JSON payload ===
JSON_PAYLOAD=$(cat <<EOF
{
  "display_type": $DISPLAY_TYPE,
  "nozzel_id_for_print_0": "$NOZZLE_ID_0",
  "fuel_type_str_for_print_0": "$FUEL_TYPE_0",
  "nozzel_id_for_print_1": "$NOZZLE_ID_1",
  "fuel_type_str_for_print_1": "$FUEL_TYPE_1"
}
EOF
)

# === Send request ===
echo "📡 Sending print fuel types to $POST_URL..."
curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"
echo "✅ Done."
