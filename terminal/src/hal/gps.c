#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include "hal.h"

/*
 * EC20 的 GNSS 引擎出厂默认关闭：必须先在 AT 口(ttyUSB2)发 AT+QGPS=1，
 * NMEA 口(ttyUSB1)才会有数据流，否则永远静默、sats 恒为 0。
 * 重复开启模块回 ERROR，无害；模块不在时静默返回。
 */
static void ec20_gps_enable(void) {
    int fd = open("/dev/ttyUSB2", O_RDWR | O_NOCTTY);
    if (fd < 0) return;                    /* 模块未接，静默 */
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        cfsetispeed(&t, B115200);
        cfsetospeed(&t, B115200);
        t.c_cflag |= CLOCAL | CREAD;
        tcsetattr(fd, TCSANOW, &t);
    }
    if (write(fd, "AT+QGPS=1\r", 10) < 0) { close(fd); return; }
    usleep(500000);                        /* 等模块应答 */
    char rsp[64] = {0};
    read(fd, rsp, sizeof(rsp) - 1);
    printf("[gps] AT+QGPS=1 -> %s\n",
           strstr(rsp, "OK")    ? "GNSS started" :
           strstr(rsp, "ERROR") ? "already on"   : "no ack (continuing)");
    close(fd);
    sleep(1);                              /* 引擎启动缓冲 */
}

int hal_gps_open(void) {
    ec20_gps_enable();
    int fd = open("/dev/ttyUSB1", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) printf("gps: not available (EC20 not attached), continue without GPS\n");
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

/* 提取一条 NMEA 语句（$ 开头到 \n），talker 兼容 GP/GN/BD */
static int take_sentence(const char *buf, const char *type, char *line, int cap) {
    const char *p = buf;
    while ((p = strstr(p, type)) != NULL) {
        const char *s = p;
        while (s > buf && *(s - 1) != '\n') s--;        /* 回退到行首 */
        if (*s == '$') {
            const char *eol = strchr(s, '\n');
            int n = eol ? (int)(eol - s) : (int)strlen(s);
            if (n < cap) { memcpy(line, s, n); line[n] = '\0'; return 1; }
        }
        p += strlen(type);
    }
    return 0;
}

/* ddmm.mmmm -> 度（NMEA 坐标格式） */
static double nmea_to_deg(const char *s) {
    if (!s[0]) return 0.0;
    double v = atof(s);
    int d = (int)(v / 100.0);
    return d + (v - d * 100.0) / 60.0;
}

/*
 * GGA: $GPGGA,hhmmss,llll.lll,a,yyyyy.yyy,a,x,xx,x.x,... (f1时间 f2纬 f4经 f6质量 f7卫星数)
 * RMC: $GPRMC,hhmmss,A|V,llll.lll,a,yyyyy.yyy,a,速度(节),航向,ddmmyy,...
 *      f7=地面速度（节）→ ×1.852 = km/h，填 0x0200 速度字段
 * 均为 UTC。返回 1 = 有效定位。
 */
int hal_gps_parse(const char *buf, int len, gps_fix_t *fix) {
    char line[160];
    char f[16][32];
    (void)len;

    /* --- RMC：补充日期 --- */
    if (take_sentence(buf, "RMC", line, sizeof(line))) {
        memset(f, 0, sizeof(f));
        split(f, line);
        if (strlen(f[9]) == 6) {
            fix->day   = (f[9][0]-'0')*10 + (f[9][1]-'0');
            fix->month = (f[9][2]-'0')*10 + (f[9][3]-'0');
            fix->year  = 2000 + (f[9][4]-'0')*10 + (f[9][5]-'0');
        }
        /* 地面速度：节 -> km/h（静止时 RMC 该字段为空，保持 0） */
        if (f[2][0] == 'A' && f[7][0])
            fix->speed_kmh = atof(f[7]) * 1.852;
        else
            fix->speed_kmh = 0.0;
    }

    /* --- GGA：定位与时间 --- */
    if (!take_sentence(buf, "GGA", line, sizeof(line))) return 0;
    memset(f, 0, sizeof(f));
    split(f, line);
    int status = atoi(f[6]);
    if (status == 0) return 0;
    fix->status = status;
    fix->lat = nmea_to_deg(f[2]);
    fix->lon = nmea_to_deg(f[4]);
    if (strlen(f[1]) >= 6) {
        fix->hour = (f[1][0]-'0')*10 + (f[1][1]-'0');
        fix->min  = (f[1][2]-'0')*10 + (f[1][3]-'0');
        fix->sec  = (f[1][4]-'0')*10 + (f[1][5]-'0');
    }
    fix->sats = atoi(f[7]);
    return 1;
}
