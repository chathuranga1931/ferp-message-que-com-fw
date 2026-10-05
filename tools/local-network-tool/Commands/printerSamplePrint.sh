#!/bin/bash

IP="$1"

if [ -z "$IP" ]; then
    echo "Usage: $0 <ip-address>"
    exit 1
fi

echo "Sending printSample command to $IP"
curl -s "http://$IP/printSample"
echo " "