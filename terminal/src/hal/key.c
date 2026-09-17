#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include "hal.h"

/*
 * FS6818 键盘实际驱动 = key-zlg72128.ko（input 事件型）：
 *   insmod 后注册 input 设备 "i2cKEY"（板上是 /dev/input/event4，但不写死）。
 *   本 HAL 启动时扫描 /dev/input/event0~9，用 EVIOCGNAME 找 "i2cKEY"；
 *   找不到再依次尝试 event4/event0。键值直接用 ev.code
 *   （键值矩阵见 keymap.c，与 zlg72128_key_test.c 一致）。
 */
static int open_named_input(const char *want) {
    char path[64];
    char name[256];
    for (int i = 0; i < 10; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
            strstr(name, want) != NULL) {
            printf("key: using %s (%s)\n", path, name);
            return fd;
        }
        close(fd);
    }
    return -1;
}

int hal_key_open(void) {
    int fd = open_named_input("i2cKEY");
    if (fd >= 0) return fd;
    /* 找不到按名字的设备时，退回常见节点 */
    const char *fb[] = {"/dev/input/event4", "/dev/input/event0", NULL};
    for (int i = 0; fb[i]; i++) {
        fd = open(fb[i], O_RDONLY);
        if (fd >= 0) { printf("key: using %s (fallback)\n", fb[i]); return fd; }
    }
    perror("open key input device");
    return -1;
}

int hal_key_read(int fd, unsigned int *code) {
    struct input_event ev;
    for (;;) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != (ssize_t)sizeof(ev)) continue;
        if (ev.type == EV_KEY && ev.value == 1) {   /* 仅按下沿 */
            *code = ev.code;
            return 1;
        }
    }
}
