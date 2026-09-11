#include <string.h>
#include "jt808.h"

static unsigned char xor_checksum(const unsigned char *p, int size) {
    unsigned char x = p[0];
    for (int i = 1; i < size; i++) x ^= p[i];
    return x;
}

int jt808_build(unsigned char *out, int out_cap, unsigned short msg_id,
                const unsigned char phone[6], const unsigned char term_id[6],
                unsigned short *serial, const unsigned char *body, int body_len)
{
    unsigned char raw[2048];
    int raw_len, i, j;
    unsigned char x;
    if (body_len < 0 || body_len > 1023) return -1;
    raw[0] = 0x7E;
    raw[1] = msg_id >> 8;
    raw[2] = msg_id & 0xFF;
    raw[3] = (body_len >> 8) & 0x03;   /* 高字节低2位为长度高字节，分包标志为0 */
    raw[4] = body_len & 0xFF;
    memcpy(raw + 5,  phone, 6);        /* 终端手机号 */
    memcpy(raw + 11, term_id, 6);      /* 终端ID */
    raw[17] = (*serial) >> 8;          /* 流水号 */
    raw[18] = (*serial) & 0xFF;
    (*serial)++;
    raw[19] = 0; raw[20] = 1;          /* 总包数 = 1 */
    raw[21] = 0; raw[22] = 1;          /* 包序号 = 1 */
    if (body_len) memcpy(raw + 23, body, body_len);

    /* 校验：从消息ID(下标1)到消息体末尾，共 22 + body_len 字节 */
    x = xor_checksum(raw + 1, 22 + body_len);
    raw[23 + body_len] = x;
    raw[24 + body_len] = 0x7E;
    raw_len = 25 + body_len;

    /* 转义（头尾 0x7E 不转义） */
    out[0] = raw[0];
    j = 1;
    for (i = 1; i < raw_len - 1; i++) {
        if (raw[i] == 0x7E || raw[i] == 0x7D) {
            if (j + 2 > out_cap) return -1;
            out[j++] = 0x7D;
            out[j++] = (raw[i] == 0x7E) ? 0x02 : 0x01;
        } else {
            if (j + 1 > out_cap) return -1;
            out[j++] = raw[i];
        }
    }
    if (j + 1 > out_cap) return -1;
    out[j++] = raw[raw_len - 1];
    return j;
}

void jt808_decoder_init(jt808_decoder_t *d) {
    memset(d, 0, sizeof(*d));
}

/* 处理一帧（去转义后）。返回：1 成功、0 数据不足、-1 校验/长度错误 */
static int jt808_process(jt808_decoder_t *d, unsigned short *msg_id,
                         unsigned char *phone, unsigned char *term_id,
                         unsigned char *body, int *body_len)
{
    int blen;
    if (d->len < 23) return 0;                 /* 22 头 + 1 校验 */
    blen = d->len - 23;
    if (((d->buf[2] & 0x03) << 8 | d->buf[3]) != blen) { d->len = 0; d->in_frame = 0; d->esc = 0; return -1; }
    if (xor_checksum(d->buf, d->len - 1) != d->buf[d->len - 1]) { d->len = 0; d->in_frame = 0; d->esc = 0; return -1; }
    *msg_id = (d->buf[0] << 8) | d->buf[1];
    memcpy(phone,   d->buf + 4,  6);
    memcpy(term_id, d->buf + 10, 6);
    if (body && blen) memcpy(body, d->buf + 22, blen);
    if (body_len) *body_len = blen;
    d->len = 0; d->in_frame = 0; d->esc = 0;
    return 1;
}

int jt808_decode(jt808_decoder_t *d, const unsigned char *data, int n,
                 unsigned short *msg_id, unsigned char *phone, unsigned char *term_id,
                 unsigned char *body, int *body_len)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned char c = data[i];
        if (!d->in_frame) {
            if (c == 0x7E) { d->in_frame = 1; d->len = 0; d->esc = 0; }
            continue;
        }
        if (d->esc) {
            d->esc = 0;
            if (d->len >= (int)sizeof(d->buf)) { d->len = 0; d->in_frame = 0; return -1; }
            d->buf[d->len++] = (c == 0x02) ? 0x7E : (c == 0x01) ? 0x7D : c;
            continue;
        }
        if (c == 0x7D) { d->esc = 1; continue; }
        if (c == 0x7E) {                      /* 帧尾 */
            int r = jt808_process(d, msg_id, phone, term_id, body, body_len);
            if (r != 0) return r;             /* 1 或 -1 */
            continue;
        }
        d->buf[d->len++] = c;
        if (d->len >= (int)sizeof(d->buf)) { d->len = 0; d->in_frame = 0; return -1; }
    }
    return 0;
}
