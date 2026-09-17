#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"

/*
 * FS6818 数码管实际驱动 = key-zlg72128.ko（misc 设备 /dev/zlg72128-0）。
 * 官方测试 zlg72128_key_test.c 的用法：
 *   ioctl(fd, ZLG72128_DIGITRON_DISP_NUM, 段码)
 * 驱动 ioctl 只实现 DISP_NUM（固定显示在第 1 位），参数为段码：
 *   0=0x3F 1=0x06 2=0x5B 3=0x4F 4=0x66 5=0x6D 6=0x7D 7=0x07
 *   8=0x7F 9=0x6F A=0x77 B=0x7C C=0x39 D=0x5E E=0x79 F=0x71
 *   '-'=0x40 ' '=0x00
 * 应用层 API 保持不变（hal_display_string 显示 8 字符串），
 * 实际硬件只显示第一个可显示字符（驱动限制），其余字符忽略。
 */
#define ZLG72128_MAGIC                  'F'
#define ZLG72128_DIGITRON_DISP_NUM      _IOC(_IOC_WRITE, ZLG72128_MAGIC, 3, 0)

static int fd_zlg = -1;

static int seg_code(char c) {
    switch (c) {
        case '0': return 0x3F; case '1': return 0x06; case '2': return 0x5B;
        case '3': return 0x4F; case '4': return 0x66; case '5': return 0x6D;
        case '6': return 0x7D; case '7': return 0x07; case '8': return 0x7F;
        case '9': return 0x6F; case 'A': return 0x77; case 'B': return 0x7C;
        case 'C': return 0x39; case 'D': return 0x5E; case 'E': return 0x79;
        case 'F': return 0x71; case '-': return 0x40; default:  return 0x00;
    }
}

int hal_display_open(void) {
    fd_zlg = open("/dev/zlg72128-0", O_RDWR);
    if (fd_zlg < 0) {
        perror("open /dev/zlg72128-0 (insmod key-zlg72128.ko first)");
        return -1;
    }
    return 0;
}

int hal_display_clear(int fd) {
    (void)fd;
    if (fd_zlg < 0) return -1;
    ioctl(fd_zlg, ZLG72128_DIGITRON_DISP_NUM, 0x00);
    return 0;
}

int hal_display_string(int fd, const char *s) {
    (void)fd;
    if (fd_zlg < 0) return -1;
    for (; *s; s++) {
        char c = *s;
        if (c >= 'a' && c <= 'f') c -= 32;
        if (strchr("0123456789ABCDEF-", c)) {
            ioctl(fd_zlg, ZLG72128_DIGITRON_DISP_NUM, seg_code(c));
            return 0;
        }
    }
    ioctl(fd_zlg, ZLG72128_DIGITRON_DISP_NUM, 0x00);   /* 无可显示字符=清屏 */
    return 0;
}

int hal_display_number(int fd, long v, int width) {
    (void)width;
    char b[16];
    snprintf(b, sizeof(b), "%ld", v);
    return hal_display_string(fd, b);
}
