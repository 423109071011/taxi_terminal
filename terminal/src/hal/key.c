#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include "hal.h"
int hal_key_open(void) {
    int fd = open("/dev/input/event4", O_RDONLY);
    if (fd < 0) perror("open /dev/input/event4");
    return fd;
}
int hal_key_read(int fd, unsigned int *code) {
    struct input_event ev;
    for (;;) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != (ssize_t)sizeof(ev)) continue;
        if (ev.type == EV_KEY) { *code = ev.code; return ev.value ? 1 : 0; }
    }
}
