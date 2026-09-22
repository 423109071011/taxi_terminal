#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "cmdline.h"
#include "hal.h"
#include "jt808.h"
#include "dispatch.h"
#include "taxi.h"

static void print_help(void) {
    printf("commands:\n");
    printf("  ? / help                 show this help\n");
    printf("  info                     show current sensor values\n");
    printf("  uppath [coord-file]      upload history path; no arg = preset file\n");
    printf("                           fmt A: YYMMDD HH:MM:SS lon lat spd(km/h)\n");
    printf("                           fmt B: lat,lon\n");
}
static void print_info(app_state *st) {
    printf("net_ok=%d door_open=%d verified=%d fatigue=%d\n",
           st->net_ok, st->door_open, st->verified, st->fatigue);
    printf("gps: lat=%.6f lon=%.6f sats=%d status=%d\n",
           st->gps.lat, st->gps.lon, st->gps.sats, st->gps.status);
    printf("card: %02x%02x%02x%02x has_card=%d\n",
           st->card[0], st->card[1], st->card[2], st->card[3], st->has_card);
    printf("smoke: adc=%d alarm=%d\n", st->smoke_val, st->smoke_alarm);
    printf("drive: %ld min (started=%d)\n",
           st->drive_start ? (long)((time(NULL) - st->drive_start) / 60) : 0L,
           st->drive_start ? 1 : 0);
}

/*
 * 历史路径文件格式（自动识别，二选一）：
 *  A) "YYMMDD HH:MM:SS 经度 纬度 速度"（空白分隔；北京时间为历史时间，速度 km/h）
 *  B) "纬度,经度"（度，十进制；无时间，帧内取当前时间）
 */
int uppath_upload(app_state *st, const char *file) {
    FILE *f = fopen(file, "r");
    unsigned char out[512], body[34];
    char line[160];
    int cnt = 0;
    if (!f) { perror("uppath open"); return -1; }
    while (fgets(line, sizeof(line), f)) {
        double lat = 0, lon = 0, spd = -1.0;
        int ymd = 0, h = 0, m = 0, s = 0;
        /* 先试 A 格式（ymd>0 且凑满经纬度）；不成再按老 B 格式 */
        if (sscanf(line, "%d %d:%d:%d %lf %lf %lf",
                   &ymd, &h, &m, &s, &lon, &lat, &spd) >= 6 && ymd > 0) {
            ; /* A 格式，ymd/h/m/s/lon/lat/spd 已就位 */
        } else if (sscanf(line, "%lf , %lf", &lat, &lon) == 2) {
            ymd = 0;                        /* B 格式：无历史时间 */
        } else {
            continue;
        }
        /* 协议完整 0x0200 消息体（28B 基本信息 + 里程附加项） */
        taxi_build_location_body(st, lat, lon, body);
        /* 历史点强制置"已定位"：状态位默认取板上 GPS 实时状态，室内未 fix
         * 时全是"未定位"，平台会按定位状态过滤掉这些真实历史坐标。
         * bit1(0x02)=已定位 bit18(0x00040000)=GPS定位，status 大端：
         * body[5]=bit16~23, body[7]=bit24~31 低 8 位 */
        body[5] |= 0x04;
        body[7] |= 0x02;
        if (ymd > 0) {
            /* 文件历史时间（北京时间）-> 协议要求 UTC：减 8h，
             * 跨日借位用 timegm/gmtime 处理，再回填 BCD 时间字段 */
            struct tm bt;
            struct tm *u;
            time_t ep;
            memset(&bt, 0, sizeof(bt));
            bt.tm_year = ymd / 10000 + 100;  /* 260601 -> 2026 年 */
            bt.tm_mon  = ymd / 100 % 100 - 1;
            bt.tm_mday = ymd % 100;
            bt.tm_hour = h; bt.tm_min = m; bt.tm_sec = s;
            ep = timegm(&bt) - 8 * 3600;
            u = gmtime(&ep);
            if (u) {
                int yy = (u->tm_year + 1900) % 100;
                int MM = u->tm_mon + 1, DD = u->tm_mday;
                int HH = u->tm_hour, MI = u->tm_min, SS = u->tm_sec;
                body[22] = (yy / 10) << 4 | (yy % 10);
                body[23] = (MM / 10) << 4 | (MM % 10);
                body[24] = (DD / 10) << 4 | (DD % 10);
                body[25] = (HH / 10) << 4 | (HH % 10);
                body[26] = (MI / 10) << 4 | (MI % 10);
                body[27] = (SS / 10) << 4 | (SS % 10);
            }
            /* 速度：文件第 5 列 km/h -> WORD 大端 body[18..19] */
            if (spd >= 0) {
                int k = (int)(spd + 0.5);
                if (k < 0)   k = 0;
                if (k > 999) k = 999;
                body[18] = (k >> 8) & 0xFF;
                body[19] = k & 0xFF;
            }
        }
        int n = jt808_build(out, sizeof(out), MSG_LOCATION, st->phone_id, st->term_id,
                            &st->serial, body, 34);
        if (st->net_fd >= 0 && n > 0) hal_net_send(st->net_fd, out, n);
        cnt++;
        usleep(200000);
    }
    fclose(f);
    printf("uppath %s done (%d points)\n", file, cnt);
    return 0;
}

void cmdline_loop(app_state *st) {
    char line[256];
    printf("taxi_terminal> "); fflush(stdout);
    while (fgets(line, sizeof(line), stdin)) {
        char cmd[64] = {0}, arg[128] = {0};
        char *p = line;
        /* 防呆：粘贴时把 "taxi_terminal>" 提示符一起粘进来的，自动跳过 */
        for (int i = 0; i < 3; i++) {
            char probe[64] = {0};
            if (sscanf(p, "%63s", probe) != 1) break;
            size_t pl = strlen(probe);
            if (pl > 0 && probe[pl - 1] == '>') {
                p += pl;
                while (*p == ' ' || *p == '\t') p++;
                continue;
            }
            break;
        }
        sscanf(p, "%63s %127s", cmd, arg);
        if (!strcmp(cmd, "?") || !strcmp(cmd, "help")) print_help();
        else if (!strcmp(cmd, "info")) print_info(st);
        else if (!strcmp(cmd, "servo")) {
            if (st->servo_fd < 0) { printf("servo not available\n"); }
            else if (!arg[0]) {
                /* 无参数：扫动测试 0→90→180→90→0，动作直观 */
                static const int seq[] = {0, 90, 180, 90, 0};
                for (unsigned i = 0; i < sizeof(seq)/sizeof(seq[0]); i++) {
                    int r = hal_servo_angle(st->servo_fd, seq[i]);
                    printf("servo -> %d (ioctl ret=%d)\n", seq[i], r);
                    fflush(stdout);
                    sleep(1);
                }
            }
            else {
                int a = atoi(arg);
                if (a < 0 || a > 180) { printf("angle 0-180\n"); }
                else {
                    int r = hal_servo_angle(st->servo_fd, a);
                    printf("servo -> %d deg (ioctl ret=%d, 0=ok)\n", a, r);
                }
            }
        }
        else if (!strcmp(cmd, "uppath")) {
            if (arg[0]) uppath_upload(st, arg);
            else uppath_upload(st, "/arduino_drivers/path.txt");   /* 无参数：预存历史路径文件 */
        }
        else if (cmd[0]) printf("unknown cmd: %s (try help)\n", cmd);
        printf("taxi_terminal> "); fflush(stdout);
    }
}
