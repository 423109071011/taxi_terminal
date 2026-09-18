#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"

/*
 * FS6818 数码管实际驱动 = key-zlg72128.ko（misc 设备 /dev/zlg72128-0）。
 * ioctl DISP_NUM 语义（2026-09-18 disp_test 板端实测确认）：
 *   - 参数是段码，原样写入芯片显示缓冲；
 *   - 新值进入最左位，旧内容整体右移一位（滚动效果）。
 * 段码表：0=0x3F 1=0x06 2=0x5B 3=0x4F 4=0x66 5=0x6D 6=0x7D 7=0x07
 *         8=0x7F 9=0x6F A=0x77 B=0x7C C=0x39 D=0x5E E=0x79 F=0x71
 *         '-'=0x40 ' '=0x00
 *
 * hal_display_char    ：只送一个字符（键盘录入用，新数字从最左位滚入）
 * hal_display_string  ：整串渲染，从右往左逐字送入，
 *                       最终从左到右恰好显示该串（旧内容被推出屏幕右侧）
 * 注意：驱动 DISP_NUM 一旦最左位为 0 就不再移位，因此无法用送 0 的方式
 *       清屏——靠新字符串把旧内容"推"出去。
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
        case 'F': return 0x71; case '-': return 0x40;
        /* 扩展字母（7段数码管惯用近似字形），供状态词显示 */
        case 'G': return 0x3D; case 'H': return 0x76; case 'I': return 0x30;
        case 'J': return 0x1E; case 'L': return 0x38; case 'N': return 0x54;
        case 'O': return 0x3F; case 'P': return 0x73; case 'S': return 0x6D;
        case 'T': return 0x78; case 'U': return 0x3E; case 'Y': return 0x6E;
        default:  return -1;
    }
}

static int feed_seg(int seg) {
    return ioctl(fd_zlg, ZLG72128_DIGITRON_DISP_NUM, seg) < 0 ? -1 : 0;
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
    /* 送一段非0再送0，尽量把旧内容推出；驱动限制下无法完全清屏 */
    for (int i = 0; i < 8; i++) feed_seg(0x00);
    return 0;
}

int hal_display_char(char c) {
    int seg;
    if (fd_zlg < 0) return -1;
    if (c >= 'a' && c <= 'z') c -= 32;
    seg = seg_code(c);
    if (seg < 0) return 0;   /* 不可显示字符：跳过 */
    return feed_seg(seg);
}

int hal_display_string(int fd, const char *s) {
    int n = 0, i;
    int segs[8];
    (void)fd;
    if (fd_zlg < 0) return -1;
    /* 从右往左逐字送入：最后送的是 s[0]，落在最左位 */
    for (const char *p = s + strlen(s) - 1; p >= s && n < 8; p--) {
        char c = *p;
        int seg;
        if (c >= 'a' && c <= 'z') c -= 32;
        seg = seg_code(c);
        if (seg < 0) continue;          /* 7段码显示不了的字符跳过 */
        segs[n++] = seg;
    }
    for (i = n - 1; i >= 0; i--) feed_seg(segs[i]);
    return 0;
}

int hal_display_number(int fd, long v, int width) {
    (void)width;
    char b[16];
    snprintf(b, sizeof(b), "%ld", v);
    return hal_display_string(fd, b);
}
