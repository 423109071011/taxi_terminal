#include "keymap.h"
/* 依据驱动 key_list_def：K2..K11 -> KEY_1..KEY_0（code 2..11）。
 * 确认/关门/翻页键为占位默认值，需用 tools/key_dump 在真机标定后改这里。 */
int key_map(unsigned int code, char *digit) {
    if (code >= 2 && code <= 10) { *digit = (char)('1' + (code - 2)); return K_DIGIT; }
    if (code == 11) { *digit = '0'; return K_DIGIT; }
    if (code == 28) return K_CONFIRM;   /* KEY_ENTER，待标定 */
    if (code == 29) return K_CLOSE;     /* KEY_LEFTCTRL=K1，待标定 */
    if (code == 103) return K_PREV;     /* KEY_UP，待标定 */
    if (code == 108) return K_NEXT;     /* KEY_DOWN，待标定 */
    return K_NONE;
}
