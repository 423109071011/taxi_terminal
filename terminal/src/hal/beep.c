#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "hal.h"
#define BEEP_ON  _IO('B', 0)
#define BEEP_OFF _IO('B', 1)
int hal_beep_open(void) {
    int fd = open("/dev/beep", O_RDWR);
    if (fd < 0) perror("open beep");
    return fd;
}
void hal_beep_on(int fd)  { ioctl(fd, BEEP_ON); }
void hal_beep_off(int fd) { ioctl(fd, BEEP_OFF); }
