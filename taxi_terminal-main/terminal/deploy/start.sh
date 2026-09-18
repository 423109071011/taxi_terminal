#!/bin/sh
# 启动脚本：先 4G 拨号（可选，无 EC20 数据业务时注释掉），再启动终端
cd $(dirname $0)

if [ -x /app/quectel-CM ]; then
    /app/quectel-CM > /dev/null &
    sleep 5
fi

./taxi_terminal ./config
