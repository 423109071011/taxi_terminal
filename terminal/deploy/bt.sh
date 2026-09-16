#!/bin/sh
# 出租车终端 —— 驱动加载脚本（在 /app_taxi 目录下执行）
cd $(dirname $0)

insmod ./key-zlg72128.ko
insmod ./rfid_driver.ko
insmod ./fs6818_led.ko
insmod ./fs6818_pwm.ko
insmod ./servo.ko

# fs6818_led 驱动只注册了主设备号 500，不自动建节点，需要手动 mknod
[ -c /dev/led ] || mknod /dev/led c 500 0

echo "drivers ready:"
ls /dev/input/event4 /dev/rfid_module0 /dev/servo /dev/pwm /dev/led 2>/dev/null
