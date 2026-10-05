#!/bin/bash

# === Usage check ===
if [ $# -ne 3 ]; then
    echo "Usage: $0 <network> <start> <end>"
    echo "Example: $0 192.168.1.0 1 254"
    exit 1
fi

network=$1
start=$2
end=$3

# Extract the base (e.g., 192.168.1)
base=$(echo "$network" | cut -d'.' -f1-3)

echo "Scanning $base.$start to $base.$end..."

i=$start
while [ "$i" -le "$end" ]; do
    ip="$base.$i"
    if ping -c 1 -W 1 "$ip" > /dev/null 2>&1; then
        echo "$ip is alive"
    fi
    i=$((i + 1))
done
