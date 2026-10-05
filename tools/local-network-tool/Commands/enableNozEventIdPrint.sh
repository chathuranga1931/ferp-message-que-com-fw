#!/bin/bash

DEVICE_IP="$1"
en_nid_prnt="$2"
POST_URL="http://$DEVICE_IP/setDeviceConfigurationsPost"

# Function to validate en_nid_prnt
validate_en_nid_prnt() {
  local value="$1"
  if [[ "$value" == "true" || "$value" == "false" ]]; then
    return 0  # Valid
  else
    echo "Error: en_nid_prnt must be 'true' or 'false'"
    exit 1
  fi
}

# Validate input
validate_en_nid_prnt "$en_nid_prnt"

# Prepare JSON payload
JSON_PAYLOAD=$(cat <<EOF
{
  "en_nid_prnt": $en_nid_prnt
}
EOF
)

# Send the POST request
curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"

echo "✅ Done."
