#!/bin/sh
# 出租车终端 —— 驱动加载脚本（自动补建设备节点）
# 用法：把本目录(deploy)与 taxi_terminal 一起放到板上任意目录后执行 ./bt.sh
# 驱动 .ko 依次在脚本所在目录、/arduino_drivers、/ 三处查找
# 参考：老师原始 bt.sh（加载 / 下全部模块、仅手动 mknod /dev/led c 500 0），
#       我们的 NFS rootfs 无预置节点，故所有节点统一在此自动创建。
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

# misc 设备：主设备号固定 10，次设备号查 /proc/misc（如 "41 zlg72128-0"）
mk_misc() {
    [ -c "$2" ] && return 0
    set -- $(grep "$1" /proc/misc 2>/dev/null)
    if [ -n "$1" ]; then
        mknod "$2" c 10 "$1" 2>/dev/null && echo "  mknod $2 c 10 $1"
    else
        echo "  WARN: misc '$1' not registered yet"
    fi
}

# 字符设备：主设备号查 /proc/devices（如 " 248 rfid"），次设备号驱动内固定
mk_chr() {
    [ -c "$2" ] && return 0
    set -- $(grep -w "$1" /proc/devices 2>/dev/null)
    if [ -n "$1" ]; then
        mknod "$2" c "$1" "$3" 2>/dev/null && echo "  mknod $2 c $1 $3"
    else
        echo "  WARN: chardev '$1' not registered yet"
    fi
}

load key-zlg72128.ko     # 键盘+数码管 i2cKEY + /dev/zlg72128-0（注意勿与 zlg7290.ko 同载，I2C 地址冲突）
load rfid_driver.ko      # RFID   字符设备 rfid，minor 固定 8 → /dev/rfid_module0
load beep_driver.ko      # 蜂鸣器 misc → /dev/beep
load servo.ko            # 舵机   misc → /dev/servo
load fs6818_led.ko       # 状态灯 主设备号静态 500 → /dev/led（可选）

# ---- 补建设备节点（板子无 devtmpfs/udev，重启后节点会丢）----
mk_misc  zlg72128-0 /dev/zlg72128-0
mk_misc  beep       /dev/beep
mk_misc  servo      /dev/servo
mk_chr   rfid       /dev/rfid_module0 8
[ -c /dev/led ] || mknod /dev/led c 500 0 2>/dev/null
# input event 次设备号动态（i2cKEY 常在 event4），一次建全 event0~7，
# key.c 按 EVIOCGNAME=i2cKEY 识别，多余的节点不影响
i=0
while [ $i -le 7 ]; do
    [ -c /dev/input/event$i ] || { mkdir -p /dev/input; mknod /dev/input/event$i c 13 $((64+i)) 2>/dev/null; }
    i=$((i+1))
done
chmod 666 /dev/zlg72128-0 /dev/beep /dev/servo /dev/rfid_module0 /dev/led 2>/dev/null
chmod 666 /dev/input/event* 2>/dev/null

echo "drivers ready:"
ls /dev/zlg72128-0 /dev/rfid_module0 /dev/beep /dev/servo /dev/led 2>/dev/null
