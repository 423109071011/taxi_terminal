#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "hal.h"

int hal_gps_open(void) {
    int fd = open("/dev/ttyUSB1", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) perror("open gps");
    return fd;
}

static int split(char fields[][32], char *s) {
    int j = 0, k = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == ',') { fields[j][k] = '\0'; j++; k = 0; continue; }
        if (k < 31) fields[j][k++] = s[i];
    }
    fields[j][k] = '\0';
    return j + 1;
}

/* 解析 $GPGGA；返回 1 有效定位 / 0 无效或非 GGA */
int hal_gps_parse(const char *buf, int len, gps_fix_t *fix) {
    const char *gga = strstr(buf, "$GPGGA");
    char line[128]; char f[16][32];
    if (!gga) return 0;
    const char *eol = strchr(gga, '\n');
    if (!eol) return 0;
    int n = (int)(eol - gga);
    if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
    memcpy(line, gga, n); line[n] = '\0';
    memset(f, 0, sizeof(f));
    split(f, line);               /* f[0]="$GPGGA" */
    int status = atoi(f[6]);
    if (status == 0) return 0;
    fix->status = status;
    /* 纬度 ddmm.mmmm -> 度 */
    double lat = atof(f[2]);
    int d = (int)(lat / 100.0);
    fix->lat = d + (lat - d * 100.0) / 60.0;
    double lon = atof(f[4]);
    d = (int)(lon / 100.0);
    fix->lon = d + (lon - d * 100.0) / 60.0;
    /* 时间 hhmmss */
    if (strlen(f[1]) >= 6) {
        fix->hour = (f[1][0]-'0')*10 + (f[1][1]-'0');
        fix->min  = (f[1][2]-'0')*10 + (f[1][3]-'0');
        fix->sec  = (f[1][4]-'0')*10 + (f[1][5]-'0');
    }
    fix->sats = atoi(f[7]);
    (void)len;
    return 1;
}
