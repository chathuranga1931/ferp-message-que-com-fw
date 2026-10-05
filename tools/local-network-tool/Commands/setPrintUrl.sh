#!/bin/sh

# === Check for IP and printer URL parameters ===
if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: $0 <device_ip> <printer_url>"
    exit 1
fi

DEVICE_IP="$1"
PRINTER_URL="$2"
POST_URL="http://$DEVICE_IP/setDeviceConfigurationsPost"

JSON_PAYLOAD=$(cat <<EOF
{
  "printer_url": "$PRINTER_URL"
}
EOF
)

echo "📡 Setting printer URL ($PRINTER_URL) to $POST_URL..."
curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"
echo "✅ Done."
