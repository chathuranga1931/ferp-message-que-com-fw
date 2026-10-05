#!/bin/bash

ip_address="${1}"
filter="${2}"

if [ -z "$ip_address" ]; then
  echo "Usage: $0 <ip-address>"
  exit 1
fi

# ========== Device information ==========
# echo
URL="http://${ip_address}/getDeviceInformation"
# echo "$URL"
device_info=$(curl -s "$URL")

keys_and_values=$(echo "$device_info" | grep -o '"[^"]\+":\s*"[^"]*"' | sed 's/"//g')
aligned_keys_and_values=$(echo "$keys_and_values" | awk -F':' -v OFS=':' '{ $1=sprintf("%-30s", $1); print }')

# echo
echo "Device Info (Key: Value)"
echo "$aligned_keys_and_values" | grep "$filter"
