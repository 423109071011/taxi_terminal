#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "jt808.h"

/* 《数据指令组帧举例1》老师的标准答案帧（回归基准） */
static const char *TEACHER_0200 =
    "7E020000220911112222660911112222660001000000000000000000000000"
    "0245C97006D5AC07000000000000251023110001010400000000A47E";
static const char *TEACHER_8001 =
    "7E800100050911112222660911112222667A2A000000000001020000D77E";
static const char *TEACHER_8811_ON =
    "7E8811000109111122226609111122226600010000000001 98 7E"; /* 去空格后比较 */
static const char *TEACHER_TERM_ACK =
    "7E0001000509111122226609111122226600060000000000018811009A7E";

static int hex2bin(const char *h, unsigned char *o) {
    int n = 0;
    while (*h) {
        if (*h == ' ') { h++; continue; }
        unsigned v; sscanf(h, "%2x", &v);
        o[n++] = (unsigned char)v; h += 2;
    }
    return n;
}

int main(void)
{
    unsigned char out[256];
    unsigned char phone[6] = {0x01,0x34,0x56,0x78,0x90,0x12};
    unsigned char term[6]  = {0x09,0x11,0x11,0x22,0x22,0x00};
    unsigned char body[5]  = {0x01,0x02,0x03,0x04,0x05};
    unsigned short serial = 1;

    int n = jt808_build(out, sizeof(out), MSG_LOCATION, phone, term, &serial, body, 5);
    assert(n > 0);
    assert(out[0] == 0x7E && out[n-1] == 0x7E);

    /* 解码回读（含流水号） */
    jt808_decoder_t d; jt808_decoder_init(&d);
    unsigned short mid = 0, rseq = 0;
    unsigned char ph[6]={0}, tm[6]={0}, rb[64]={0}; int rblen = 0;
    int r = jt808_decode(&d, out, n, &mid, ph, tm, rb, &rblen, &rseq);
    assert(r == 1);
    assert(mid == MSG_LOCATION);
    assert(memcmp(ph, phone, 6) == 0);
    assert(memcmp(tm, term, 6) == 0);
    assert(rseq == 1);
    assert(rblen == 5 && memcmp(rb, body, 5) == 0);

    /* 转义正确性：消息体含 0x7E/0x7D */
    unsigned char body2[2] = {0x7E, 0x7D};
    n = jt808_build(out, sizeof(out), MSG_FATIGUE, phone, term, &serial, body2, 2);
    assert(n > 0);
    jt808_decoder_init(&d);
    r = jt808_decode(&d, out, n, &mid, ph, tm, rb, &rblen, &rseq);
    assert(r == 1 && mid == MSG_FATIGUE && rblen == 2 && rb[0]==0x7E && rb[1]==0x7D);

    /* 校验错误：翻转一个中间字节 */
    out[5] ^= 0xFF;
    jt808_decoder_init(&d);
    r = jt808_decode(&d, out, n, &mid, ph, tm, rb, &rblen, &rseq);
    assert(r == -1);

    /* ===== 回归：组帧结果必须与老师 0x0200 标准帧逐字节一致 ===== */
    {
        unsigned char expect[128], got[128];
        int elen = hex2bin(TEACHER_0200, expect);
        /* 老师 0x0200 帧：报警=0 状态=0 纬=0x0245C970 经=0x06D5AC07
           时间 251023110001（BCD） 附加里程 TLV(01 04 00000000) */
        unsigned char tb[34] = {0};
        const unsigned char latb[4] = {0x02,0x45,0xC9,0x70};
        const unsigned char lonb[4] = {0x06,0xD5,0xAC,0x07};
        const unsigned char timb[6] = {0x25,0x10,0x23,0x11,0x00,0x01};
        memcpy(tb+8,  latb, 4);
        memcpy(tb+12, lonb, 4);
        memcpy(tb+22, timb, 6);   /* 时间BCD在消息体偏移22（文档表格起始21系笔误） */
        tb[28]=0x01; tb[29]=0x04; /* 里程 TLV，值 0 */
        unsigned char tphone[6] = {0x09,0x11,0x11,0x22,0x22,0x66};
        unsigned short tseq = 1;
        int gn = jt808_build(got, sizeof(got), MSG_LOCATION, tphone, tphone, &tseq, tb, 34);
        assert(gn == elen);
        assert(memcmp(got, expect, elen) == 0);
        printf("regression 0x0200 vs teacher frame: MATCH\n");
    }

    /* ===== 回归：解析老师 0x8001 平台通用应答帧 ===== */
    {
        unsigned char f[128];
        int flen = hex2bin(TEACHER_8001, f);
        jt808_decoder_init(&d);
        r = jt808_decode(&d, f, flen, &mid, ph, tm, rb, &rblen, &rseq);
        assert(r == 1 && mid == MSG_PLAT_ACK);
        assert(rseq == 0x7A2A);
        assert(rblen == 5);
        assert(rb[0]==0x00 && rb[1]==0x01 && rb[2]==0x02 && rb[3]==0x00 && rb[4]==0x00);
        printf("regression 0x8001 teacher frame: MATCH\n");
    }

    /* ===== 回归：解析老师 0x8811 蜂鸣器控制帧（含数据域 0x01） ===== */
    {
        unsigned char f[128];
        int flen = hex2bin("7E8811000109111122226609111122226600010000000001987E", f);
        jt808_decoder_init(&d);
        r = jt808_decode(&d, f, flen, &mid, ph, tm, rb, &rblen, &rseq);
        assert(r == 1 && mid == MSG_BUZZER);
        assert(rseq == 1 && rblen == 1 && rb[0] == 0x01);
        printf("regression 0x8811 teacher frame: MATCH\n");
    }

    /* ===== 回归：解析老师 0x0001 终端通用应答帧 ===== */
    {
        unsigned char f[128];
        int flen = hex2bin(TEACHER_TERM_ACK, f);
        jt808_decoder_init(&d);
        r = jt808_decode(&d, f, flen, &mid, ph, tm, rb, &rblen, &rseq);
        assert(r == 1 && mid == MSG_TERM_ACK);
        assert(rseq == 6 && rblen == 5);
        assert(rb[2]==0x88 && rb[3]==0x11 && rb[4]==0x00);
        printf("regression 0x0001 teacher frame: MATCH\n");
    }

    (void)TEACHER_8811_ON;
    printf("test_jt808 PASS\n");
    return 0;
}
