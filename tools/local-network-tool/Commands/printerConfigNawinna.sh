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

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l1        "PETRO GAS (Pvt) Ltd"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l2        ""
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l3        ""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l1         "395, Highlevel Road, Nawinna,"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l2         "Maharagama."
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l3         ""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" tele           "TELE 0112804630"

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l1       "THANK YOU! COME AGAIN!"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l2       "FEEDBACK 0112804630"

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" end_lines      5

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" en_print_t     false
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" theme          0

echo "✅ All printer configs updated for $DEVICE_IP"
