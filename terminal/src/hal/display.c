#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"

/*
 * FS6818 数码管实际驱动 = key-zlg72128.ko（misc 设备 /dev/zlg72128-0）。
 * 本仓库 board_driver/zlg72128/ 为增强版驱动，在原厂基础上实现了
 * DISPBUF_SET（ioctl 'F'/4）：应用层传 8 字节段码指针，整屏直写，
 * 不经过 DISP_NUM 的移位逻辑——旧版驱动最左位为 0 后不再移位，
 * 短字符串右侧旧内容永远残留（表现为乱码），无法清屏。
 *
 * 段码表：0=0x3F 1=0x06 2=0x5B 3=0x4F 4=0x66 5=0x6D 6=0x7D 7=0x07
 *         8=0x7F 9=0x6F A=0x77 B=0x7C C=0x39 D=0x5E E=0x79 F=0x71
 *         '-'=0x40 灭=0x00
 *
 * hal_display_char    ：单字符滚入最左位（键盘录入，用 DISP_NUM）
 * hal_display_string  ：整串左对齐渲染，右侧全灭（用 DISPBUF_SET，
 *                       旧驱动回退为移位推入法，右侧可能有残留）
 * hal_display_clear   ：全灭（DISPBUF_SET 真·清屏；旧驱动尽力推 0）
 */
#define ZLG72128_MAGIC                  'F'
#define ZLG72128_DIGITRON_DISP_NUM      _IOC(_IOC_WRITE, ZLG72128_MAGIC, 3, 0)
#define ZLG72128_DIGITRON_DISPBUF_SET   _IOC(_IOC_WRITE, ZLG72128_MAGIC, 4, 0)

static int fd_zlg = -1;
static int has_buf_set = -1;    /* -1 未探测 / 1 支持 / 0 不支持(旧驱动) */

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

/* 整屏直写：buf[0]=最左位 ... buf[7]=最右位。返回 0 成功，-1 不支持 */
static int disp_buf_set(const unsigned char buf[8]) {
    if (fd_zlg < 0) return -1;
    if (has_buf_set == 0) return -1;
    if (ioctl(fd_zlg, ZLG72128_DIGITRON_DISPBUF_SET, buf) < 0) {
        has_buf_set = 0;                /* 旧驱动：无此命令 */
        return -1;
    }
    has_buf_set = 1;
    return 0;
}

int hal_display_open(void) {
    fd_zlg = open("/dev/zlg72128-0", O_RDWR);
    if (fd_zlg < 0) {
        perror("open /dev/zlg72128-0 (insmod key-zlg72128.ko first)");
        return -1;
    }
    has_buf_set = -1;
    return 0;
}

int hal_display_clear(int fd) {
    unsigned char buf[8];
    (void)fd;
    if (fd_zlg < 0) return -1;
    memset(buf, 0x00, sizeof(buf));
    if (disp_buf_set(buf) == 0) return 0;
    /* 旧驱动回退：送 0 尽量清最左位（右侧可能残留，属已知驱动限制） */
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
    unsigned char buf[8];
    int n = 0;
    (void)fd;
    if (fd_zlg < 0) return -1;
    memset(buf, 0x00, sizeof(buf));
    /* 左对齐填入可显字符，右侧全灭 */
    for (const char *p = s; *p && n < 8; p++) {
        char c = *p;
        int seg;
        if (c >= 'a' && c <= 'z') c -= 32;
        seg = seg_code(c);
        if (seg < 0) continue;          /* 7段码显示不了的字符跳过 */
        buf[n++] = (unsigned char)seg;
    }
    if (disp_buf_set(buf) == 0) return 0;
    /* 旧驱动回退：从右往左移位推入（短串右侧会残留旧内容） */
    for (int i = n - 1; i >= 0; i--) feed_seg(buf[i]);
    return 0;
}

int hal_display_number(int fd, long v, int width) {
    (void)width;
    char b[16];
    snprintf(b, sizeof(b), "%ld", v);
    return hal_display_string(fd, b);
}
