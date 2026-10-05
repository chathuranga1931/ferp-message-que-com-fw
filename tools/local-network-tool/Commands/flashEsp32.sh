#!/bin/sh

# Check if two arguments are provided
if [ $# -ne 2 ]; then
    echo "Usage: $0 <firmware_file> <ip_address>"
    exit 1
fi

# Assign arguments to variables
filename=$2
ip_address=$1

echo "Downloading ${filename} to ${ip_address}"

# Formulate URL
URL="http://${ip_address}/updateFirmwareBin"
echo "URL: $URL"

# Use curl to send firmware file and save response
curl -F "file=@${filename}" "$URL" -o response.txt

# Display response from server
if [ -f response.txt ]; then
    echo -n "Response: "
    cat response.txt
else
    echo "Failed to download firmware."
fi

echo " "
