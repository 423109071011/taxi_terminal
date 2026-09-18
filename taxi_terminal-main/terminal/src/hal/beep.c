#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"

/*
 * FS6818 蜂鸣器 = 课时 7 蜂鸣器驱动（beep_driver.ko，misc 设备）
 *   设备节点：/dev/beep（驱动源码 beep_driver.c：DEVICE_NAME "beep"）
 *   命令码：  BEEP_ON  _IO('B',0)   响
 *             BEEP_OFF _IO('B',1)   停
 * 若板上加载的是课时 7 的 PWM 方式驱动（/dev/pwm，'K'魔数），自动回退使用。
 */
#define BEEP_ON   _IO('B', 0)
#define BEEP_OFF  _IO('B', 1)
#define PWM_ON    _IO('K', 0)
#define PWM_OFF   _IO('K', 1)

static int g_pwm_mode = 0;   /* 1 = /dev/pwm 回退模式 */

int hal_beep_open(void) {
    int fd = open("/dev/beep", O_RDWR);
    if (fd >= 0) { g_pwm_mode = 0; return fd; }
    perror("open /dev/beep (insmod beep_driver.ko first)");
    fd = open("/dev/pwm", O_RDWR);
    if (fd >= 0) { g_pwm_mode = 1; return fd; }
    perror("open /dev/pwm");
    return -1;
}
void hal_beep_on(int fd)  { ioctl(fd, g_pwm_mode ? PWM_ON  : BEEP_ON);  }
void hal_beep_off(int fd) { ioctl(fd, g_pwm_mode ? PWM_OFF : BEEP_OFF); }
