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

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l1       "       D.D. Kasthuriarachchi & Sons      "
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l2       "       82, Badulla Rd, Nuwara Eliya      "
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" name_l3       ""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l1        ""
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l2        ""
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" add_l3        ""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" tele          "Tel: - 052 2222 826"

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l1      "Thank you for your Business!"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l2      "Come again!"
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l3      ""
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l4      ""
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l5      ""
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" thank_l6      ""

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" end_lines     8

$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" en_print_t     false
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" theme         2
$SCRIPT_DIR/$SCRIPTNAME "$DEVICE_IP" printer_dots  42

echo "✅ All printer configs updated for $DEVICE_IP"
