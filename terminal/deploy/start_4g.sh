#!/bin/sh
# 4G one-key start: dial + fix default route
# Usage: /arduino_drivers/start_4g.sh
#
# 注意：EC20 冷启动（上电首次拨号）SIM 初始化+驻网可能超过 30s，
# 固定 sleep 12s 会没等到网关就误报 OK。这里轮询等待，最长 45s。
route del default gw 192.168.1.1 2>/dev/null
cd /app
nohup ./quectel-CM >/tmp/cm.log 2>&1 &

GW=""
i=0
while [ $i -lt 45 ]; do
    sleep 3
    i=$((i + 3))
    GW=$(grep "route add default" /tmp/cm.log | head -1 | awk '{print $6}')
    [ -n "$GW" ] && break
done

if [ -n "$GW" ]; then
    route add default gw $GW dev usb0 2>/dev/null
else
    echo "=== 4G FAILED: no gateway in cm.log after 45s ==="
    echo "    check: tail -20 /tmp/cm.log ; ps | grep quectel"
    route -n
    echo DONE
    exit 1
fi
route -n
echo "=== 4G OK (gw=$GW), next: /arduino_drivers/run_taxi.sh ==="
echo DONE
