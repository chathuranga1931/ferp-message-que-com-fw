#!/bin/bash


# === Validate parameters ===
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <device_ip> <UDP server IP> <UDP Server Port>"
    exit 1
fi

# Function to validate server port
validate_server_port() {
  local port=$1
  if ! [[ "$port" =~ ^[0-9]+$ ]] || [ "$port" -lt 1 ] || [ "$port" -gt 65535 ]; then
    echo "❌ Invalid port: $port"
    exit 1
  fi
}

# Function to validate IP address
validate_server_ip() {
  local ip=$1
  if ! [[ "$ip" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
    echo "❌ Invalid IP: $ip"
    exit 1
  fi
}


# Input values (you can replace with read or environment variables)
DEVICE_IP="$1"
server_ip="$2"
server_port="$3"


POST_URL="http://$DEVICE_IP/setDeviceConfigurationsPost"

# Validate inputs
validate_server_port "$server_port"
validate_server_ip "$server_ip"

# Prepare JSON payload
JSON_PAYLOAD=$(cat <<EOF
{
  "udp_srvr_port": $server_port,
  "udp_srvr_ip": "$server_ip"
}
EOF
)


# === Send request ===
echo "📡 Sending Udp Serial Logs $POST_URL..."
curl -X POST "$POST_URL" \
     -H "Content-Type: application/json" \
     -d "$JSON_PAYLOAD"
echo "✅ Done."
