#ifndef JT808_H
#define JT808_H
#define MSG_LOCATION   0x0200
#define MSG_AUTH_REQ   0x0210
#define MSG_AUTH_RESP  0x8110
#define MSG_FATIGUE    0x8210
#define MSG_COMMON_ACK 0x8001

#define JT808_PHONE_LEN 6
#define JT808_TERM_LEN  6

typedef struct {
    unsigned char buf[2048];   /* 去转义后的帧缓冲 */
    int len;                   /* 已存字节数 */
    int in_frame;              /* 是否已收到帧头 */
    int esc;                   /* 上一个字节是 0x7D */
} jt808_decoder_t;

int  jt808_build(unsigned char *out, int out_cap, unsigned short msg_id,
                 const unsigned char phone[6], const unsigned char term_id[6],
                 unsigned short *serial, const unsigned char *body, int body_len);
void jt808_decoder_init(jt808_decoder_t *d);
int  jt808_decode(jt808_decoder_t *d, const unsigned char *data, int n,
                  unsigned short *msg_id, unsigned char *phone, unsigned char *term_id,
                  unsigned char *body, int *body_len);
#endif
