#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "hal.h"

/* ZLG72128 段码：'0'~'9'、空格、'-'（与 zlg7290 驱动同表） */
static const unsigned char seg_code[128] = {0};
static unsigned char seg(char c) {
    switch (c) {
        case '0': return 0xfc; case '1': return 0x60; case '2': return 0xda;
        case '3': return 0xf2; case '4': return 0x66; case '5': return 0xb6;
        case '6': return 0xbe; case '7': return 0xe0; case '8': return 0xfe;
        case '9': return 0xf6; case '-': return 0x02; case ' ': return 0x00;
        case 'A': return 0xee; case 'b': return 0x3e; case 'C': return 0x9c;
        case 'd': return 0x7a; case 'E': return 0x9e; case 'F': return 0x8e;
        default:  return 0x00;
    }
}

static int fd_i2c = -1;
static int fd_misc = -1;

int hal_display_open(void) {
    fd_i2c = open("/dev/i2c-2", O_RDWR);
    if (fd_i2c >= 0) {
        if (ioctl(fd_i2c, I2C_SLAVE, 0x30) < 0) { close(fd_i2c); fd_i2c = -1; }
    }
    if (fd_i2c < 0) fd_misc = open("/dev/zlg72128-0", O_RDWR);
    return (fd_i2c >= 0 || fd_misc >= 0) ? 0 : -1;
}

static int i2c_write_reg(int reg, unsigned char v) {
    unsigned char buf[2] = { reg, v };
    return write(fd_i2c, buf, 2) == 2 ? 0 : -1;
}

int hal_display_clear(int fd) {
    (void)fd;
    if (fd_i2c >= 0) {
        for (int i = 0; i < 12; i++) i2c_write_reg(0x10 | i, 0x00);
        return 0;
    }
    if (fd_misc >= 0) { for (int i = 0; i < 12; i++) ioctl(fd_misc, _IOW('F', 3, 0), 0x00); }
    return 0;
}

int hal_display_string(int fd, const char *s) {
    (void)fd;
    int len = strlen(s);
    if (len > 12) len = 12;
    if (fd_i2c >= 0) {
        hal_display_clear(0);
        for (int i = 0; i < len; i++) i2c_write_reg(0x10 | i, seg(s[i]));
        return 0;
    }
    if (fd_misc >= 0) {
        for (int i = 0; i < len; i++) ioctl(fd_misc, _IOW('F', 3, 0), seg(s[i]));
        return 0;
    }
    return -1;
}

int hal_display_number(int fd, long v, int width) {
    char b[16];
    snprintf(b, sizeof(b), "%*ld", width, v);
    return hal_display_string(fd, b);
}
