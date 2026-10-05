#!/bin/bash

ip_address="${1}"

if [ -z "$ip_address" ]; then
  echo "Usage: $0 <ip-address>"
  exit 1
fi

echo
URL="http://${ip_address}/showLogFileSPIFFs?filename=startup.log"
echo "$URL"
curl -s -o startup.log "$URL"
if [ $? -eq 0 ]; then
  echo "GET request successful. Log file saved as startup.log."
  cat startup.log
else
  echo "Failed to fetch startup.log"
  exit 1
fi
