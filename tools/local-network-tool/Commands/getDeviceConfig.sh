

#!/bin/bash

IP="$1"

URL="http://$IP/getDeviceConfigurations"
curl -s "$URL"
echo " "