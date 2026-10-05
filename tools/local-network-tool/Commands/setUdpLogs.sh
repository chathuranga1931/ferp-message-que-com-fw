#!/bin/bash

IP="$1"
ACTION="$2"

if [ "$ACTION" != "enable" ] && [ "$ACTION" != "disable" ]; then
    echo "Usage: $0 <ip> <enable|disable>"
    exit 1
fi

if [ "$ACTION" == "enable" ]; then
    URL="http://$IP/enableUDPSerialLogs"
else
    URL="http://$IP/disableUDPSerialLogs"
fi

echo "Sending UDPLogs $ACTION command to $IP"
curl -s "$URL"
echo " "
