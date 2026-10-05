#!/bin/bash

DEVICE_IP="$1"
p_cpy_cnt="$2"
POST_URL="http://$DEVICE_IP/setDeviceConfigurationsPost"

# Function to validate p_cpy_cnt
validate_p_cpy_cnt() {
  local value="$1"
  # Check if value is a number and less than 10
  if [[ "$value" =~ ^[0-9]+$ ]] && [[ "$value" -lt 10 ]]; then
    return 0  # Valid
  else
    echo "Error: p_cpy_cnt must be an integer less than 10"
    exit 1
  fi
}

# Validate input
validate_p_cpy_cnt "$p_cpy_cnt"

# Prepare JSON payload
JSON_PAYLOAD=$(cat <<EOF
{
  "p_cpy_cnt": $p_cpy_cnt
}
EOF
)

# Send the POST request
curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"

echo "✅ Done."
