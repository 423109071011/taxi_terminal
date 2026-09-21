#!/bin/sh
# 4G one-key start: dial + fix default route
# Usage: /arduino_drivers/start_4g.sh
route del default gw 192.168.1.1 2>/dev/null
cd /app
nohup ./quectel-CM >/tmp/cm.log 2>&1 &
sleep 12
GW=$(grep "route add default" /tmp/cm.log | head -1 | awk '{print $6}')
if [ -n "$GW" ]; then
    route add default gw $GW dev usb0 2>/dev/null
fi
route -n
echo "=== 4G OK, next: /arduino_drivers/run_taxi.sh ==="
echo DONE
