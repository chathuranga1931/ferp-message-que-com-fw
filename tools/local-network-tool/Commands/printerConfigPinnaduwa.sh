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

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l1        		"             Y.V.A. JANAKA              "
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l2        		"         LANKA FILLING STATION          "
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l3        		""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l1         		"          NEW TOWN - PINNADUWA          "
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l2         		"  Tel 0770462919       A/C No - 104991  "
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l3         		""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" tele           		""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l1       		"Super Petrol (95% Octane), Super Diesel"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l2       		"Are available Now"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l3       		"Open at 6.00am to 10.00pm"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l4       		"THANK YOU, COME AGAIN"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l5       		""
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l6       		""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" end_lines      		12

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" en_print_t     		false
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" theme          		2
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" en_cloud_print         false

# Enable signature printing is supported only in theme 2 == 3rd theme, Others the signature is always printed
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" en_signature           false

echo "✅ All printer configs updated for $DEVICE_IP"
