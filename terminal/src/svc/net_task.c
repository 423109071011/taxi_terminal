#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "app.h"
#include "hal.h"
#include "dispatch.h"
#include "jt808.h"
#include "taxi.h"

static int term_id_from_text(const char *txt, unsigned char out[6]) {
    /* 12 个 BCD 数字符 -> 6 字节 */
    for (int i = 0; i < 6; i++) {
        unsigned int v = 0;
        if (sscanf(txt + i * 2, "%2x", &v) != 1) return -1;
        out[i] = (unsigned char)v;
    }
    return 0;
}

int app_term_id_from_text(app_state *st) {
    if (term_id_from_text(st->cfg.terminal_id, st->term_id) != 0) return -1;
    /* 手机号未单独配置时（空），用终端ID 代替 */
    if (st->cfg.phone_id[0] == '\0')
        strcpy(st->cfg.phone_id, st->cfg.terminal_id);
    return term_id_from_text(st->cfg.phone_id, st->phone_id);
}

void *net_task_run(void *arg) {
    app_state *st = (app_state *)arg;
    unsigned char rbuf[1024];
    jt808_decoder_t dec;
    jt808_decoder_init(&dec);

    for (;;) {
        int fd = hal_net_connect(st->cfg.server_ip, st->cfg.server_port);
        if (fd < 0) { usleep(1000000); continue; }
        printf("connected to platform %s:%d\n", st->cfg.server_ip, st->cfg.server_port);
        pthread_mutex_lock(&st->lock); st->net_ok = 1; st->net_fd = fd; pthread_mutex_unlock(&st->lock);

        /* 协议：连接建立后（实验免注册鉴权）立即上报一帧 0x0200 */
        taxi_report_location(st);

        while (1) {
            int n = hal_net_recv(fd, rbuf, sizeof(rbuf));
            if (n <= 0) break;
            /* 逐字节喂入流式解码器，支持单次 recv 内多帧 */
            for (int i = 0; i < n; i++) {
                unsigned short mid; unsigned char ph[6], tm[6], body[512]; int blen;
                unsigned short seq = 0;
                int r = jt808_decode(&dec, rbuf + i, 1, &mid, ph, tm, body, &blen, &seq);
                if (r == 1) {
                    dispatch_handle(mid, body, blen);
                    /* 协议：终端收到平台下发的所有消息需回终端通用应答 0x0001
                     *（0x8000~0x9FFF 为平台下发区间） */
                    if (mid >= 0x8001 && mid <= 0x9FFF)
                        taxi_send_term_ack(st, seq, mid, 0);
                }
                else if (r == -1) jt808_decoder_init(&dec);
            }
        }
        pthread_mutex_lock(&st->lock); st->net_ok = 0; st->net_fd = -1; pthread_mutex_unlock(&st->lock);
        close(fd);
        usleep(2000000);
    }
    return NULL;
}
