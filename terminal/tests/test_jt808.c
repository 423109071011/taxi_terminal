#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "jt808.h"

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

    /* 解码回读 */
    jt808_decoder_t d; jt808_decoder_init(&d);
    unsigned short mid = 0; unsigned char ph[6]={0}, tm[6]={0}, rb[64]={0}; int rblen = 0;
    int r = jt808_decode(&d, out, n, &mid, ph, tm, rb, &rblen);
    assert(r == 1);
    assert(mid == MSG_LOCATION);
    assert(memcmp(ph, phone, 6) == 0);
    assert(memcmp(tm, term, 6) == 0);
    assert(rblen == 5 && memcmp(rb, body, 5) == 0);

    /* 转义正确性：消息体含 0x7E/0x7D */
    unsigned char body2[2] = {0x7E, 0x7D};
    n = jt808_build(out, sizeof(out), MSG_FATIGUE, phone, term, &serial, body2, 2);
    assert(n > 0);
    jt808_decoder_init(&d);
    r = jt808_decode(&d, out, n, &mid, ph, tm, rb, &rblen);
    assert(r == 1 && mid == MSG_FATIGUE && rblen == 2 && rb[0]==0x7E && rb[1]==0x7D);

    /* 校验错误：翻转一个中间字节 */
    out[5] ^= 0xFF;
    jt808_decoder_init(&d);
    r = jt808_decode(&d, out, n, &mid, ph, tm, rb, &rblen);
    assert(r == -1);

    printf("test_jt808 PASS\n");
    return 0;
}
