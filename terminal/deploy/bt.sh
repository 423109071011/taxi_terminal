#!/bin/sh
# 出租车终端 —— 驱动加载脚本
# 用法：把本目录(deploy)与 taxi_terminal 一起放到板上任意目录后执行 ./bt.sh
# 驱动 .ko 依次在脚本所在目录、/arduino_drivers、/ 三处查找
cd $(dirname $0)

find_ko() {
    for d in . /arduino_drivers /; do
        if [ -f "$d/$1" ]; then echo "$d/$1"; return 0; fi
    done
    return 1
}

load() {
    p=$(find_ko "$1")
    if [ -n "$p" ]; then
        insmod "$p" 2>/dev/null || echo "  ($1 already loaded or failed)"
    else
        echo "  WARN: $1 not found (skip)"
    fi
}

load zlg7290.ko          # 数码管+键盘 /dev/zlg7290（课时10）
load key-zlg72128.ko     # 备用键盘驱动
load rfid_driver.ko      # RFID /dev/rfid_module0（课时8）
load beep_driver.ko      # 蜂鸣器 /dev/beep（课时7）
load servo.ko            # 舵机   /dev/servo（课时12）
load fs6818_led.ko       # 状态灯 /dev/led（课时6，可选）

# fs6818_led 驱动只注册主设备号 500，不自动建节点时手动 mknod
[ -c /dev/led ] || mknod /dev/led c 500 0 2>/dev/null

echo "drivers ready:"
ls /dev/zlg7290 /dev/rfid_module0 /dev/beep /dev/servo /dev/led 2>/dev/null
