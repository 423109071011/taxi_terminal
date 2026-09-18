#include "keymap.h"
/*
 * FS6818 实际键盘驱动 = key-zlg72128.ko（input 事件型，i2cKEY）。
 * 键值来源：2026-09-18 板端实测标定（taxi_terminal [KEY] raw 打印）：
 *   '0'-'9' -> 3, 27, 26, 25, 19, 18, 17, 12, 11, 10
 *   'A'=24(前翻页)  'B'=16(后翻页)  'C'=9(关车门，板端验证 [DOOR] close 生效)
 *   '#'=2(确认)  '*'(OK)=4(确认)  ——沿用厂商表，未实测
 *   'D' 码值未知，未绑定功能（预留）
 * 注意：厂商 zlg72128_key_test.c 的数字表（3,253,27,26,20,...）与实际 input
 * 上报值不符，勿再用；其 25/17 实为数字 3/6。
 */
int key_map(unsigned int code, char *digit) {
    static const unsigned int num_code[10] = {3, 27, 26, 25, 19, 18, 17, 12, 11, 10};
    for (int i = 0; i < 10; i++) {
        if (code == num_code[i]) { *digit = (char)('0' + i); return K_DIGIT; }
    }
    if (code == 2)  return K_CONFIRM;   /* '#'  确认上报 */
    if (code == 9)  return K_CLOSE;     /* 'C'  关车门（实测验证） */
    if (code == 4)  return K_CONFIRM;   /* '*'  也当确认 */
    if (code == 24) return K_PREV;      /* 'A'  前翻页（实测标定） */
    if (code == 16) return K_NEXT;      /* 'B'  后翻页（实测标定） */
    return K_NONE;
}
