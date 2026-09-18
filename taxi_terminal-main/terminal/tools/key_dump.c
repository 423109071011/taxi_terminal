#include <stdio.h>
#include "hal.h"
#include "keymap.h"
int main(void) {
    int fd = hal_key_open();
    if (fd < 0) return 1;
    printf("press keys; Ctrl-C to quit\n");
    while (1) {
        unsigned int code = 0; char d = 0;
        int v = hal_key_read(fd, &code);
        if (v == 1) {
            int a = key_map(code, &d);
            printf("code=0x%02x(%u) action=%d digit=%c\n", code, code, a, d ? d : '?');
        }
    }
    return 0;
}
