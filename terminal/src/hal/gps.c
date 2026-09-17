#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "hal.h"

int hal_gps_open(void) {
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
 * RMC: $GPRMC,hhmmss,A|V,llll.lll,a,yyyyy.yyy,a,x.x,x.x,ddmmyy,... (f9日期)
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
