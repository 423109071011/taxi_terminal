#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "hal.h"

int hal_net_connect(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    if (fd < 0) return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
    return fd;
}
int hal_net_send(int fd, const unsigned char *b, int n) {
    int t = 0;
    while (t < n) {
        int r = send(fd, b + t, n - t, 0);
        if (r <= 0) return -1;
        t += r;
    }
    return t;
}
int hal_net_recv(int fd, unsigned char *b, int cap) {
    int r = recv(fd, b, cap, 0);
    return r;   /* >0 字节数, 0 对端关闭, -1 错误 */
}
