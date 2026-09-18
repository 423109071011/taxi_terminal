#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "cmdline.h"
#include "hal.h"
#include "jt808.h"
#include "dispatch.h"
#include "taxi.h"

static void print_help(void) {
    printf("commands:\n");
    printf("  ? / help                 show this help\n");
    printf("  info                     show current sensor values\n");
    printf("  servo [0-180]            no arg: sweep test; with angle: set angle\n");
    printf("  uppath <coord-file>      upload history path to server\n");
}

static void print_info(app_state *st) {
    printf("net_ok=%d door_open=%d verified=%d fatigue=%d\n",
           st->net_ok, st->door_open, st->verified, st->fatigue);
    printf("gps: lat=%.6f lon=%.6f sats=%d status=%d\n",
           st->gps.lat, st->gps.lon, st->gps.sats, st->gps.status);
    printf("card: %02x%02x%02x%02x has_card=%d\n",
           st->card[0], st->card[1], st->card[2], st->card[3], st->has_card);
}

/* 历史路径文件格式：每行 "纬度,经度"（度，十进制） */
int uppath_upload(app_state *st, const char *file) {
    FILE *f = fopen(file, "r");
    unsigned char out[512], body[34];
    char line[128];
    if (!f) { perror("uppath open"); return -1; }
    while (fgets(line, sizeof(line), f)) {
        double lat, lon;
        if (sscanf(line, "%lf , %lf", &lat, &lon) != 2) continue;
        /* 协议完整 0x0200 消息体（28B 基本信息 + 里程附加项） */
        taxi_build_location_body(st, lat, lon, body);
        int n = jt808_build(out, sizeof(out), MSG_LOCATION, st->phone_id, st->term_id,
                            &st->serial, body, 34);
        if (st->net_fd >= 0 && n > 0) hal_net_send(st->net_fd, out, n);
        usleep(200000);
    }
    fclose(f);
    printf("uppath %s done\n", file);
    return 0;
}

void cmdline_loop(app_state *st) {
    char line[256];
    printf("taxi_terminal> "); fflush(stdout);
    while (fgets(line, sizeof(line), stdin)) {
        char cmd[64] = {0}, arg[128] = {0};
        sscanf(line, "%63s %127s", cmd, arg);
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
            else printf("usage: uppath <coord-file>\n");
        }
        else if (cmd[0]) printf("unknown cmd: %s (try help)\n", cmd);
        printf("taxi_terminal> "); fflush(stdout);
    }
}
