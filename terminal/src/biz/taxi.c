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

/* 疲劳驾驶计时：平台认证 PASS 开门后开始连续计时，达到阈值本地置疲劳报警
 * （阈值按交规 4 小时；演示/测试时可临时改小；平台 0x8210 仍可随时下发） */
#define DRIVE_FATIGUE_MIN 240
/* 刷卡有效期：确认时距刷卡超过该秒数，视为"刷卡已过期"，
 * 0x0210 卡号字段填全 0（无卡）仅上报密码，平台按白名单查不到自然拒绝 */
#define CARD_VALID_SEC 30

static pthread_mutex_t g_send_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- 卡号换算 ---------- */
/* RFID 4 字节 UID 按大端合成一个 32 位整数，再转十进制
 * （例：0D 0E 0F 10 → 0x0D0E0F10 → 219025168）。
 * 数码管显示与 0x0210 上传的卡号均使用该换算值，保证两侧一致 */
static unsigned long taxi_card_id(const unsigned char card[4]) {
    return ((unsigned long)card[0] << 24) | ((unsigned long)card[1] << 16) |
           ((unsigned long)card[2] << 8)  |  (unsigned long)card[3];
}

/* ---------- 显示条目 getter ---------- */
static char *g_card(app_state *st) {
    static char b[16];
    /* 与刷卡页一致：4 字节 UID 大端合成十进制（0x0D0E0F10 → 219025168）；
     * 第 0 页类型值只放得下 6 位，显示卡号十进制的后六位 */
    int n = snprintf(b, sizeof(b), "%08lu", taxi_card_id(st->card));
    if (n > 6) return b + n - 6;
    return b;
}
/* 显示条目数值编码（验收规格：功能类型由数字代表，格式 = 类型号-数值） */
static char *g_verify(app_state *st) {
    static char b[8];
    snprintf(b, sizeof(b), "%05d", st->verified == 1 ? 1 : st->verified == -1 ? 2 : 0);
    return b;                       /* 0=未验证 1=通过 2=拒绝 */
}
static char *g_door(app_state *st) {
    static char b[8];
    snprintf(b, sizeof(b), "%05d", st->door_open ? 1 : 0);
    return b;                       /* 0=关闭 1=打开 */
}
static char *g_fatigue(app_state *st) {
    static char b[8];
    snprintf(b, sizeof(b), "%05d", st->fatigue ? 1 : 0);
    return b;                       /* 0=正常 1=疲劳报警 */
}
static char *g_net(app_state *st) {
    static char b[8];
    snprintf(b, sizeof(b), "%05d", st->net_ok ? 1 : 0);
    return b;                       /* 0=离线 1=在线 */
}
static char *g_gps(app_state *st) {
    static char b[16];
    snprintf(b, sizeof(b), "%05d", st->gps.sats);
    return b;                       /* 卫星颗数 */
}
static char *g_smoke(app_state *st) {
    static char b[8];
    snprintf(b, sizeof(b), "%05d", st->smoke_alarm ? 1 : 0);
    return b;                       /* 0=正常 1=烟雾超标 */
}
static char *g_drive(app_state *st) {
    static char buf[12];
    long m = st->drive_start ? (long)((time(NULL) - st->drive_start) / 60) : 0L;
    snprintf(buf, sizeof(buf), "%05ld", m);
    return buf;                     /* 认证开门后的连续驾驶分钟数 */
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
    /* 正常应答不打印，避免每秒心跳刷屏；仅异常时提示 */
    if (result != 0)
        printf("[TX] TERM-ACK seq=%u id=0x%04X result=%u (FAIL)\n", ack_seq, ack_id, result);
    return 0;
}

int taxi_report_auth(app_state *st) {
    unsigned char body[64];
    /* 刷卡有效期判定：确认时距刷卡超 CARD_VALID_SEC 秒 → 卡号字段填全 0
     * （"0000000000"=无卡），只上报密码，平台白名单查不到即拒绝 */
    int expired = (st->card_time == 0) ||
                  (time(NULL) - st->card_time > CARD_VALID_SEC);
    /* 卡号 = 4 字节 UID 大端合成十进制，定长 10 位 ASCII（前导 0 补齐）上传，
     * 平台取消息体前 10 字节为卡号，其后为密码 ASCII（auth_len ≤ 6，总长 ≤ 16） */
    int len;
    if (expired)
        memcpy(body, "0000000000", 10);
    else
        snprintf((char *)body, 11, "%010lu", taxi_card_id(st->card));
    len = 10 + st->auth_len;
    memcpy(body + 10, st->auth_buf, st->auth_len);
    send_frame(st, MSG_AUTH_REQ, body, len);
    if (expired)
        printf("[TX] AUTH-REQ card=0000000000 (card expired >%ds) code=%.*s\n",
               CARD_VALID_SEC, st->auth_len, st->auth_buf);
    else
        printf("[TX] AUTH-REQ card=%010lu code=%.*s\n",
               taxi_card_id(st->card), st->auth_len, st->auth_buf);
    return 0;
}

/*
 * 0x0200 位置报警信息汇报，消息体 = 位置基本信息(28B) + 附加信息项列表：
 *   报警标志(4) bit2=疲劳驾驶
 *   状态(4)     bit1=已定位 bit12=车门加锁 bit13=门1开 bit18=GPS定位
 *   纬度(4)     度*1e6 大端
 *   经度(4)     度*1e6 大端
 *   高程(2)/速度(2)/方向(2)：速度=GPS 地面速度 km/h
 *   时间 BCD[6] YYMMDDhhmmss（UTC；协议要求 GMT+8 时间需换算 UTC 上报）
 *   附加项：ID=0x01 里程 DWORD（协议备注：附加信息只用到了里程）
 */
void taxi_build_location_body(app_state *st, double lat, double lon,
                              unsigned char body[34]) {
    long ilat = (long)(lat * 1000000.0);
    long ilon = (long)(lon * 1000000.0);
    /* 报警标志：bit2=疲劳驾驶(JT/T808)，bit0=紧急报警（此处用作车内烟雾超标） */
    long alarm = 0;
    if (st->fatigue)    alarm |= 0x00000004L;
    if (st->smoke_alarm) alarm |= 0x00000001L;
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
    /* 高程/方向保持 0；速度 = GPS 地面速度 km/h（平台按 alarm_rule 判超速） */
    {
        int spd = (int)(st->gps.speed_kmh + 0.5);
        if (spd < 0)   spd = 0;
        if (spd > 999) spd = 999;       /* WORD 上限内 */
        body[18] = (spd >> 8) & 0xFF;
        body[19] = spd & 0xFF;
    }
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
    /* 正常 OK 静音（每秒心跳都会应答），仅失败时提示 */
    if (b[4] != 0)
        printf("[RX] PLAT-ACK: my_seq=%u msgID=0x%04X result=%u (FAIL)\n", aseq, aid, b[4]);
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

/* 0x8110 身份验证应答：通过 → 开车门(舵机90°)，失败仅提示 */
int taxi_handle_auth_resp(unsigned short id, const unsigned char *b, int len) {
    (void)id;
    app_state *st = g_st;
    if (len < 1) return -1;
    pthread_mutex_lock(&st->lock);
    st->verified = (b[0] == 0) ? 1 : -1;
    int v = st->verified;
    if (v == 1) {
        st->door_open = 1;
        st->drive_start = time(NULL);   /* 平台确认通过，开始疲劳驾驶计时 */
        st->last_key = time(NULL);   /* 认证开门视为一次操作，刷新空闲计时 */
    }
    pthread_mutex_unlock(&st->lock);
    if (v == 1 || !st->door_open)
        hal_display_string(st->display_fd, v == 1 ? "PASS" : "FAIL");
    else
        /* 刷卡已物理开门、仅身份码未匹配（FAIL）时不覆盖显示，避免误导 */
        printf("[AUTH] FAIL ignored (door already opened by card swipe)\n");
    if (v == 1 && st->servo_fd >= 0)
        hal_servo_angle(st->servo_fd, st->cfg.door_open_angle);
    hal_beep_on(st->beep_fd); usleep(200000); hal_beep_off(st->beep_fd);
    printf("[AUTH] %s%s\n", v == 1 ? "PASS" : "FAIL",
           v == 1 ? ", door open" : "");
    if (v == 1) taxi_report_location(st);   /* 车门状态变化立即上报 */
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
    taxi_report_location(st);   /* 报警状态变化立即上报（对接要求，不等心跳） */
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
        int need_auth = 0, need_report = 0;
        pthread_mutex_lock(&st->lock);
        if (a == K_DIGIT) {
            if (auth_len < 6) { auth_buf[auth_len++] = d; auth_buf[auth_len] = 0; }
            /* 数码管只显示已输入的密码，按输入顺序左对齐（如 12345 显示 12345），
             * 不再与卡号混显滚入 */
            char pw[8];
            snprintf(pw, sizeof(pw), "%.*s", auth_len, auth_buf);
            hal_display_string(st->display_fd, pw);
            /* 串口终端按 GBK 解码，中文用 GBK 字节转义，避免乱码 */
            printf("[\xc9\xed\xb7\xdd\xc2\xeb\xca\xe4\xc8\xeb] %.*s\n", auth_len, auth_buf);
        } else if (a == K_CONFIRM) {
            if (!st->has_card) {
                /* 未刷卡就确认：提示并清空已输入身份码 */
                printf("[\xcc\xe1\xca\xbe] \xc7\xeb\xcf\xc8\xcb\xa2\xbf\xa8\xd4\xd9\xc8\xb7\xc8\xcf\n");
                auth_len = 0;
            } else if (auth_len == 0) {
                /* 规范流程：刷卡后必须输入密码，空密码不允许上报平台 */
                printf("[\xcc\xe1\xca\xbe] \xc7\xeb\xca\xe4\xc8\xeb\xc3\xdc\xc2\xeb\xba\xf3\xd4\xd9\xc8\xb7\xc8\xcf\n");
            } else {
                auth_buf[auth_len] = 0;
                memcpy(st->auth_buf, auth_buf, auth_len);
                st->auth_len = auth_len;
                need_auth = 1;
                auth_len = 0;
            }
        } else if (a == K_CLOSE) {
            st->door_open = 0;
            st->drive_start = 0;         /* 关车门：停止疲劳驾驶计时 */
            need_report = 1;    /* 关车门状态变化立即上报 */
            hal_servo_angle(st->servo_fd, st->cfg.door_close_angle);
            printf("[\xb3\xb5\xc3\xc5] \xb9\xd8\xb1\xd5\n");
        } else if (a == K_PREV) {
            st->disp_idx = (st->disp_idx + display_mgr_count() - 1) % display_mgr_count();
            display_mgr_show_item(st, st->disp_idx, 1);
        } else if (a == K_NEXT) {
            st->disp_idx = (st->disp_idx + 1) % display_mgr_count();
            display_mgr_show_item(st, st->disp_idx, 1);
        } else if (a == K_DISMISS) {
            /* 解除报警：清疲劳/烟雾标志（蜂鸣线程下个周期自动停响），
             * 并直接关一次蜂鸣器——平台 0x8811 直控的蜂鸣不经过标志位，
             * 必须显式关闭 */
            hal_beep_off(st->beep_fd);
            st->fatigue = 0;
            st->smoke_alarm = 0;
            if (st->drive_start) st->drive_start = time(NULL);  /* 解除疲劳后重新计时 */
            need_report = 1;    /* 报警解除状态变化立即上报 */
            printf("[KEY] code=%u alarm dismissed (fatigue/smoke cleared, beep off)\n", code);
            hal_display_string(st->display_fd, "OK");
        }
        st->last_key = time(NULL);
        pthread_mutex_unlock(&st->lock);
        if (need_auth) taxi_report_auth(st);
        if (need_report) taxi_report_location(st);
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
            st->card_time = time(NULL);  /* 刷卡时刻：30s 内确认才带卡号上报 */
            st->last_key = time(NULL);   /* 刷卡视为一次操作，刷新空闲计时 */
            pthread_mutex_unlock(&st->lock);
            char ids[16];
            /* 卡号换算：4 字节 UID 大端合成十进制（0D 0E 0F 10 → 219025168）；
             * 数码管整屏只有 8 位，超长时右对齐显示后 8 位 */
            int in = snprintf(ids, sizeof(ids), "%08lu", taxi_card_id(card));
            const char *disp = (in > 8) ? ids + in - 8 : ids;
            hal_display_string(st->display_fd, disp);  /* 卡号十进制显示在数码管 */
            auth_len = 0;                              /* 新卡重置密码输入 */
            printf("[\xcb\xa2\xbf\xa8] \xbf\xa8\xba\xc5 %s(hex:%02X%02X%02X%02X)\xa3\xac\xc7\xeb\xca\xe4\xc8\xeb\xc9\xed\xb7\xdd\xc2\xeb\xb2\xa2\xc8\xb7\xc8\xcf\n",
                   ids, card[0], card[1], card[2], card[3]);
            usleep(500000);
        } else {
            usleep(200000);
        }
    }
    return NULL;
}

/* 兜底心跳：独立线程每秒上报一帧 0x0200（不依赖 GPS 是否定位/解析）。
 * 平台侧 IdleStateHandler 读空闲超时(30s/180s)会关闭连接，
 * 心跳必须严格 1s 周期，且不能挂在可能阻塞的 GPS read() 上。 */
void *taxi_heartbeat_thread(void *arg) {
    app_state *st = (app_state *)arg;
    while (1) {
        taxi_report_location(st);
        usleep(1000000);
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
        /* 只负责解析更新 st->gps；上报由 heartbeat 线程统一每秒执行 */
        hal_gps_parse(buf, n, &st->gps);
    }
    return NULL;
}

void *taxi_beep_thread(void *arg) {
    app_state *st = (app_state *)arg;
    while (1) {
        int fatigue, smoke;
        pthread_mutex_lock(&st->lock);
        fatigue = st->fatigue; smoke = st->smoke_alarm;
        pthread_mutex_unlock(&st->lock);
        if (fatigue || smoke) {
            hal_beep_on(st->beep_fd); usleep(300000);
            hal_beep_off(st->beep_fd); usleep(300000);
        } else {
            /* 疲劳驾驶计时检查：认证开门后连续驾驶达到阈值，本地置疲劳报警 */
            int trig = 0;
            pthread_mutex_lock(&st->lock);
            if (st->drive_start && !st->fatigue &&
                time(NULL) - st->drive_start >= (time_t)DRIVE_FATIGUE_MIN * 60) {
                st->fatigue = 1;
                trig = 1;
            }
            pthread_mutex_unlock(&st->lock);
            if (trig) {
                printf("[FATIGUE] driving %dmin, local fatigue alarm ON\n", DRIVE_FATIGUE_MIN);
                taxi_report_location(st);   /* 报警位置变化立即上报 */
            }
            usleep(100000);
        }
    }
    return NULL;
}

/* ---------- 烟雾传感器采集（滞回 + 连续确认，防阈值抖动） ----------
 * 报警开启：连续 SMOKE_CONSEC 次采样 >= SMOKE_TH_ON
 * 报警解除：连续 SMOKE_CONSEC 次采样 <= SMOKE_TH_OFF（低于开启阈值 SMOKE_HYST）
 * 这样值在阈值附近跳动时不会频繁启停蜂鸣器/报警位。 */
#define SMOKE_TH_ON     2000    /* 报警开启阈值（12位AD，0~4095），可按传感器实测调整 */
#define SMOKE_HYST      200     /* 滞回区间 */
#define SMOKE_TH_OFF    (SMOKE_TH_ON - SMOKE_HYST)
#define SMOKE_CONSEC    3       /* 连续确认次数 */
#define SMOKE_PERIOD_MS 500     /* 采样周期 */

void *taxi_smoke_thread(void *arg) {
    app_state *st = (app_state *)arg;
    int hi = 0, lo = 0;
    if (st->adc_fd < 0) { printf("smoke: adc not available, sensor thread off\n"); return NULL; }
    while (1) {
        int v = hal_adc_read(st->adc_fd);
        if (v >= 0) {
            pthread_mutex_lock(&st->lock);
            st->smoke_val = v;              /* 最新原始值供 info 命令显示 */
            int st_now = st->smoke_alarm;
            pthread_mutex_unlock(&st->lock);
            hi = (v >= SMOKE_TH_ON)  ? hi + 1 : 0;
            lo = (v <= SMOKE_TH_OFF) ? lo + 1 : 0;
            if (!st_now && hi >= SMOKE_CONSEC) {
                pthread_mutex_lock(&st->lock);
                st->smoke_alarm = 1;
                pthread_mutex_unlock(&st->lock);
                printf("[SMOKE] alarm ON (adc=%d >= %d)\n", v, SMOKE_TH_ON);
                taxi_report_location(st);   /* 状态变化立即上报 */
            } else if (st_now && lo >= SMOKE_CONSEC) {
                pthread_mutex_lock(&st->lock);
                st->smoke_alarm = 0;
                pthread_mutex_unlock(&st->lock);
                printf("[SMOKE] alarm OFF (adc=%d <= %d)\n", v, SMOKE_TH_OFF);
                taxi_report_location(st);
            }
        }
        usleep(SMOKE_PERIOD_MS * 1000);
    }
    return NULL;
}

void *taxi_cycle_thread(void *arg) {
    app_state *st = (app_state *)arg;
    int pos = 0;                       /* 轮播游标 */
    sleep(1);
    pthread_mutex_lock(&st->lock);
    hal_display_clear(st->display_fd);  /* 开机清一次残留 */
    pthread_mutex_unlock(&st->lock);
    while (1) {
        pthread_mutex_lock(&st->lock);
        int idle = time(NULL) - st->last_key >= 10;
        pthread_mutex_unlock(&st->lock);
        if (idle) {
            /* 空闲轮播：每 3 秒切换一项，仅数码管显示，控制台无输出（验收 §B）*/
            display_mgr_show_item(st, pos, 0);
            pos = (pos + 1) % display_mgr_count();
            sleep(3);
        } else {
            sleep(1);
        }
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
    st->adc_fd = hal_adc_open();
    st->net_fd = -1;

    display_mgr_register(0, "CARD", g_card);
    display_mgr_register(1, "VERIFY", g_verify);
    display_mgr_register(2, "DOOR", g_door);
    display_mgr_register(3, "FATIGUE", g_fatigue);
    display_mgr_register(4, "NET", g_net);
    display_mgr_register(5, "GPS-SATS", g_gps);
    display_mgr_register(6, "SMOKE", g_smoke);
    display_mgr_register(7, "DRIVE", g_drive);

    dispatch_register(MSG_PLAT_ACK,  taxi_handle_plat_ack);
    dispatch_register(MSG_BUZZER,    taxi_handle_buzzer);
    dispatch_register(MSG_AUTH_RESP, taxi_handle_auth_resp);
    dispatch_register(MSG_FATIGUE,   taxi_handle_fatigue);

    hal_servo_angle(st->servo_fd, st->cfg.door_close_angle);   /* 初始关门 */
    return 0;
}
