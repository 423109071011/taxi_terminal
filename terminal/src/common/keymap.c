#include "keymap.h"
/*
 * FS6818 实际键盘驱动 = key-zlg72128.ko（input 事件型，i2cKEY）。
 * 键值来源：2026-09-18 板端实测标定（taxi_terminal [KEY] raw 打印，0-9 逐键扫描）：
 *   '0'-'9' -> 3, 27, 26, 25, 19, 18, 17, 12, 11, 10
 * 注意：厂商 zlg72128_key_test.c 的表（3,253,27,26,20,...）与实际 input
 * 上报值不符，勿再用；25/17 实为数字 3/6。
 * 功能键（#/A/B/C/*）实测值待标定，暂沿用旧值：
 *   '#'=2(确认)  'C'=9(关门)  '*'(OK)=4(确认)
 */
int key_map(unsigned int code, char *digit) {
    static const unsigned int num_code[10] = {3, 27, 26, 25, 19, 18, 17, 12, 11, 10};
    for (int i = 0; i < 10; i++) {
        if (code == num_code[i]) { *digit = (char)('0' + i); return K_DIGIT; }
    }
    if (code == 2)  return K_CONFIRM;   /* '#'  确认上报（待标定） */
    if (code == 9)  return K_CLOSE;     /* 'C'  关车门（待标定） */
    if (code == 4)  return K_CONFIRM;   /* '*'  也当确认（待标定） */
    return K_NONE;
}
