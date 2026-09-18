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
    n = jt808_build(out, sizeof(out), mid, st->phone_id, st->term_id, &st->serial, body, len);
    fd = st->net_fd;
    pthread_mutex_unlock(&g_send_lock);
    if (fd >= 0 && n > 0) hal_net_send(fd, out, n);
}

/* 终端通用应答 0x0001：应答流水号(2) + 应答ID(2) + 结果(1) */
int taxi_send_term_ack(app_state *st, unsigned short ack_seq, unsigned short ack_id,
                       unsigned char result) {
    unsigned char body[5];
    body[0] = ack_seq >> 8;  body[1] = ack_seq & 0xFF;
    body[2] = ack_id >> 8;   body[3] = ack_id & 0xFF;
    body[4] = result;
    send_frame(st, MSG_TERM_ACK, body, 5);
    printf("[TX] TERM-ACK seq=%u id=0x%04X result=%u\n", ack_seq, ack_id, result);
    return 0;
}

int taxi_report_auth(app_state *st) {
    unsigned char body[64];
    body[0] = st->card[0]; body[1] = st->card[1]; body[2] = st->card[2]; body[3] = st->card[3];
    /* 输入的身份码（ASCII）由 key 线程暂存到 st->auth_buf */
    int len = 4 + st->auth_len;
    memcpy(body + 4, st->auth_buf, st->auth_len);
    send_frame(st, MSG_AUTH_REQ, body, len);
    printf("[TX] AUTH-REQ card=%02X%02X%02X%02X code=%.*s\n",
           st->card[0], st->card[1], st->card[2], st->card[3], st->auth_len, st->auth_buf);
    return 0;
}

/*
 * 0x0200 位置报警信息汇报，消息体 = 位置基本信息(28B) + 附加信息项列表：
 *   报警标志(4) bit2=疲劳驾驶
 *   状态(4)     bit1=已定位 bit12=车门加锁 bit13=门1开 bit18=GPS定位
 *   纬度(4)     度*1e6 大端
 *   经度(4)     度*1e6 大端
 *   高程(2)/速度(2)/方向(2)
 *   时间 BCD[6] YYMMDDhhmmss（UTC；协议要求 GMT+8 时间需换算 UTC 上报）
 *   附加项：ID=0x01 里程 DWORD（协议备注：附加信息只用到了里程）
 */
void taxi_build_location_body(app_state *st, double lat, double lon,
                              unsigned char body[34]) {
    long ilat = (long)(lat * 1000000.0);
    long ilon = (long)(lon * 1000000.0);
    long alarm = st->fatigue ? 0x00000004L : 0L;
    long status = 0;
    int yy = 0, mm = 0, dd = 0, hh = 0, mi = 0, ss = 0;

    memset(body, 0, 34);
    if (st->gps.status > 0) {
        status |= 0x02L        /* bit1  已定位 */
               |  0x40000L;    /* bit18 GPS定位 */
        yy = st->gps.year % 100; mm = st->gps.month; dd = st->gps.day;
        hh = st->gps.hour; mi = st->gps.min; ss = st->gps.sec;
    }
    pthread_mutex_lock(&st->lock);
    status |= st->door_open ? 0x2000L     /* bit13 门1开 */
                            : 0x1000L;    /* bit12 车门加锁 */
    pthread_mutex_unlock(&st->lock);

    /* 无 GPS 日期时用系统时间换算 UTC（GMT+8 - 8h） */
    if (yy == 0) {
        time_t t = time(NULL) - 8 * 3600;
        struct tm *u = gmtime(&t);
        if (u) { yy = (u->tm_year + 1900) % 100; mm = u->tm_mon + 1; dd = u->tm_mday;
                 hh = u->tm_hour; mi = u->tm_min; ss = u->tm_sec; }
    }

    body[0]  = (alarm >> 24) & 0xFF; body[1]  = (alarm >> 16) & 0xFF;
    body[2]  = (alarm >> 8)  & 0xFF; body[3]  = alarm & 0xFF;
    body[4]  = (status >> 24) & 0xFF; body[5]  = (status >> 16) & 0xFF;
    body[6]  = (status >> 8)  & 0xFF; body[7]  = status & 0xFF;
    body[8]  = (ilat >> 24) & 0xFF;  body[9]  = (ilat >> 16) & 0xFF;
    body[10] = (ilat >> 8)  & 0xFF;  body[11] = ilat & 0xFF;
    body[12] = (ilon >> 24) & 0xFF;  body[13] = (ilon >> 16) & 0xFF;
    body[14] = (ilon >> 8)  & 0xFF;  body[15] = ilon & 0xFF;
    /* 高程/速度/方向保持 0 */
    body[21] = 0x00;                            /* 方向低字节（方向=0） */
    body[22] = (yy / 10) << 4 | (yy % 10);      /* YY */
    body[23] = (mm / 10) << 4 | (mm % 10);      /* MM */
    body[24] = (dd / 10) << 4 | (dd % 10);      /* DD */
    body[25] = (hh / 10) << 4 | (hh % 10);      /* hh */
    body[26] = (mi / 10) << 4 | (mi % 10);      /* mm */
    body[27] = (ss / 10) << 4 | (ss % 10);      /* ss */
    /* 附加项：里程 ID=0x01 长度=4 值=0 */
    body[28] = 0x01; body[29] = 0x04;
    body[30] = body[31] = body[32] = body[33] = 0x00;
}

int taxi_report_location(app_state *st) {
    unsigned char body[34];
    taxi_build_location_body(st, st->gps.lat, st->gps.lon, body);
    send_frame(st, MSG_LOCATION, body, 34);
    return 0;
}

/* 0x8001 平台通用应答：应答流水号(2)+应答ID(2)+结果(1) */
static int taxi_handle_plat_ack(unsigned short id, const unsigned char *b, int len) {
    (void)id;
    if (len < 5) return -1;
    unsigned short aseq = (b[0] << 8) | b[1];
    unsigned short aid  = (b[2] << 8) | b[3];
    printf("[RX] PLAT-ACK: my_seq=%u msgID=0x%04X result=%u (%s)\n",
           aseq, aid, b[4], b[4] == 0 ? "OK" : "FAIL");
    return 0;
}

/* 0x8811 平台下发蜂鸣器控制：0x01=开 0x00=关 */
static int taxi_handle_buzzer(unsigned short id, const unsigned char *b, int len) {
    (void)id;
    app_state *st = g_st;
    if (len < 1) return -1;
    int on = (b[0] == 0x01);
    printf("[RX] BEEP control: %s\n", on ? "ON" : "OFF");
    if (st->beep_fd >= 0) {
        if (on) hal_beep_on(st->beep_fd);
        else    hal_beep_off(st->beep_fd);
    } else {
        printf("     (beep not available, print only)\n");
    }
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
    printf("[RX] FATIGUE: %s\n", st->fatigue ? "ON" : "OFF");
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
        printf("[KEY] raw=%u\n", code);   /* 调试：打印实际键码，便于校准键值表 */
        int a = key_map(code, &d);
        int need_auth = 0;
        pthread_mutex_lock(&st->lock);
        if (a == K_DIGIT) {
            if (auth_len < 6) { auth_buf[auth_len++] = d; auth_buf[auth_len] = 0; }
            hal_display_char(d);   /* 新数字滚入最左位，旧数字右移 */
            printf("[IDCODE] %.*s\n", auth_len, auth_buf);
        } else if (a == K_CONFIRM) {
            auth_buf[auth_len] = 0;
            memcpy(st->auth_buf, auth_buf, auth_len);
            st->auth_len = auth_len;
            need_auth = 1;
            auth_len = 0;
        } else if (a == K_CLOSE) {
            st->door_open = 0;
            hal_servo_angle(st->servo_fd, st->cfg.door_close_angle);
            printf("[DOOR] close\n");
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
        /* 厂商驱动无卡时也返回 4 字节 0x00，全 0 视为"没有卡"，忽略 */
        if (r == 1 && (card[0] | card[1] | card[2] | card[3]) != 0) {
            pthread_mutex_lock(&st->lock);
            memcpy(st->card, card, 4); st->has_card = 1;
            st->door_open = 1;
            pthread_mutex_unlock(&st->lock);
            hal_servo_angle(st->servo_fd, st->cfg.door_open_angle);
            printf("[RFID] card %02X%02X%02X%02X, door open\n",
                   card[0], card[1], card[2], card[3]);
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

    display_mgr_register(0, "CARD", g_card);
    display_mgr_register(1, "VERIFY", g_verify);
    display_mgr_register(2, "DOOR", g_door);
    display_mgr_register(3, "FATIGUE", g_fatigue);
    display_mgr_register(4, "NET", g_net);
    display_mgr_register(5, "GPS-SATS", g_gps);

    dispatch_register(MSG_PLAT_ACK,  taxi_handle_plat_ack);
    dispatch_register(MSG_BUZZER,    taxi_handle_buzzer);
    dispatch_register(MSG_AUTH_RESP, taxi_handle_auth_resp);
    dispatch_register(MSG_FATIGUE,   taxi_handle_fatigue);

    hal_servo_angle(st->servo_fd, st->cfg.door_close_angle);   /* 初始关门 */
    return 0;
}
