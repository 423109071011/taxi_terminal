#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* 数码管写入实验工具：确认 ZLG72128 DISP_NUM 的参数语义
 * 用法: disp_test <hex-value>
 *   disp_test 3F   段码'0'   disp_test 06  段码'1'
 *   disp_test 0    数字值0   disp_test 1   数字值1
 */
#define ZLG72128_MAGIC                  'F'
#define ZLG72128_DIGITRON_DISP_NUM      _IOC(_IOC_WRITE, ZLG72128_MAGIC, 3, 0)

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: %s <hex-value>  (try: 3F 06 5B 4F 0 1 2)\n", argv[0]); return 1; }
    int fd = open("/dev/zlg72128-0", O_RDWR);
    if (fd < 0) { perror("open /dev/zlg72128-0"); return 1; }
    unsigned long v = strtoul(argv[1], NULL, 16);
    int ret = ioctl(fd, ZLG72128_DIGITRON_DISP_NUM, v);
    printf("DISP_NUM value=0x%02lX ret=%d (watch digitron 3s)\n", v, ret);
    sleep(3);
    close(fd);
    return 0;
}
