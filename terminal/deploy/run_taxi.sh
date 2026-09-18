#!/bin/sh
# 安全启动 taxi_terminal —— 规避 NFS 上可执行文件被覆盖导致的内核 Oops
#
# 背景：板子根文件系统在 NFS 上。若在程序运行时重新部署覆盖了同一路径的
# 可执行文件，旧的进程退出时会走 removed_exe_file_vma → nfs_release，
# 触发 Linux 3.4.39 NFS 客户端的空指针解引用内核崩溃（需重启）。
# 本脚本每次先把程序复制成一个本次运行专用的临时副本再 exec，
# 重新部署只会影响 /arduino_drivers/taxi_terminal 本体，
# 不会影响已经在跑的那个副本，因此随时 Ctrl+C 都不会崩。
#
# 用法： /arduino_drivers/run_taxi.sh [配置文件路径]

SRC=/arduino_drivers/taxi_terminal
TMPDIR=${TMPDIR:-/tmp}
DST="$TMPDIR/.taxi_run_$$"

[ -d "$TMPDIR" ] || TMPDIR=/var/tmp
[ -d "$TMPDIR" ] || TMPDIR=/
DST="$TMPDIR/.taxi_run_$$"

# 清理很久以前的陈旧副本（超过 3 小时，正常情况下那时进程早已退出）
find "$TMPDIR" -maxdepth 1 -name '.taxi_run_*' -type f -mmin +180 \
     -exec rm -f {} + 2>/dev/null

if cp "$SRC" "$DST" 2>/dev/null; then
    chmod +x "$DST"
    echo "[run_taxi] running private copy $DST"
    exec "$DST" "$@"
else
    echo "[run_taxi] copy failed, launching directly from $SRC"
    exec "$SRC" "$@"
fi
