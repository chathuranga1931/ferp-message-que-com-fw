#!/bin/sh

echo $1

ipAddressList=( \
	"192.168.8.151" \
	"192.168.8.152" \
	"192.168.8.153" \
	"192.168.8.154" \
	"192.168.8.155" \
# "192.168.8.156" \
	"192.168.8.157" \
	"192.168.8.158" \
# "192.168.8.159" \
	"192.168.8.160" \
# "192.168.8.161" \
	"192.168.8.162" \
	"192.168.8.163" \
	"192.168.8.164" \
	"192.168.8.165" \
	)
date=$1

# for ipAddr in "${ipAddressList[@]}"
# do
#     echo "MAC: $ipAddr - Date: $date"
#     url="http://$ipAddr/downloadSD?filename=$date.log -o ferp-iot-com-$ipAddr-$date.log -s"
#     echo $url
#     curl $url
# done

for ipAddr in "${ipAddressList[@]}"
do
    echo "MAC: $ipAddr - Date: $date"
    url="http://$ipAddr/downloadSD?filename=event-pumped-$date.log -o ferp-iot-com-$ipAddr-event-pumped-$date.log -s --connect-timeout 5"
    echo $url
    curl $url
done

for ipAddr in "${ipAddressList[@]}"
do
    echo "MAC: $ipAddr - Date: $date"
    url="http://$ipAddr/downloadSD?filename=event-cloudfailed-$date.log -o ferp-iot-com-$ipAddr-event-cloudfailed-$date.log -s --connect-timeout 5"
    echo $url
    curl $url
done

# macAddressList=("c8f09e2e06f8" "c8f09e2e06f8" "c8f09e2e06f8" "c8f09e2e06f8")
# date=$1

# for mac in "${macAddressList[@]}"
# do
#     echo "MAC: $mac - Date: $date"
#     url="http://ferp-iot-com-$mac/downloadSD?filename=$date.log -o ferp-iot-com-$mac-$date.log -s"
#     echo $url
#     curl $url
# done

