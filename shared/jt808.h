#ifndef JT808_H
#define JT808_H
/* 消息号：0x0200/0x8001/0x0001/0x8811 严格按《JH车载定位数据协议_V0.1》 */
#define MSG_TERM_ACK   0x0001   /* 终端通用应答（终端→平台） */
#define MSG_LOCATION   0x0200   /* 位置报警信息汇报（终端→平台） */
#define MSG_PLAT_ACK   0x8001   /* 平台通用应答（平台→终端） */
#define MSG_BUZZER     0x8811   /* 平台下发蜂鸣器控制（自定义） */
/* 以下为小组自定义扩展消息（身份验证/疲劳驾驶） */
#define MSG_AUTH_REQ   0x0210
#define MSG_AUTH_RESP  0x8110
#define MSG_FATIGUE    0x8210

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
/* serial_out 可为 NULL；输出收到帧消息头中的流水号（应答时要用） */
int  jt808_decode(jt808_decoder_t *d, const unsigned char *data, int n,
                  unsigned short *msg_id, unsigned char *phone, unsigned char *term_id,
                  unsigned char *body, int *body_len, unsigned short *serial_out);
#endif
