#!/bin/bash

ip_address="${1}"
filter="${2}"

if [ -z "$ip_address" ]; then
  echo "Usage: $0 <ip-address>"
  exit 1
fi

# ========== Main thread counter ==========
echo
URL="http://${ip_address}/getMainThreadCounter"
echo "$URL"
output=$(curl -s "$URL")
echo "$output"