#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "cfg.h"

int main(void)
{
    app_config_t c;
    /* 默认值测试：文件不存在时仍返回默认 */
    assert(cfg_load(&c, "/nonexistent/xxx.cfg") == 0);
    assert(strcmp(c.server_ip, "192.168.1.100") == 0);
    assert(c.server_port == 8888);
    assert(c.password_len == 6);
    assert(c.door_open_angle == 90);
    assert(c.door_close_angle == 0);

    /* 解析测试 */
    FILE *f = fopen("/tmp/cfgtest.cfg", "w");
    fprintf(f, "# comment\nserver_ip=10.0.0.5\nserver_port=9000\npassword_len=8\n"
               "door_open_angle=120\ndoor_close_angle=10\n");
    fclose(f);
    assert(cfg_load(&c, "/tmp/cfgtest.cfg") == 0);
    assert(strcmp(c.server_ip, "10.0.0.5") == 0);
    assert(c.server_port == 9000);
    assert(c.password_len == 8);
    assert(c.door_open_angle == 120);
    assert(c.door_close_angle == 10);
    printf("test_cfg PASS\n");
    return 0;
}
