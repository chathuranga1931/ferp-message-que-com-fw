

#!/bin/bash

IP="$1"

URL="http://$IP/getDeviceInformation"
curl -s "$URL"
echo " "