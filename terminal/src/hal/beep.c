#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"

/*
 * FS6818 蜂鸣器（红圈，接在 GPIOC14），走的驱动是改造版 beep_driver.ko：
 *
 *   设备节点：/dev/beep
 *   命令码（魔数 'B'）：BEEP_ON  _IO('B',0)   响
 *                      BEEP_OFF _IO('B',1)   停
 *                      SET_FREQUENCY _IOW('B',2,int)  有源蜂鸣器，保留但空实现
 *
 * 为什么用 /dev/beep 而不是 /dev/pwm：
 *   红圈是**有源**蜂鸣器 —— 引脚给高电平就响、给低电平就静音，不需要方波。
 *   课时 15 的 fs6818_pwm.ko 走 PWM 通道，有两个坑：
 *     ① 定时器是整芯片共享的，课后实验常把定时器留在运行状态，
 *        而驱动的 PWM_OFF 只清 TCON 的 [15:12] 位，未必真的停得掉；
 *     ② 定时器停住时 PWM 输出脚会保持停住那一刻的电平，若是高电平，
 *        有源蜂鸣器就会长鸣（实测用 devmem 把 TCON 清零后依然响，就是这个原因）。
 *   改用 GPIO 驱动后，引脚电平由我们说一不二：高=响、低=静，彻底可控。
 *
 * /dev/pwm 分支保留为兜底（万一板上只加载了 PWM 驱动）。
 */
#define BEEP_ON   _IO('B', 0)
#define BEEP_OFF  _IO('B', 1)
#define BEEP_SET_FREQUENCY _IOW('B', 2, int)   /* 兼容用，改造版驱动里是空实现 */

#define PWM_ON    _IO('K', 0)
#define PWM_OFF   _IO('K', 1)
#define PWM_SET_CNT _IOW('K', 3, int)

/* 兜底走 PWM 时的音调计数值（1~1000，越小越尖） */
#define BUZZER_PWM_CNT 200

/* 兜底走 GPIO 蜂鸣器驱动时的翻转半周期（ms），2 → 约 250Hz */
#define BEEP_DEFAULT_HALF_PERIOD_MS 2

static int g_pwm_mode = 0;   /* 1 = /dev/pwm 兜底模式；0 = /dev/beep（正常） */

int hal_beep_open(void) {
    int fd = open("/dev/beep", O_RDWR);          /* 首选：GPIO 直控，电平可控 */
    if (fd >= 0) {
        int half_period = BEEP_DEFAULT_HALF_PERIOD_MS;
        g_pwm_mode = 0;
        ioctl(fd, BEEP_SET_FREQUENCY, &half_period);   /* 改造版驱动忽略，兼容原版 */
        ioctl(fd, BEEP_OFF);                           /* 打开即静音 */
        return fd;
    }
    perror("open /dev/beep (insmod beep_driver.ko first)");

    fd = open("/dev/pwm", O_RDWR);               /* 兜底 */
    if (fd >= 0) {
        int cnt = BUZZER_PWM_CNT;
        g_pwm_mode = 1;
        ioctl(fd, PWM_SET_CNT, &cnt);
        ioctl(fd, PWM_OFF);
        return fd;
    }
    perror("open /dev/pwm (insmod fs6818_pwm.ko first)");
    return -1;
}

/* 运行中改兜底 PWM 模式的音调 */
void hal_beep_set_tonepwm(int fd, int cnt) {
    if (fd < 0 || !g_pwm_mode) return;
    if (cnt < 1) cnt = 1;
    if (cnt > 1000) cnt = 1000;
    ioctl(fd, PWM_SET_CNT, &cnt);
}

/* 运行中改兜底 GPIO 模式的翻转半周期 */
void hal_beep_set_tone(int fd, int half_ms) {
    if (fd < 0 || g_pwm_mode) return;
    if (half_ms < 1) half_ms = 1;
    if (half_ms > 100) half_ms = 100;
    ioctl(fd, BEEP_SET_FREQUENCY, &half_ms);
}

void hal_beep_on(int fd)  { ioctl(fd, g_pwm_mode ? PWM_ON  : BEEP_ON);  }
void hal_beep_off(int fd) { ioctl(fd, g_pwm_mode ? PWM_OFF : BEEP_OFF); }
