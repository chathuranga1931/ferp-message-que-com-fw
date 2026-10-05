#!/bin/bash

ip_address="${1}"

if [ -z "$ip_address" ]; then
  echo "Usage: $0 <ip-address>"
  exit 1
fi

# ========== startup.log ==========
echo
URL="http://${ip_address}/showLogFileSPIFFs?filename=startup.log"
echo "$URL"
curl -s -o startup.log "$URL"
if [ $? -eq 0 ]; then
  echo "GET request successful. Log file saved as startup.log."
  cat startup.log
else
  echo "Failed to fetch startup.log"
fi

# ========== Main thread counter ==========
echo
URL="http://${ip_address}/getMainThreadCounter"
echo "$URL"
output=$(curl -s "$URL")
echo "$output"

# ========== Display reading ==========
echo
URL="http://${ip_address}/getDisplayReading"
echo "$URL"
output=$(curl -s "$URL")
echo "$output"

# ========== Device information ==========
echo
URL="http://${ip_address}/getDeviceInformation"
echo "$URL"
device_info=$(curl -s "$URL")

keys_and_values=$(echo "$device_info" | grep -o '"[^"]\+":\s*"[^"]*"' | sed 's/"//g')
aligned_keys_and_values=$(echo "$keys_and_values" | awk -F':' -v OFS=':' '{ $1=sprintf("%-30s", $1); print }')

echo
echo "Device Info (Key: Value)"
echo "$aligned_keys_and_values"

# ========== restransmitStatus.json ==========
echo
URL="http://${ip_address}/showFileSD?filename=restransmitStatus.json"
echo "$URL"
curl -s -o restransmitStatus.json "$URL"
if [ $? -eq 0 ]; then
  echo "restransmitStatus.json downloaded."
  cat restransmitStatus.json
else
  echo "Failed to fetch restransmitStatus.json"
fi
