#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"
#define LED_ON  _IOW('L', 0, int)
#define LED_OFF _IOW('L', 1, int)
int hal_led_open(void) {
    int fd = open("/dev/led", O_RDWR);
    if (fd < 0) perror("open led");
    return fd;
}
void hal_led_set(int fd, char color, int on) {
    int c = color;
    ioctl(fd, on ? LED_ON : LED_OFF, &c);
}
