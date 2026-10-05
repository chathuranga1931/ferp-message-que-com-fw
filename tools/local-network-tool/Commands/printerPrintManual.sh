#!/bin/bash

# === Validate parameters ===
if [ "$#" -ne 7 ]; then
    echo "Usage: $0 <device_ip> <Pump ID> <Type> <U> <L> <P> <Count>"
    echo "Example: $0 192.168.1.100 "P01" 'Octane 92' 12.02 750.0 50.0 2344"
    exit 1
fi

DEVICE_IP="$1"
NOZZLE_NAME="$2"
T="$3"
U="$4"
L="$5"
P="$6"
NOZZLE_ID="$7"

# === Validate IP format ===
if ! [[ "$DEVICE_IP" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
    echo "❌ Invalid IP: $DEVICE_IP"
    exit 1
fi

# === Validate numeric parameters ===
if ! [[ "$L" =~ ^[0-9]+(\.[0-9]+)?$ ]] || ! [[ "$P" =~ ^[0-9]+(\.[0-9]+)?$ ]] || ! [[ "$U" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
    echo "❌ L, P, and U must be numeric"
    exit 1
fi

# === Optional static values ===
CURRENT_TIME_ISO=$(date -u +"%Y-%m-%dT%H:%M:%SZ")       # UTC with Z
CURRENT_TIME_HUMAN=$(date +"%Y-%m-%dT%H:%M:%S")         # Local time, ISO 8601

PRINT_NOTE="Manual"
POST_URL="http://$DEVICE_IP/print"

# === Construct JSON payload ===
JSON_PAYLOAD=$(cat <<EOF
{
  "time": "$CURRENT_TIME_ISO",
  "print_time": "$CURRENT_TIME_HUMAN",
  "print_note": "$PRINT_NOTE",
  "nozzel_id": "$NOZZLE_ID",
  "measurements": {
    "L": $L,
    "T": "$T",
    "P": $P,
    "U": $U,
    "NE_ID": "$NOZZLE_ID"
  }
}
EOF
)

# === Send POST request ===
echo "🖨️ Sending print request to $POST_URL..."
echo "📦 Payload: $JSON_PAYLOAD"

curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"

echo "✅ Print request sent."
