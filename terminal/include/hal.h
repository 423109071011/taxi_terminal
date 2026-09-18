#ifndef HAL_H
#define HAL_H
/* key */
int hal_key_open(void);
int hal_key_read(int fd, unsigned int *code);
/* display */
int hal_display_open(void);
int hal_display_clear(int fd);
int hal_display_string(int fd, const char *s);
int hal_display_char(char c);
int hal_display_number(int fd, long v, int width);
int hal_rfid_open(void);
int hal_rfid_read(int fd, unsigned char card[4]);
int hal_led_open(void);
void hal_led_set(int fd, char color, int on);
int hal_beep_open(void);
void hal_beep_on(int fd);
void hal_beep_off(int fd);
int hal_servo_open(void);
void hal_servo_angle(int fd, int angle);
/* GPS：GGA 取时间/经纬度/星数，RMC 取日期（协议 0x0200 时间字段要用 UTC 日期+时间） */
typedef struct {
    double lat, lon;
    int year, month, day;      /* 来自 RMC（UTC 日期） */
    int hour, min, sec, sats, status;
} gps_fix_t;
int hal_gps_open(void);
int hal_gps_parse(const char *buf, int len, gps_fix_t *fix);
int hal_net_connect(const char *ip, int port);
int hal_net_send(int fd, const unsigned char *b, int n);
int hal_net_recv(int fd, unsigned char *b, int cap);
#endif
