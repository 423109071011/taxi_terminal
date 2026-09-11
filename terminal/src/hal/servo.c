#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "hal.h"
#define SET_ANGLE _IO('S', 5)
int hal_servo_open(void) {
    int fd = open("/dev/servo", O_RDWR);
    if (fd < 0) perror("open servo");
    return fd;
}
void hal_servo_angle(int fd, int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    ioctl(fd, SET_ANGLE, angle);
}
