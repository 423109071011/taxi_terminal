#include <stdio.h>
#include <assert.h>
#include "keymap.h"
/* 2026-09-18 板端实测标定：'0'-'9'->3,27,26,25,19,18,17,12,11,10
 * 功能键待标定，暂 '#'=2 'C'=9 '*'=4 */
int main(void) {
    char d = 0;
    assert(key_map(3,  &d) == K_DIGIT && d == '0');
    assert(key_map(27, &d) == K_DIGIT && d == '1');
    assert(key_map(26, &d) == K_DIGIT && d == '2');
    assert(key_map(25, &d) == K_DIGIT && d == '3');
    assert(key_map(19, &d) == K_DIGIT && d == '4');
    assert(key_map(18, &d) == K_DIGIT && d == '5');
    assert(key_map(17, &d) == K_DIGIT && d == '6');
    assert(key_map(12, &d) == K_DIGIT && d == '7');
    assert(key_map(11, &d) == K_DIGIT && d == '8');
    assert(key_map(10, &d) == K_DIGIT && d == '9');
    assert(key_map(2,  &d) == K_CONFIRM);
    assert(key_map(9,  &d) == K_CLOSE);
    assert(key_map(4,  &d) == K_CONFIRM);
    assert(key_map(999, &d) == K_NONE);
    printf("test_keymap PASS\n");
    return 0;
}
