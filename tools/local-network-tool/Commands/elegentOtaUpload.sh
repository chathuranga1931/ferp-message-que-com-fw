#!/bin/sh
FIRMWARE_FILE="$1"
DEVICE_IP="$2"
if [ -z "$FIRMWARE_FILE" ] || [ -z "$DEVICE_IP" ]; then
    echo "? Usage: sh ElegentOtaUpload.sh <firmware.bin> <device_ip>"
    exit 1
fi
if [ ! -f "$FIRMWARE_FILE" ]; then
    echo "? Firmware file not found: $FIRMWARE_FILE"
    exit 1
fi
OTA_URL="http://$DEVICE_IP/update"
USERNAME="FERP"
PASSWORD="FERP2873"

MD5_HASH=$(md5sum "$FIRMWARE_FILE" | awk '{ print $1 }')
echo "? MD5: $MD5_HASH"

echo "?? Uploading '$FIRMWARE_FILE' to $OTA_URL..."
curl -u "$USERNAME:$PASSWORD" \
     -F "MD5=$MD5_HASH" \
     -F "file=@$FIRMWARE_FILE" \
     "$OTA_URL"
     
     