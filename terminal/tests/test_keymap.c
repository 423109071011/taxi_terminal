#include <stdio.h>
#include <assert.h>
#include "keymap.h"
int main(void) {
    char d = 0;
    assert(key_map(2,  &d) == K_DIGIT && d == '1');
    assert(key_map(11, &d) == K_DIGIT && d == '0');
    assert(key_map(28, &d) == K_CONFIRM);
    assert(key_map(29, &d) == K_CLOSE);
    assert(key_map(999, &d) == K_NONE);
    printf("test_keymap PASS\n");
    return 0;
}
