#!/bin/bash

ip_address="${1}"
mac_address="${1}"  # if you want to distinguish by MAC in filenames

if [ -z "$ip_address" ]; then
  echo "Usage: $0 <ip-address>"
  exit 1
fi

# ========== Download logfile index ==========
URL="http://${ip_address}/showFileSD?filename=logfileindex"
echo "$URL"
curl -s -o logfileindex "$URL"
if [ $? -eq 0 ]; then
  echo "logfileindex downloaded."
else
  echo "Failed to fetch logfileindex"
  exit 1
fi

# ========== Extract and normalize log ID ==========
logid=$(cat logfileindex)
echo "Current Log ID = ${logid}"

integer_range=$(echo "$logid" | sed 's/^0*//')
[ -z "$integer_range" ] && integer_range=0
logid=$(printf "%03d" "$integer_range")
echo "Next Log Id = ${logid}"

# ========== Log download function ==========
getfile() {
    local loc_logid="$1"

    URL="http://${ip_address}/showFileSD?filename=log-${loc_logid}.log"
    echo "$URL"
    curl -s -o "${mac_address}-log-${loc_logid}.log" "$URL"
    mkdir -p "../Logs"
    mv "${mac_address}-log-${loc_logid}.log" "../Logs/${mac_address}-log-${loc_logid}.log"
    if [ $? -eq 0 ]; then
        echo "Log file saved as ${mac_address}-log-${loc_logid}.log"
    else
        echo "Failed to fetch log-${loc_logid}.log"
    fi
}

# ========== Loop for user interaction ==========
while true; do
    echo
    read -p "Press 'y' to get log file ${logid}, or 'n' to exit: " choice

    case "$choice" in
        [Yy]*)
            getfile "$logid"
            ;;
        [Nn]*)
            echo "Exiting..."
            break
            ;;
        *)
            echo "Invalid input. Use 'y' or 'n'."
            ;;
    esac

    integer_range=$(echo "$logid" | sed 's/^0*//')
    [ -z "$integer_range" ] && integer_range=0

    if [ "$integer_range" -eq 0 ]; then
        integer_range=99
    else
        integer_range=$((integer_range - 1))
    fi

    logid=$(printf "%03d" "$integer_range")
    echo "Next Log Id = ${logid}"
done
