#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "cmdline.h"
#include "hal.h"
#include "jt808.h"
#include "dispatch.h"

static void print_help(void) {
    printf("commands:\n");
    printf("  ? / help                 show this help\n");
    printf("  info                     show current sensor values\n");
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
    unsigned char out[512];
    char line[128];
    if (!f) { perror("uppath open"); return -1; }
    while (fgets(line, sizeof(line), f)) {
        double lat, lon;
        if (sscanf(line, "%lf , %lf", &lat, &lon) != 2) continue;
        /* 简易 0x0200 消息体：报警0/状态0 + 纬度4 + 经度4 */
        unsigned char body[16] = {0};
        long ilat = (long)(lat * 1000000);
        long ilon = (long)(lon * 1000000);
        body[0]=body[1]=body[2]=body[3]=0; body[4]=body[5]=body[6]=body[7]=0;
        body[8] = (ilat>>24)&0xff; body[9]=(ilat>>16)&0xff; body[10]=(ilat>>8)&0xff; body[11]=ilat&0xff;
        body[12] = (ilon>>24)&0xff; body[13]=(ilon>>16)&0xff; body[14]=(ilon>>8)&0xff; body[15]=ilon&0xff;
        int n = jt808_build(out, sizeof(out), MSG_LOCATION, st->term_id, st->term_id,
                            &st->serial, body, 16);
        if (st->net_fd >= 0) hal_net_send(st->net_fd, out, n);
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
        else if (!strcmp(cmd, "uppath")) {
            if (arg[0]) uppath_upload(st, arg);
            else printf("usage: uppath <coord-file>\n");
        }
        else if (cmd[0]) printf("unknown cmd: %s (try help)\n", cmd);
        printf("taxi_terminal> "); fflush(stdout);
    }
}
