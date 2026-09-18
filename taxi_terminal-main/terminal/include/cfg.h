#ifndef CFG_H
#define CFG_H
typedef struct {
    char server_ip[32];
    int  server_port;
    char terminal_id[13];   /* 6 字节 BCD，文本 12 字符 + \0 */
    char phone_id[13];      /* 终端手机号 BCD[6]，缺省同 terminal_id */
    int  password_len;
    int  door_open_angle;
    int  door_close_angle;
} app_config_t;
int cfg_load(app_config_t *c, const char *path);
#endif
