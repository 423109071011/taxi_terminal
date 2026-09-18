#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cfg.h"

static void trim(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
}

static void set_defaults(app_config_t *c) {
    memset(c, 0, sizeof(*c));
    strcpy(c->server_ip, "192.168.1.100");
    c->server_port = 8888;
    strcpy(c->terminal_id, "091111222200");
    c->phone_id[0] = '\0';             /* 未配置时用终端ID代替（见 net_task.c） */
    c->password_len = 6;
    c->door_open_angle = 90;
    c->door_close_angle = 0;
}

int cfg_load(app_config_t *c, const char *path) {
    FILE *f;
    char line[256];
    set_defaults(c);
    f = fopen(path, "r");
    if (!f) return 0;                 /* 文件缺失用默认值（返回 0 供调用方告警） */
    while (fgets(line, sizeof(line), f)) {
        char *eq, *k, *v;
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        k = line; v = eq + 1; trim(k); trim(v);
        if      (!strcmp(k, "server_ip"))        strncpy(c->server_ip, v, 31);
        else if (!strcmp(k, "server_port"))      c->server_port = atoi(v);
        else if (!strcmp(k, "terminal_id"))      strncpy(c->terminal_id, v, 12);
        else if (!strcmp(k, "phone_id"))         strncpy(c->phone_id, v, 12);
        else if (!strcmp(k, "password_len"))     c->password_len = atoi(v);
        else if (!strcmp(k, "door_open_angle"))  c->door_open_angle = atoi(v);
        else if (!strcmp(k, "door_close_angle")) c->door_close_angle = atoi(v);
    }
    fclose(f);
    return 1;                         /* 配置文件加载成功 */
}
