#!/bin/bash

NEW_SSID="$1"
NEW_PASS="$2"
TEST_HOST="8.8.8.8"
DEVICE="wlan0"

sudo iwlist wlan0 scan | grep -E "ESSID:|Quality="

# Get current active connection name
CUR_CONN=$(nmcli -t -f NAME,DEVICE connection show --active | grep "$DEVICE" | cut -d: -f1)
echo "Current active WiFi: $CUR_CONN"

# Try to connect to the new WiFi
nmcli device wifi connect "$NEW_SSID" password "$NEW_PASS"

sleep 10  # Wait a bit for connection

# Test if internet is available
if ping -c 2 "$TEST_HOST" > /dev/null; then
    echo "Internet is working on $NEW_SSID"
    exit 0
else
    echo "Internet is NOT working on $NEW_SSID, switching back to $CUR_CONN"
    nmcli connection up "$CUR_CONN"
    exit 1
fi
