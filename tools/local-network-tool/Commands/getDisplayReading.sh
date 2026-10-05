#!/bin/bash

IP="$1"
NOZZLE_ID="$2"

# if [ -z "$NOZZLE_ID" ]; then
#     echo "Usage: $0 <ip> <nozzle_id>"
#     exit 1
# fi

# URL="http://$IP/getDisplayReading?nozzel_id=$NOZZLE_ID"

# echo "Fetching display reading for nozzle $NOZZLE_ID from $IP"
# curl -s "$URL"
# echo " "

URL="http://$IP/getDisplayReading?nozzel_id=0"

# echo "Fetching display reading for nozzle $NOZZLE_ID from $IP"
curl -s "$URL"
echo " "

URL="http://$IP/getDisplayReading?nozzel_id=1"

# echo "Fetching display reading for nozzle $NOZZLE_ID from $IP"
curl -s "$URL"
echo " "
