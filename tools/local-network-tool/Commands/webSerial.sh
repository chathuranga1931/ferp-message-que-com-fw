#!/bin/bash

IP="$1"
ACTION="$2"

if [ "$ACTION" != "enable" ] && [ "$ACTION" != "disable" ]; then
    echo "Usage: $0 <ip> <enable|disable>"
    exit 1
fi

if [ "$ACTION" == "enable" ]; then
    URL="http://$IP/enableWebSerialLogs"
else
    URL="http://$IP/disableWebSerialLogs"
fi

echo "Sending WebSerial $ACTION command to $IP"
curl -s "$URL"
echo " "
