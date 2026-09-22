#ifndef APP_H
#define APP_H
#include <pthread.h>
#include <time.h>
#include "cfg.h"
#include "hal.h"

typedef struct {
    pthread_mutex_t lock;
    app_config_t cfg;
    unsigned char term_id[6];       /* 由 terminal_id 文本转 BCD */
    unsigned char phone_id[6];      /* 终端手机号 BCD（消息头用） */
    unsigned short serial;
    /* 设备句柄 */
    int net_fd;                     /* 当前网络套接字，net_task 维护 */
    int key_fd, display_fd, rfid_fd, beep_fd, servo_fd, gps_fd, adc_fd;
    /* 业务状态 */
    unsigned char card[4]; int has_card;
    time_t card_time;               /* 刷卡时刻：0x0210 确认时距此超 30s 视为刷卡过期 */
    int door_open;
    int verified;                   /* 0 未验证 / 1 准许 / -1 拒绝 */
    int fatigue;
    int smoke_alarm;                /* 烟雾超标报警状态（滞回+连续确认后） */
    int smoke_val;                  /* 烟雾 ADC 最新原始值 0~4095（info 显示用） */
    time_t drive_start;             /* 疲劳驾驶计时起点：平台认证 PASS 开门后开始，0=未在计时 */
    gps_fix_t gps;
    int net_ok;
    /* 身份码输入 */
    unsigned char auth_buf[8]; int auth_len;
    /* 显示 */
    int disp_idx;                   /* 当前翻页条目 */
    time_t last_key;                /* 最后按键时间（轮播判断） */
} app_state;

int  app_term_id_from_text(app_state *st);   /* 文本 -> BCD */
#endif
