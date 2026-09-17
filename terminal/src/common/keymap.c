#include "keymap.h"
/*
 * FS6818 实际键盘驱动 = key-zlg72128.ko（input 事件型，i2cKEY）。
 * 键值来源：sources/19.zlg72128/app/zlg72128_key_test.c（权威对照）：
 *   '0'-'9' -> 3, 253, 27, 26, 20, 19, 18, 12, 11, 10
 *   '#'=2(确认)  'C'=9  'A'=25  'B'=17  'D'=29  '*'(OK)=4
 * 注意：这与 zlg7290.ko 的矩阵不同（7290 的 '1'=28），勿混用。
 * 若按键与预期不符，运行 tools/key_dump 查看实际键值后调整此处。
 */
int key_map(unsigned int code, char *digit) {
    static const unsigned int num_code[10] = {3, 253, 27, 26, 20, 19, 18, 12, 11, 10};
    for (int i = 0; i < 10; i++) {
        if (code == num_code[i]) { *digit = (char)('0' + i); return K_DIGIT; }
    }
    if (code == 2)  return K_CONFIRM;   /* '#'  确认上报 */
    if (code == 9)  return K_CLOSE;     /* 'C'  关车门 */
    if (code == 25) return K_PREV;      /* 'A'  前翻页 */
    if (code == 17) return K_NEXT;      /* 'B'  后翻页 */
    if (code == 4)  return K_CONFIRM;   /* '*'  也当确认 */
    return K_NONE;
}
