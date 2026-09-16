#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"

/*
 * FS6818 蜂鸣器 = 课时 15 的 PWM 驱动（fs6818_pwm.ko）
 *   设备节点：/dev/pwm（驱动自动创建）
 *   命令码：  PWM_ON  _IO('K',0)   启动 PWM 输出（响）
 *             PWM_OFF _IO('K',1)   停止输出（停）
 *             SET_PRE _IOW('K',2,int)  预分频
 *             SET_CNT _IOW('K',3,int)  计数值（决定音调）
 * 用法与课时 7 的 pwm_music 测试程序一致。
 */
#define PWM_ON  _IO('K', 0)
#define PWM_OFF _IO('K', 1)
#define SET_PRE _IOW('K', 2, int)
#define SET_CNT _IOW('K', 3, int)

/* PCLK=0x4200000, div=(PCLK/256/4)/音调Hz；这里取约 1kHz 的报警音 */
#define BEEP_DIV_DEFAULT 66   /* 67584/1000 ≈ 67 */

int hal_beep_open(void) {
    int fd = open("/dev/pwm", O_RDWR);
    int pre = 255, div = BEEP_DIV_DEFAULT;
    if (fd < 0) { perror("open /dev/pwm"); return fd; }
    ioctl(fd, SET_PRE, &pre);   /* 与 pwm_music 相同的预分频 */
    ioctl(fd, SET_CNT, &div);   /* 设定报警音音调 */
    ioctl(fd, PWM_OFF);         /* 关键：清掉之前实验残留的 PWM 启动状态，保证开机静音 */
    return fd;
}
void hal_beep_on(int fd)  { ioctl(fd, PWM_ON); }
void hal_beep_off(int fd) { ioctl(fd, PWM_OFF); }
