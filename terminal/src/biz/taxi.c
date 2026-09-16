#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "taxi.h"
#include "hal.h"
#include "keymap.h"
#include "display_mgr.h"
#include "dispatch.h"
#include "jt808.h"

/* 供消息处理函数访问（taxi_init 时指向 main 的 st） */
static app_state *g_st;

static pthread_mutex_t g_send_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- 显示条目 getter ---------- */
static char *g_card(app_state *st) {
    static char b[16];
    snprintf(b, sizeof(b), "%02X%02X%02X%02X", st->card[0], st->card[1], st->card[2], st->card[3]);
    return b;
}
static char *g_verify(app_state *st) {
    return st->verified == 1 ? "PASS" : st->verified == -1 ? "FAIL" : "----";
}
static char *g_door(app_state *st) { return st->door_open ? "OPEN" : "CLOS"; }
static char *g_fatigue(app_state *st) { return st->fatigue ? "FATIG" : "OK"; }
static char *g_net(app_state *st) { return st->net_ok ? "NETOK" : "NET--"; }
static char *g_gps(app_state *st) {
    static char b[16];
    snprintf(b, sizeof(b), "%d", st->gps.sats);
    return b;
}

/* ---------- 上报 ---------- */
static void send_frame(app_state *st, unsigned short mid, const unsigned char *body, int len) {
    unsigned char out[512];
    int n, fd;
    pthread_mutex_lock(&g_send_lock);
    n = jt808_build(out, sizeof(out), mid, st->term_id, st->term_id, &st->serial, body, len);
    fd = st->net_fd;
    pthread_mutex_unlock(&g_send_lock);
    if (fd >= 0) hal_net_send(fd, out, n);
}

int taxi_report_auth(app_state *st) {
    unsigned char body[64];
    body[0] = st->card[0]; body[1] = st->card[1]; body[2] = st->card[2]; body[3] = st->card[3];
    /* 输入的身份码（ASCII）由 key 线程暂存到 st->auth_buf */
    int len = 4 + st->auth_len;
    memcpy(body + 4, st->auth_buf, st->auth_len);
    send_frame(st, MSG_AUTH_REQ, body, len);
    return 0;
}

int taxi_report_location(app_state *st) {
    unsigned char body[32] = {0};
    /* 报警标志4 + 状态4 = 0；纬度4 + 经度4 */
    long ilat = (long)(st->gps.lat * 1000000);
    long ilon = (long)(st->gps.lon * 1000000);
    body[8]=(ilat>>24)&0xff; body[9]=(ilat>>16)&0xff; body[10]=(ilat>>8)&0xff; body[11]=ilat&0xff;
    body[12]=(ilon>>24)&0xff; body[13]=(ilon>>16)&0xff; body[14]=(ilon>>8)&0xff; body[15]=ilon&0xff;
    send_frame(st, MSG_LOCATION, body, 16);
    return 0;
}

/* 0x8110 身份验证应答 */
int taxi_handle_auth_resp(unsigned short id, const unsigned char *b, int len) {
    (void)id;
    app_state *st = g_st;
    if (len < 1) return -1;
    pthread_mutex_lock(&st->lock);
    st->verified = (b[0] == 0) ? 1 : -1;
    pthread_mutex_unlock(&st->lock);
    hal_display_string(st->display_fd, st->verified == 1 ? "PASS" : "FAIL");
    hal_beep_on(st->beep_fd); usleep(200000); hal_beep_off(st->beep_fd);
    return 0;
}

/* 0x8210 疲劳驾驶下发 */
int taxi_handle_fatigue(unsigned short id, const unsigned char *b, int len) {
    (void)id;
    app_state *st = g_st;
    if (len < 1) return -1;
    pthread_mutex_lock(&st->lock);
    st->fatigue = (b[0] == 1) ? 1 : 0;
    pthread_mutex_unlock(&st->lock);
    return 0;
}

/* ---------- 线程 ---------- */
static char auth_buf[8];
static int  auth_len = 0;

void *taxi_key_thread(void *arg) {
    app_state *st = (app_state *)arg;
    while (1) {
        unsigned int code = 0; char d = 0;
        int v = hal_key_read(st->key_fd, &code);
        if (v != 1) continue;
        int a = key_map(code, &d);
        int need_auth = 0;
        pthread_mutex_lock(&st->lock);
        if (a == K_DIGIT) {
            if (auth_len < 6) { auth_buf[auth_len++] = d; auth_buf[auth_len] = 0; }
            hal_display_string(st->display_fd, auth_buf);
            printf("[身份码输入] %.*s\n", auth_len, auth_buf);
        } else if (a == K_CONFIRM) {
            auth_buf[auth_len] = 0;
            memcpy(st->auth_buf, auth_buf, auth_len);
            st->auth_len = auth_len;
            need_auth = 1;
            auth_len = 0;
        } else if (a == K_CLOSE) {
            st->door_open = 0;
            hal_servo_angle(st->servo_fd, st->cfg.door_close_angle);
            printf("[车门] 关闭\n");
        } else if (a == K_PREV) {
            st->disp_idx = (st->disp_idx + display_mgr_count() - 1) % display_mgr_count();
            display_mgr_show_item(st, st->disp_idx, 1);
        } else if (a == K_NEXT) {
            st->disp_idx = (st->disp_idx + 1) % display_mgr_count();
            display_mgr_show_item(st, st->disp_idx, 1);
        }
        st->last_key = time(NULL);
        pthread_mutex_unlock(&st->lock);
        if (need_auth) taxi_report_auth(st);
    }
    return NULL;
}

void *taxi_rfid_thread(void *arg) {
    app_state *st = (app_state *)arg;
    unsigned char card[4];
    while (1) {
        int r = hal_rfid_read(st->rfid_fd, card);
        /* 厂商驱动无卡时也返回 4 字节 0x00，全 0 视为“没有卡”，忽略 */
        if (r == 1 && (card[0] | card[1] | card[2] | card[3]) != 0) {
            pthread_mutex_lock(&st->lock);
            memcpy(st->card, card, 4); st->has_card = 1;
            st->door_open = 1;
            pthread_mutex_unlock(&st->lock);
            hal_servo_angle(st->servo_fd, st->cfg.door_open_angle);
            printf("[刷卡] 卡号 %02X%02X%02X%02X，车门打开\n", card[0],card[1],card[2],card[3]);
            usleep(500000);
        } else {
            usleep(200000);
        }
    }
    return NULL;
}

void *taxi_gps_thread(void *arg) {
    app_state *st = (app_state *)arg;
    char buf[512];
    while (1) {
        int n = read(st->gps_fd, buf, sizeof(buf) - 1);
        if (n <= 0) { usleep(500000); continue; }
        buf[n] = 0;
        if (hal_gps_parse(buf, n, &st->gps) == 1) {
            taxi_report_location(st);
        }
        usleep(1000000);
    }
    return NULL;
}

void *taxi_beep_thread(void *arg) {
    app_state *st = (app_state *)arg;
    while (1) {
        int fatigue;
        pthread_mutex_lock(&st->lock); fatigue = st->fatigue; pthread_mutex_unlock(&st->lock);
        if (fatigue) {
            hal_beep_on(st->beep_fd); usleep(300000);
            hal_beep_off(st->beep_fd); usleep(300000);
        } else {
            usleep(100000);
        }
    }
    return NULL;
}

void *taxi_cycle_thread(void *arg) {
    app_state *st = (app_state *)arg;
    while (1) {
        sleep(1);
        pthread_mutex_lock(&st->lock);
        time_t now = time(NULL);
        if (now - st->last_key >= 10) {
            /* 轮播：每项 3 秒，只显示数码管不写控制台 */
            for (int i = 0; i < display_mgr_count(); i++) {
                display_mgr_show_item(st, i, 0);
                pthread_mutex_unlock(&st->lock);
                sleep(3);
                pthread_mutex_lock(&st->lock);
            }
            st->last_key = now;
        }
        pthread_mutex_unlock(&st->lock);
    }
    return NULL;
}

/* ---------- 初始化 ---------- */
int taxi_init(app_state *st) {
    g_st = st;
    st->key_fd = hal_key_open();
    st->display_fd = hal_display_open();
    st->rfid_fd = hal_rfid_open();
    st->beep_fd = hal_beep_open();
    st->servo_fd = hal_servo_open();
    st->gps_fd = hal_gps_open();
    st->net_fd = -1;

    display_mgr_register(0, "卡号", g_card);
    display_mgr_register(1, "身份验证", g_verify);
    display_mgr_register(2, "车门", g_door);
    display_mgr_register(3, "疲劳", g_fatigue);
    display_mgr_register(4, "网络", g_net);
    display_mgr_register(5, "GPS卫星", g_gps);

    dispatch_register(MSG_AUTH_RESP, taxi_handle_auth_resp);
    dispatch_register(MSG_FATIGUE, taxi_handle_fatigue);

    hal_servo_angle(st->servo_fd, st->cfg.door_close_angle);   /* 初始关门 */
    return 0;
}
