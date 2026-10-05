#!/bin/bash

DEVICE_IP="$1"
dt_log_rate="$2"
POST_URL="http://$DEVICE_IP/setDeviceConfigurationsPost"

# Function to validate dt_log_rate
validate_dt_log_rate() {
  local value="$1"
  # Check if value is a number and within the 0-500 range
  if [[ "$value" =~ ^[0-9]+$ ]] && [ "$value" -ge 0 ] && [ "$value" -le 500 ]; then
    return 0  # Valid
  else
    echo "Error: dt_log_rate must be an integer between 0 and 500"
    exit 1
  fi
}

# Validate input
validate_dt_log_rate "$dt_log_rate"

# Prepare JSON payload
JSON_PAYLOAD=$(cat <<EOF
{
  "dt_log_rate": $dt_log_rate
}
EOF
)

# Send the POST request
curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"

echo "✅ Done."
