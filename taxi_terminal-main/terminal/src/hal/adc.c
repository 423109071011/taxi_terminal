#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"

/*
 * ADC 采集（FS6818 adc_driver.ko，misc 设备 /dev/adc）：
 *   ioctl(fd, SET_CHANNEL=_IO('A',0), 通道) 选通道，read(fd,&v,4) 得 12 位值(0~4095)。
 * 通道定义（adc_driver.h，FS6818M4 配置）：酒精=7 光敏=6 烟雾=5 火焰=6/1。
 * 出租车终端用烟雾传感器（SMOKE_CHANNEL=5）做车内气体超标检测。
 */
#define ADC_SET_CHANNEL     _IO('A', 0)
#define SMOKE_CHANNEL       5

int hal_adc_open(void) {
    int fd = open("/dev/adc", O_RDWR);
    if (fd < 0) {
        perror("open /dev/adc (insmod adc_driver.ko first)");
        return -1;
    }
    ioctl(fd, ADC_SET_CHANNEL, SMOKE_CHANNEL);
    return fd;
}

int hal_adc_read(int fd) {
    int v = -1;
    if (fd < 0) return -1;
    if (read(fd, &v, sizeof(v)) != (int)sizeof(v)) return -1;
    return v;
}
