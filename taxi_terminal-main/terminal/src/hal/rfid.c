#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "hal.h"
int hal_rfid_open(void) {
    int fd = open("/dev/rfid_module0", O_RDWR);
    if (fd < 0) perror("open rfid");
    return fd;
}
int hal_rfid_read(int fd, unsigned char card[4]) {
    unsigned char b[4] = {0};
    ssize_t n = read(fd, b, 4);
    if (n == 4) { for (int i = 0; i < 4; i++) card[i] = b[i]; return 1; }
    return (n < 0) ? -1 : 0;
}
