#!/bin/bash

# === Validate parameters ===
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <device_ip>"
    exit 1
fi

DEVICE_IP="$1"
SCRIPT_DIR=$(dirname "$0")
SCRIPTNAME="printerUpdateConfig.sh"

# === Update each config value using SCRIPTNAME ===

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l1        		"N T K Wijesekara & Son"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l2        		"Lanka Filling Station"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l3        		""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l1         		"Wanduramba"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l2         		"A/C No - 105068"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l3         		""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" tele           		"TELE: 0912292433"

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l1       		"THANK YOU! COME AGAIN!"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l2       		"FEEDBACK 0770101916"

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" end_lines      		6

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" en_print_t     		false
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" theme          		1
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" en_cloud_print         true

echo "✅ All printer configs updated for $DEVICE_IP"
