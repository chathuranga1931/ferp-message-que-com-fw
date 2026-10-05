#!/bin/bash

IP="$1"
FILENAME="$2"

if [ -z "$FILENAME" ]; then
    echo "Usage: $0 <ip> <filename>"
    exit 1
fi

echo "Fetching SD file '$FILENAME' from $IP"
curl -O "http://$IP/api/getsdfile/$FILENAME"
echo " "
