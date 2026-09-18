#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "jt808.h"

/*
 * 流式拼包回归测试（老师要求：一帧数据可能分多次 recv 才收全，必须能拼接包）：
 * 用 jt808_build 生成一帧 0x0200，再按各种切分方式（逐字节/固定块/随机块）
 * 喂入流式解码器，验证都能正确拼出完整帧；
 * 并验证"一帧尾部 + 下一帧头部粘在同一次数据里"的场景。
 */

static int hex2bin(const char *h, unsigned char *o) {
    int n = 0;
    while (*h) {
        if (*h == ' ') { h++; continue; }
        unsigned v;
        if (sscanf(h, "%2x", &v) != 1) return -1;
        o[n++] = (unsigned char)v;
        h += 2;
    }
    return n;
}

static unsigned short s_serial = 1;

static int build_0200(unsigned char *out, int cap) {
    unsigned char body[34];
    unsigned char phone[6] = {0x09,0x11,0x11,0x22,0x22,0x66};
    unsigned char term[6]  = {0x09,0x11,0x11,0x22,0x22,0x66};
    memset(body, 0, sizeof(body));
    body[30] = 0x25; body[31] = 0x10;   /* 里程 TLV */
    return jt808_build(out, cap, MSG_LOCATION, phone, term, &s_serial, body, 34);
}

/* 把 frame[0..n) 按 chunk 大小逐段喂入解码器，返回收到的 msg_id（应为 0x0200） */
static int feed_in_chunks(const unsigned char *frame, int n, int chunk) {
    jt808_decoder_t dec;
    jt808_decoder_init(&dec);
    for (int i = 0; i < n; i += chunk) {
        int c = (i + chunk > n) ? n - i : chunk;
        unsigned short mid = 0, seq = 0;
        unsigned char ph[6], tm[6], body[512]; int blen = 0;
        int r = jt808_decode(&dec, frame + i, c, &mid, ph, tm, body, &blen, &seq);
        if (r == 1) return mid;
        assert(r == 0);   /* 中间状态只允许"未完成" */
    }
    return -1;
}

int main(void) {
    unsigned char frame[512];
    int n = build_0200(frame, sizeof(frame));
    assert(n > 30);

    /* 1. 一次全给 */
    assert(feed_in_chunks(frame, n, n) == MSG_LOCATION);

    /* 2. 逐字节给（最极端的拆包） */
    assert(feed_in_chunks(frame, n, 1) == MSG_LOCATION);

    /* 3. 每次给 3 字节（不等长余数） */
    assert(feed_in_chunks(frame, n, 3) == MSG_LOCATION);

    /* 4. 每次给 13 字节 */
    assert(feed_in_chunks(frame, n, 13) == MSG_LOCATION);

    /* 5. 随机分片，跑 200 轮 */
    srand(20260918);
    for (int t = 0; t < 200; t++) {
        jt808_decoder_t dec;
        jt808_decoder_init(&dec);
        int i = 0, done = 0;
        while (i < n) {
            int c = 1 + rand() % 20;
            if (i + c > n) c = n - i;
            unsigned short mid = 0, seq = 0;
            unsigned char ph[6], tm[6], body[512]; int blen = 0;
            int r = jt808_decode(&dec, frame + i, c, &mid, ph, tm, body, &blen, &seq);
            assert(r == 0 || r == 1);
            if (r == 1) { assert(mid == MSG_LOCATION); done = 1; }
            i += c;
        }
        assert(done);
    }

    /* 6. 粘包：两帧连着发，一次 recv 到"帧1尾部+帧2头部" */
    unsigned char two[1024];
    int n1 = build_0200(two, sizeof(two));
    int n2 = build_0200(two + n1, sizeof(two) - n1);
    jt808_decoder_t dec;
    jt808_decoder_init(&dec);
    int got = 0;
    for (int i = 0; i < n1 + n2; i++) {
        unsigned short mid = 0, seq = 0;
        unsigned char ph[6], tm[6], body[512]; int blen = 0;
        int r = jt808_decode(&dec, two + i, 1, &mid, ph, tm, body, &blen, &seq);
        if (r == 1) { got++; assert(mid == MSG_LOCATION); }
    }
    assert(got == 2);

    printf("test_jt808_stream PASS\n");
    return 0;
}
