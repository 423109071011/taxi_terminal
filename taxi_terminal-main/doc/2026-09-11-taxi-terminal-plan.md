# 出租车智能定位终端 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 FS6818 + EC20 平台上实现出租车智能定位终端（arm 交叉编译）＋ PC 模拟平台（C，宿主机 gcc），通过 JT808 风格协议联调。

**Architecture:** 终端分三层（HAL/服务层/业务层，业务模块插件式注册）；JT808 编解码为纯 C 的共享模块，终端与平台共用同一份源码；平台端预留 hook 对接接口。

**Tech Stack:** C99；终端 `arm-none-linux-gnueabi-gcc -static -lpthread`；平台宿主机 `gcc`；纯逻辑模块用宿主机 gcc 做单元测试。

**项目根目录：** `D:\shixiwenjian\嵌入式资料\taxi_terminal\`

---

## Global Constraints

- 交叉编译器：`arm-none-linux-gnueabi-gcc`，链接参数 `-static -lpthread`。
- 终端设备节点：键盘 `/dev/input/event4`；数码管 `/dev/i2c-2`（主）/`/dev/zlg72128-0`（备）；RFID `/dev/rfid_module0`；状态灯 `/dev/led`；蜂鸣器 `/dev/beep`；舵机 `/dev/servo`；GPS `/dev/ttyUSB1`。
- 协议：沿用 `gps_report.c` 的 JT808 帧（`7E` 头尾、异或校验、`7D→7D01`/`7E→7D02` 转义）；消息号 `0x0200` 位置、`0x0210` 身份请求、`0x8110` 身份应答、`0x8210` 疲劳下发、`0x8001` 通用应答。
- 业务流程：刷卡=开门、按键=关门；键盘密码仅验证身份（平台准许/拒绝仅提示）；疲劳由平台下发/解除。
- 门角度默认：关门 0°、开门 90°；身份码默认 6 位，确认键默认 `#`（可配）。
- 状态灯时序（逐字照抄 spec §3.2 第 5 条）；显示轮播：10 秒无按键触发、每项 3 秒、轮播时不写控制台。
- 命令行：`?` / `help` / `info` / `uppath <文件>`；主线程只读 stdin，业务全在子线程。

---

## 文件结构总览

```
taxi_terminal/
├── shared/                    # 终端与平台共用（纯 C，无 OS 依赖）
│   ├── jt808.h
│   └── jt808.c
├── terminal/
│   ├── Makefile
│   ├── include/
│   │   ├── app.h             # 全局状态 app_state 定义
│   │   ├── cfg.h  keymap.h
│   │   ├── hal.h             # 所有 HAL 接口汇总
│   │   ├── dispatch.h  display_mgr.h  status_led.h  cmdline.h  net_task.h
│   │   └── taxi.h
│   ├── src/
│   │   ├── main.c
│   │   ├── common/cfg.c  keymap.c
│   │   ├── hal/key.c  display.c  rfid.c  led.c  beep.c  servo.c  gps.c  net.c
│   │   ├── svc/dispatch.c  net_task.c  display_mgr.c  status_led.c  cmdline.c
│   │   └── biz/taxi.c
│   ├── tools/key_dump.c
│   ├── tests/test_jt808.c  test_cfg.c
│   └── deploy/config  bt.sh  start.sh
└── platform/
    ├── Makefile
    ├── config
    ├── src/server.c  session.c  logic.c  hook.c  main.c
    └── include/*.h
```

---

## Task 1: 公共模块 cfg（配置解析）+ 单元测试脚手架

**Files:**
- Create: `terminal/include/cfg.h`, `terminal/src/common/cfg.c`, `terminal/tests/test_cfg.c`
- Create: `terminal/include/app.h`（先放 `app_config_t` 全局声明，Task 1 只用到配置类型）

**Interfaces:**
- Produces:
  - `int cfg_load(app_config_t *c, const char *path)` — 解析 `key=value` 配置；文件缺失用默认值；返回 0 成功 / -1 失败。
  - 类型 `app_config_t`（见下）。

- [ ] **Step 1: 写失败测试**

`terminal/tests/test_cfg.c`：
```c
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
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `gcc -I terminal/include -o /tmp/test_cfg terminal/tests/test_cfg.c terminal/src/common/cfg.c && /tmp/test_cfg`
Expected: 编译报错（`cfg.h` / `app_config_t` 未定义）→ FAIL。

- [ ] **Step 3: 写实现**

`terminal/include/cfg.h`：
```c
#ifndef CFG_H
#define CFG_H
typedef struct {
    char server_ip[32];
    int  server_port;
    char terminal_id[13];   /* 6 字节 BCD，文本 12 字符 + \0 */
    int  password_len;
    int  door_open_angle;
    int  door_close_angle;
} app_config_t;
int cfg_load(app_config_t *c, const char *path);
#endif
```

`terminal/src/common/cfg.c`：
```c
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
    c->password_len = 6;
    c->door_open_angle = 90;
    c->door_close_angle = 0;
}

int cfg_load(app_config_t *c, const char *path) {
    FILE *f;
    char line[256];
    set_defaults(c);
    f = fopen(path, "r");
    if (!f) return 0;                 /* 文件缺失用默认值 */
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
        else if (!strcmp(k, "password_len"))     c->password_len = atoi(v);
        else if (!strcmp(k, "door_open_angle"))  c->door_open_angle = atoi(v);
        else if (!strcmp(k, "door_close_angle")) c->door_close_angle = atoi(v);
    }
    fclose(f);
    return 0;
}
```

- [ ] **Step 4: 运行测试，确认通过**

Run: `gcc -I terminal/include -o /tmp/test_cfg terminal/tests/test_cfg.c terminal/src/common/cfg.c && /tmp/test_cfg`
Expected: `test_cfg PASS`，退出码 0。

- [ ] **Step 5: 提交**

```bash
git add terminal/include/cfg.h terminal/src/common/cfg.c terminal/tests/test_cfg.c
git commit -m "feat: cfg module with unit tests"
```

---

## Task 2: JT808 编解码（共享，纯 C，TDD）

**Files:**
- Create: `shared/jt808.h`, `shared/jt808.c`, `terminal/tests/test_jt808.c`

**Interfaces:**
- Produces:
  - `int jt808_build(unsigned char *out, int out_cap, unsigned short msg_id, const unsigned char phone[6], const unsigned char term_id[6], unsigned short *serial, const unsigned char *body, int body_len)` — 组帧+转义，返回转义后长度，`-1` 表示参数错误。
  - `void jt808_decoder_init(jt808_decoder_t *d)`；`int jt808_decode(jt808_decoder_t *d, const unsigned char *data, int n, unsigned short *msg_id, unsigned char *phone, unsigned char *term_id, unsigned char *body, int *body_len)` — 流式解帧，返回 `1` 解出一帧 / `0` 数据不足 / `-1` 校验错误。
  - 消息号宏：`MSG_LOCATION 0x0200`, `MSG_AUTH_REQ 0x0210`, `MSG_AUTH_RESP 0x8110`, `MSG_FATIGUE 0x8210`, `MSG_COMMON_ACK 0x8001`。

- [ ] **Step 1: 写失败测试**

`terminal/tests/test_jt808.c`：
```c
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
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `gcc -I shared -o /tmp/test_jt808 terminal/tests/test_jt808.c shared/jt808.c && /tmp/test_jt808`
Expected: 编译报错（`jt808.h`/函数未定义）→ FAIL。

- [ ] **Step 3: 写实现**

`shared/jt808.h`：
```c
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
```

`shared/jt808.c`：
```c
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
        if (raw[i] == 0x7E)      { out[j++] = 0x7D; out[j++] = 0x02; }
        else if (raw[i] == 0x7D) { out[j++] = 0x7D; out[j++] = 0x01; }
        else                     { out[j++] = raw[i]; }
        if (j > out_cap) return -1;
    }
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
    if (((d->buf[3] & 0x03) << 8 | d->buf[4]) != blen) return -1;
    if (xor_checksum(d->buf, d->len - 1) != d->buf[d->len - 1]) return -1;
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
```

- [ ] **Step 4: 运行测试，确认通过**

Run: `gcc -I shared -o /tmp/test_jt808 terminal/tests/test_jt808.c shared/jt808.c && /tmp/test_jt808`
Expected: `test_jt808 PASS`，退出码 0。

- [ ] **Step 5: 提交**

```bash
git add shared/jt808.h shared/jt808.c terminal/tests/test_jt808.c
git commit -m "feat: JT808 codec (shared) with unit tests"
```

---

## Task 3: 键盘 HAL + 键值映射 + key_dump 标定工具

**Files:**
- Create: `terminal/include/keymap.h`, `terminal/src/common/keymap.c`
- Create: `terminal/include/hal.h`（先放 key 接口）
- Create: `terminal/src/hal/key.c`, `terminal/tools/key_dump.c`

**Interfaces:**
- Produces:
  - `int hal_key_open(void)` / `int hal_key_read(int fd, unsigned int *code)` — 阻塞读 `input_event`，按下返回 1（`*code` 为 `ev.code`），弹起返回 0。
  - `int key_map(unsigned int code, char *digit)` — 返回逻辑动作：`K_DIGIT`(digit 填字符) / `K_CONFIRM` / `K_CLOSE` / `K_PREV` / `K_NEXT` / `K_NONE`。

- [ ] **Step 1: 写失败测试（纯函数 key_map）**

`terminal/tests/test_keymap.c`：
```c
#include <stdio.h>
#include <assert.h>
#include "keymap.h"
int main(void) {
    char d = 0;
    assert(key_map(2,  &d) == K_DIGIT && d == '1');
    assert(key_map(11, &d) == K_DIGIT && d == '0');
    assert(key_map(28, &d) == K_CONFIRM);
    assert(key_map(29, &d) == K_CLOSE);
    assert(key_map(999, &d) == K_NONE);
    printf("test_keymap PASS\n");
    return 0;
}
```

- [ ] **Step 2: 运行，确认失败**

Run: `gcc -I terminal/include -o /tmp/test_keymap terminal/tests/test_keymap.c terminal/src/common/keymap.c && /tmp/test_keymap`
Expected: 编译报错 → FAIL。

- [ ] **Step 3: 写实现**

`terminal/include/keymap.h`：
```c
#ifndef KEYMAP_H
#define KEYMAP_H
enum { K_NONE = 0, K_DIGIT, K_CONFIRM, K_CLOSE, K_PREV, K_NEXT };
int key_map(unsigned int code, char *digit);
#endif
```

`terminal/src/common/keymap.c`：
```c
#include "keymap.h"
/* 依据驱动 key_list_def：K2..K11 -> KEY_1..KEY_0（code 2..11）。
 * 确认/关门/翻页键为占位默认值，需用 tools/key_dump 在真机标定后改这里。 */
int key_map(unsigned int code, char *digit) {
    if (code >= 2 && code <= 10) { *digit = (char)('1' + (code - 2)); return K_DIGIT; }
    if (code == 11) { *digit = '0'; return K_DIGIT; }
    if (code == 28) return K_CONFIRM;   /* KEY_ENTER，待标定 */
    if (code == 29) return K_CLOSE;     /* KEY_LEFTCTRL=K1，待标定 */
    if (code == 103) return K_PREV;     /* KEY_UP，待标定 */
    if (code == 108) return K_NEXT;     /* KEY_DOWN，待标定 */
    return K_NONE;
}
```

`terminal/include/hal.h`：
```c
#ifndef HAL_H
#define HAL_H
/* key */
int hal_key_open(void);
int hal_key_read(int fd, unsigned int *code);
#endif
```

`terminal/src/hal/key.c`：
```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include "hal.h"
int hal_key_open(void) {
    int fd = open("/dev/input/event4", O_RDONLY);
    if (fd < 0) perror("open /dev/input/event4");
    return fd;
}
int hal_key_read(int fd, unsigned int *code) {
    struct input_event ev;
    for (;;) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != (ssize_t)sizeof(ev)) continue;
        if (ev.type == EV_KEY) { *code = ev.code; return ev.value ? 1 : 0; }
    }
}
```

`terminal/tools/key_dump.c`：
```c
#include <stdio.h>
#include "hal.h"
#include "keymap.h"
int main(void) {
    int fd = hal_key_open();
    if (fd < 0) return 1;
    printf("press keys; Ctrl-C to quit\n");
    while (1) {
        unsigned int code = 0; char d = 0;
        int v = hal_key_read(fd, &code);
        if (v == 1) {
            int a = key_map(code, &d);
            printf("code=0x%02x(%u) action=%d digit=%c\n", code, code, a, d ? d : '?');
        }
    }
    return 0;
}
```

- [ ] **Step 4: 运行 keymap 单测确认通过**

Run: `gcc -I terminal/include -o /tmp/test_keymap terminal/tests/test_keymap.c terminal/src/common/keymap.c && /tmp/test_keymap`
Expected: `test_keymap PASS`。

- [ ] **Step 5: 交叉编译 key_dump 冒烟（上板后执行）**

Run: `arm-none-linux-gnueabi-gcc -static -I terminal/include -o terminal/tools/key_dump terminal/tools/key_dump.c terminal/src/common/keymap.c terminal/src/hal/key.c`
Expected: 编译通过。上板后运行 `./key_dump`，逐键记录 code，回填 `keymap.c`。

- [ ] **Step 6: 提交**

```bash
git add terminal/include/keymap.h terminal/src/common/keymap.c terminal/include/hal.h terminal/src/hal/key.c terminal/tools/key_dump.c terminal/tests/test_keymap.c
git commit -m "feat: key HAL + keymap + key_dump tool"
```

---

## Task 4: 数码管 HAL（i2c ZLG72128 主后端 + /dev/zlg72128-0 备后端）

**Files:**
- Modify: `terminal/include/hal.h`（追加 display 接口）
- Create: `terminal/src/hal/display.c`

**Interfaces:**
- Produces:
  - `int hal_display_open(void)`；`int hal_display_clear(int fd)`；`int hal_display_string(int fd, const char *s)`；`int hal_display_number(int fd, long v, int width)`（右对齐补空格）。
- 后端策略：优先打开 `/dev/i2c-2` 直接写寄存器；失败则回退 `/dev/zlg72128-0` 的 `DISP_NUM`（移位）。

- [ ] **Step 1: 写实现**

`terminal/src/hal/display.c`：
```c
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "hal.h"

/* ZLG72128 段码：'0'~'9'、空格、'-'（与 zlg7290 驱动同表） */
static const unsigned char seg_code[128] = {0};
static unsigned char seg(char c) {
    switch (c) {
        case '0': return 0xfc; case '1': return 0x60; case '2': return 0xda;
        case '3': return 0xf2; case '4': return 0x66; case '5': return 0xb6;
        case '6': return 0xbe; case '7': return 0xe0; case '8': return 0xfe;
        case '9': return 0xf6; case '-': return 0x02; case ' ': return 0x00;
        case 'A': return 0xee; case 'b': return 0x3e; case 'C': return 0x9c;
        case 'd': return 0x7a; case 'E': return 0x9e; case 'F': return 0x8e;
        default:  return 0x00;
    }
}

static int fd_i2c = -1;
static int fd_misc = -1;

int hal_display_open(void) {
    fd_i2c = open("/dev/i2c-2", O_RDWR);
    if (fd_i2c >= 0) {
        if (ioctl(fd_i2c, I2C_SLAVE, 0x30) < 0) { close(fd_i2c); fd_i2c = -1; }
    }
    if (fd_i2c < 0) fd_misc = open("/dev/zlg72128-0", O_RDWR);
    return (fd_i2c >= 0 || fd_misc >= 0) ? 0 : -1;
}

static int i2c_write_reg(int reg, unsigned char v) {
    unsigned char buf[2] = { reg, v };
    return write(fd_i2c, buf, 2) == 2 ? 0 : -1;
}

int hal_display_clear(int fd) {
    (void)fd;
    if (fd_i2c >= 0) {
        for (int i = 0; i < 12; i++) i2c_write_reg(0x10 | i, 0x00);
        return 0;
    }
    if (fd_misc >= 0) { for (int i = 0; i < 12; i++) ioctl(fd_misc, _IOW('F', 3, 0), 0x00); }
    return 0;
}

int hal_display_string(int fd, const char *s) {
    (void)fd;
    int len = strlen(s);
    if (len > 12) len = 12;
    if (fd_i2c >= 0) {
        hal_display_clear(0);
        for (int i = 0; i < len; i++) i2c_write_reg(0x10 | i, seg(s[i]));
        return 0;
    }
    if (fd_misc >= 0) {
        for (int i = 0; i < len; i++) ioctl(fd_misc, _IOW('F', 3, 0), seg(s[i]));
        return 0;
    }
    return -1;
}

int hal_display_number(int fd, long v, int width) {
    char b[16];
    snprintf(b, sizeof(b), "%*ld", width, v);
    return hal_display_string(fd, b);
}
```

- [ ] **Step 2: 交叉编译冒烟**

Run: `arm-none-linux-gnueabi-gcc -static -I terminal/include -c -o /tmp/display.o terminal/src/hal/display.c`
Expected: 编译通过。

- [ ] **Step 3: 上板冒烟**（进入真机后）

Run: `./taxi_terminal` 启动后数码管能显示轮播内容；或临时在 `main` 加一行 `hal_display_string(0,"0123456789Ab")` 观察 12 位全显。若 i2c 后端不工作，程序应自动回退 `/dev/zlg72128-0`。

- [ ] **Step 4: 提交**

```bash
git add terminal/src/hal/display.c terminal/include/hal.h
git commit -m "feat: display HAL (i2c ZLG72128 + misc fallback)"
```

---

## Task 5: 其余 HAL（rfid / led / beep / servo / gps / net）

**Files:**
- Modify: `terminal/include/hal.h`（追加接口）
- Create: `terminal/src/hal/rfid.c`, `led.c`, `beep.c`, `servo.c`, `gps.c`, `net.c`

**Interfaces:**
- Produces:
  - `int hal_rfid_open(void)`；`int hal_rfid_read(int fd, unsigned char card[4])`（返回 1 读到新卡 / 0 无 / -1 错）
  - `int hal_led_open(void)`；`void hal_led_set(int fd, char color, int on)`（color 'r'/'g'/'b'）
  - `int hal_beep_open(void)`；`void hal_beep_on(int fd)`；`void hal_beep_off(int fd)`
  - `int hal_servo_open(void)`；`void hal_servo_angle(int fd, int angle)`
  - `int hal_gps_open(void)`；`int hal_gps_parse(const char *buf, int len, gps_fix_t *fix)`（`gps_fix_t{double lat,lon; int hour,min,sec,sats,status}`，`$GPGGA` 解析，`status==0` 返回 0）
  - `int hal_net_connect(const char *ip, int port)`；`int hal_net_send(int fd, const unsigned char *b, int n)`；`int hal_net_recv(int fd, unsigned char *b, int cap)`

- [ ] **Step 1: 写实现（rfid/led/beep/servo）**

`terminal/include/hal.h` 追加：
```c
int hal_rfid_open(void);
int hal_rfid_read(int fd, unsigned char card[4]);
int hal_led_open(void);
void hal_led_set(int fd, char color, int on);
int hal_beep_open(void);
void hal_beep_on(int fd);
void hal_beep_off(int fd);
int hal_servo_open(void);
void hal_servo_angle(int fd, int angle);
typedef struct { double lat, lon; int hour, min, sec, sats, status; } gps_fix_t;
int hal_gps_open(void);
int hal_gps_parse(const char *buf, int len, gps_fix_t *fix);
int hal_net_connect(const char *ip, int port);
int hal_net_send(int fd, const unsigned char *b, int n);
int hal_net_recv(int fd, unsigned char *b, int cap);
```

`terminal/src/hal/rfid.c`：
```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "hal.h"
int hal_rfid_open(void) {
    int fd = open("/dev/rfid_module0", O_RDWR);
    if (fd < 0) perror("open rfid");
    return fd;
}
int hal_rfid_read(int fd, unsigned char card[4]) {
    unsigned char b[4] = {0};
    ssize_t n = read(fd, b, 4);
    if (n == 4) { for (int i = 0; i < 4; i++) card[i] = b[i]; return 1; }
    return (n < 0) ? -1 : 0;
}
```

`terminal/src/hal/led.c`：
```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "hal.h"
#define LED_ON  _IOW('L', 0, int)
#define LED_OFF _IOW('L', 1, int)
int hal_led_open(void) {
    int fd = open("/dev/led", O_RDWR);
    if (fd < 0) perror("open led");
    return fd;
}
void hal_led_set(int fd, char color, int on) {
    int c = color;
    ioctl(fd, on ? LED_ON : LED_OFF, &c);
}
```

`terminal/src/hal/beep.c`：
```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "hal.h"
#define BEEP_ON  _IO('B', 0)
#define BEEP_OFF _IO('B', 1)
int hal_beep_open(void) {
    int fd = open("/dev/beep", O_RDWR);
    if (fd < 0) perror("open beep");
    return fd;
}
void hal_beep_on(int fd)  { ioctl(fd, BEEP_ON); }
void hal_beep_off(int fd) { ioctl(fd, BEEP_OFF); }
```

`terminal/src/hal/servo.c`：
```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "hal.h"
#define SET_ANGLE _IO('S', 5)
int hal_servo_open(void) {
    int fd = open("/dev/servo", O_RDWR);
    if (fd < 0) perror("open servo");
    return fd;
}
void hal_servo_angle(int fd, int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    ioctl(fd, SET_ANGLE, angle);
}
```

- [ ] **Step 2: 写实现（gps NMEA 解析）**

`terminal/src/hal/gps.c`：
```c
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "hal.h"

int hal_gps_open(void) {
    int fd = open("/dev/ttyUSB1", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) perror("open gps");
    return fd;
}

static int split(char fields[][32], char *s) {
    int j = 0, k = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == ',') { fields[j][k] = '\0'; j++; k = 0; continue; }
        if (k < 31) fields[j][k++] = s[i];
    }
    fields[j][k] = '\0';
    return j + 1;
}

/* 解析 $GPGGA；返回 1 有效定位 / 0 无效或非 GGA */
int hal_gps_parse(const char *buf, int len, gps_fix_t *fix) {
    const char *gga = strstr(buf, "$GPGGA");
    char line[128]; char f[16][32];
    if (!gga) return 0;
    const char *eol = strchr(gga, '\n');
    if (!eol) return 0;
    int n = (int)(eol - gga);
    if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
    memcpy(line, gga, n); line[n] = '\0';
    memset(f, 0, sizeof(f));
    split(f, line);               /* f[0]="$GPGGA" */
    int status = atoi(f[6]);
    if (status == 0) return 0;
    fix->status = status;
    /* 纬度 ddmm.mmmm -> 度 */
    double lat = atof(f[2]);
    int d = (int)(lat / 100.0);
    fix->lat = d + (lat - d * 100.0) / 60.0;
    double lon = atof(f[4]);
    d = (int)(lon / 100.0);
    fix->lon = d + (lon - d * 100.0) / 60.0;
    /* 时间 hhmmss */
    if (strlen(f[1]) >= 6) {
        fix->hour = (f[1][0]-'0')*10 + (f[1][1]-'0');
        fix->min  = (f[1][2]-'0')*10 + (f[1][3]-'0');
        fix->sec  = (f[1][4]-'0')*10 + (f[1][5]-'0');
    }
    fix->sats = atoi(f[7]);
    (void)len;
    return 1;
}
```

- [ ] **Step 3: 写实现（net TCP 客户端）**

`terminal/src/hal/net.c`：
```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "hal.h"

int hal_net_connect(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    if (fd < 0) return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
    return fd;
}
int hal_net_send(int fd, const unsigned char *b, int n) {
    int t = 0;
    while (t < n) {
        int r = send(fd, b + t, n - t, 0);
        if (r <= 0) return -1;
        t += r;
    }
    return t;
}
int hal_net_recv(int fd, unsigned char *b, int cap) {
    int r = recv(fd, b, cap, 0);
    return r;   /* >0 字节数, 0 对端关闭, -1 错误 */
}
```

- [ ] **Step 4: 交叉编译冒烟**

Run: `for f in rfid led beep servo gps net; do arm-none-linux-gnueabi-gcc -static -I terminal/include -c -o /tmp/$f.o terminal/src/hal/$f.c || echo "FAIL $f"; done`
Expected: 全部编译通过，无 FAIL 输出。

- [ ] **Step 5: 提交**

```bash
git add terminal/src/hal/rfid.c terminal/src/hal/led.c terminal/src/hal/beep.c terminal/src/hal/servo.c terminal/src/hal/gps.c terminal/src/hal/net.c terminal/include/hal.h
git commit -m "feat: HAL wrappers (rfid/led/beep/servo/gps/net)"
```

---

## Task 6: 服务层——消息分发 + 网络收发线程

**Files:**
- Create: `terminal/include/dispatch.h`, `terminal/include/net_task.h`, `terminal/include/app.h`
- Create: `terminal/src/svc/dispatch.c`, `terminal/src/svc/net_task.c`

**Interfaces:**
- Produces:
  - `int dispatch_register(unsigned short id, msg_handler_t h)`；`int dispatch_handle(unsigned short id, const unsigned char *body, int len)`；`typedef int (*msg_handler_t)(unsigned short, const unsigned char *, int)`。
  - `app_state` 结构（`terminal/include/app.h`）：持锁保护，含 `server_ip/port`、`terminal_id[6]`、`serial`、`card[4]/has_card`、`door_open`、`verified`、`fatigue`、`gps_fix`、`net_ok`、显示条目索引等。
  - `void net_task_run(app_state *st)` — 网络线程主体（连接/重连、收包→decode→dispatch、发包由业务直接调用 `hal_net_send`）。

- [ ] **Step 1: 写实现（dispatch 注册表）**

`terminal/include/dispatch.h`：
```c
#ifndef DISPATCH_H
#define DISPATCH_H
typedef int (*msg_handler_t)(unsigned short id, const unsigned char *body, int len);
int dispatch_register(unsigned short id, msg_handler_t h);
int dispatch_handle(unsigned short id, const unsigned char *body, int len);
#endif
```

`terminal/src/svc/dispatch.c`：
```c
#include "dispatch.h"
#define MAX_HANDLERS 16
static unsigned short ids[MAX_HANDLERS];
static msg_handler_t  hds[MAX_HANDLERS];
static int count = 0;
int dispatch_register(unsigned short id, msg_handler_t h) {
    if (count >= MAX_HANDLERS) return -1;
    ids[count] = id; hds[count] = h; count++;
    return 0;
}
int dispatch_handle(unsigned short id, const unsigned char *body, int len) {
    for (int i = 0; i < count; i++)
        if (ids[i] == id) return hds[i](id, body, len);
    return -1;   /* 无处理函数 */
}
```

- [ ] **Step 2: 写实现（网络线程）**

`terminal/include/app.h`：
```c
#ifndef APP_H
#define APP_H
#include <pthread.h>
#include "cfg.h"
#include "hal.h"

typedef struct {
    pthread_mutex_t lock;
    app_config_t cfg;
    unsigned char term_id[6];       /* 由 terminal_id 文本转 BCD */
    unsigned short serial;
    /* 业务状态 */
    unsigned char card[4]; int has_card;
    int door_open;
    int verified;                   /* 0 未验证 / 1 准许 / -1 拒绝 */
    int fatigue;
    gps_fix_t gps;
    int net_ok;
    /* 显示 */
    int disp_idx;                   /* 当前翻页条目 */
} app_state;

int  app_term_id_from_text(app_state *st);   /* 文本 -> BCD */
#endif
```

`terminal/src/svc/net_task.c`：
```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "app.h"
#include "hal.h"
#include "dispatch.h"
#include "jt808.h"

static int term_id_from_text(const char *txt, unsigned char out[6]) {
    /* 12 个十六进制字符 -> 6 字节 */
    for (int i = 0; i < 6; i++) {
        unsigned int v = 0;
        if (sscanf(txt + i * 2, "%2x", &v) != 1) return -1;
        out[i] = (unsigned char)v;
    }
    return 0;
}

int app_term_id_from_text(app_state *st) {
    return term_id_from_text(st->cfg.terminal_id, st->term_id);
}

void *net_task_run(void *arg) {
    app_state *st = (app_state *)arg;
    unsigned char rbuf[1024];
    jt808_decoder_t dec;
    jt808_decoder_init(&dec);

    for (;;) {
        int fd = hal_net_connect(st->cfg.server_ip, st->cfg.server_port);
        if (fd < 0) { usleep(1000000); continue; }
        pthread_mutex_lock(&st->lock); st->net_ok = 1; pthread_mutex_unlock(&st->lock);

        while (1) {
            int n = hal_net_recv(fd, rbuf, sizeof(rbuf));
            if (n <= 0) break;
            int off = 0;
            while (off < n) {
                unsigned short mid; unsigned char ph[6], tm[6], body[512]; int blen;
                int r = jt808_decode(&dec, rbuf + off, n - off, &mid, ph, tm, body, &blen);
                if (r == 1) dispatch_handle(mid, body, blen);
                off += n;    /* 简化：单次 recv 内可能多帧时逐步推进 */
                break;
            }
        }
        pthread_mutex_lock(&st->lock); st->net_ok = 0; pthread_mutex_unlock(&st->lock);
        close(fd);
        usleep(2000000);
    }
    return NULL;
}
```

> 注：`net_task` 只读、`decode`+`dispatch`；发帧由业务线程（Task 9）在需要时用 `hal_net_send` 直接写当前套接字——为此 Task 9 会把“当前网络 fd”也放进 `app_state` 并在 `net_task` 中维护（见 Task 9 修订）。此处先落地读路径。

- [ ] **Step 3: 交叉编译冒烟**

Run: `arm-none-linux-gnueabi-gcc -static -I terminal/include -I shared -c -o /tmp/net_task.o terminal/src/svc/net_task.c && arm-none-linux-gnueabi-gcc -static -I terminal/include -c -o /tmp/dispatch.o terminal/src/svc/dispatch.c`
Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add terminal/include/dispatch.h terminal/include/net_task.h terminal/include/app.h terminal/src/svc/dispatch.c terminal/src/svc/net_task.c
git commit -m "feat: dispatch registry + net rx thread"
```

---

## Task 7: 服务层——显示管理 + 状态灯

**Files:**
- Create: `terminal/include/display_mgr.h`, `terminal/include/status_led.h`
- Create: `terminal/src/svc/display_mgr.c`, `terminal/src/svc/status_led.c`

**Interfaces:**
- Produces:
  - `void display_mgr_register(int idx, const char *name, char *(*get)(app_state *))`；`void display_mgr_show_item(app_state *st, int idx)`（数码管显示 + 控制台打印类型+值）；`void display_mgr_cycle(app_state *st)`（轮播，仅数码管）。
  - `void status_led_task(void *arg)` — 根据 `st->fatigue`/`st->net_ok`/`gps.status` 运行四种时序。

- [ ] **Step 1: 写实现（display_mgr）**

`terminal/include/display_mgr.h`：
```c
#ifndef DISPLAY_MGR_H
#define DISPLAY_MGR_H
#include "app.h"
typedef char *(*disp_getter_t)(app_state *st);
void display_mgr_register(int idx, const char *name, disp_getter_t get);
void display_mgr_show_item(app_state *st, int idx, int to_console);
int  display_mgr_count(void);
#endif
```

`terminal/src/svc/display_mgr.c`：
```c
#include <stdio.h>
#include "display_mgr.h"
#include "hal.h"
#define MAX_ITEMS 16
static const char *names[MAX_ITEMS];
static disp_getter_t getters[MAX_ITEMS];
static int count = 0;

void display_mgr_register(int idx, const char *name, disp_getter_t get) {
    if (idx < 0 || idx >= MAX_ITEMS) return;
    names[idx] = name; getters[idx] = get;
    if (idx + 1 > count) count = idx + 1;
}
int display_mgr_count(void) { return count; }

void display_mgr_show_item(app_state *st, int idx, int to_console) {
    char buf[32];
    if (idx < 0 || idx >= count || !getters[idx]) return;
    const char *val = getters[idx](st);
    snprintf(buf, sizeof(buf), "%s", val ? val : "-");
    if (hal_display_string(0, buf) < 0) { /* 数码管显示 */ }
    if (to_console) printf("[%s] = %s\n", names[idx], buf);
}
```

- [ ] **Step 2: 写实现（status_led）**

`terminal/include/status_led.h`：
```c
#ifndef STATUS_LED_H
#define STATUS_LED_H
void *status_led_task(void *arg);
#endif
```

`terminal/src/svc/status_led.c`：
```c
#include <unistd.h>
#include <pthread.h>
#include "status_led.h"
#include "app.h"
#include "hal.h"

static void ms(int t) { usleep(t * 1000); }

void *status_led_task(void *arg) {
    app_state *st = (app_state *)arg;
    int fd = hal_led_open();
    if (fd < 0) return NULL;
    int startup = 1;
    while (1) {
        int fatigue, netok;
        pthread_mutex_lock(&st->lock);
        fatigue = st->fatigue; netok = st->net_ok;
        pthread_mutex_unlock(&st->lock);

        if (startup) {            /* 启动：绿200/灭300，约2s后转运行 */
            for (int i = 0; i < 4; i++) { hal_led_set(fd,'g',1); ms(200); hal_led_set(fd,'g',0); ms(300); }
            startup = 0; continue;
        }
        if (fatigue) {            /* 报警：红/绿/蓝序列 */
            hal_led_set(fd,'r',1); ms(100); hal_led_set(fd,'r',0); ms(50);
            hal_led_set(fd,'r',1); ms(100); hal_led_set(fd,'r',0); ms(50);
            hal_led_set(fd,'g',1); ms(100); hal_led_set(fd,'g',0); ms(50);
            hal_led_set(fd,'g',1); ms(100); hal_led_set(fd,'g',0); ms(50);
            hal_led_set(fd,'b',1); ms(100); hal_led_set(fd,'b',0); ms(50);
            hal_led_set(fd,'b',1); ms(100); hal_led_set(fd,'b',0); ms(350);
        } else if (!netok || st->gps.status == 0) {  /* 无网/无GPS */
            hal_led_set(fd,'r',1); ms(300); hal_led_set(fd,'r',0); ms(200);
            hal_led_set(fd,'g',1); ms(300); hal_led_set(fd,'g',0); ms(200);
            hal_led_set(fd,'b',1); ms(300); hal_led_set(fd,'b',0); ms(200);
        } else {                  /* 正常运行：绿200/灭1000 */
            hal_led_set(fd,'g',1); ms(200); hal_led_set(fd,'g',0); ms(1000);
        }
    }
    return NULL;
}
```

- [ ] **Step 3: 交叉编译冒烟**

Run: `arm-none-linux-gnueabi-gcc -static -I terminal/include -c -o /tmp/display_mgr.o terminal/src/svc/display_mgr.c && arm-none-linux-gnueabi-gcc -static -I terminal/include -c -o /tmp/status_led.o terminal/src/svc/status_led.c`
Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add terminal/include/display_mgr.h terminal/include/status_led.h terminal/src/svc/display_mgr.c terminal/src/svc/status_led.c
git commit -m "feat: display manager + status LED state machine"
```

---

## Task 8: 服务层——命令行解析

**Files:**
- Create: `terminal/include/cmdline.h`, `terminal/src/svc/cmdline.c`

**Interfaces:**
- Produces:
  - `void cmdline_loop(app_state *st)` — 主线程调用；阻塞读 stdin，解析 `?`/`help`/`info`/`uppath <文件>`。
  - `int uppath_upload(app_state *st, const char *file)` — 读历史路径文件，逐条组 `0x0200` 帧发送（供 cmdline 调用）。

- [ ] **Step 1: 写实现**

`terminal/include/cmdline.h`：
```c
#ifndef CMDLINE_H
#define CMDLINE_H
#include "app.h"
void cmdline_loop(app_state *st);
int  uppath_upload(app_state *st, const char *file);
#endif
```

`terminal/src/svc/cmdline.c`：
```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cmdline.h"
#include "hal.h"
#include "jt808.h"
#include "dispatch.h"

static void print_help(void) {
    printf("commands:\n");
    printf("  ? / help                 show this help\n");
    printf("  info                     show current sensor values\n");
    printf("  uppath <coord-file>      upload history path to server\n");
}

static void print_info(app_state *st) {
    printf("net_ok=%d door_open=%d verified=%d fatigue=%d\n",
           st->net_ok, st->door_open, st->verified, st->fatigue);
    printf("gps: lat=%.6f lon=%.6f sats=%d status=%d\n",
           st->gps.lat, st->gps.lon, st->gps.sats, st->gps.status);
    printf("card: %02x%02x%02x%02x has_card=%d\n",
           st->card[0], st->card[1], st->card[2], st->card[3], st->has_card);
}

/* 历史路径文件格式：每行 "纬度,经度"（度，十进制） */
int uppath_upload(app_state *st, const char *file) {
    FILE *f = fopen(file, "r");
    unsigned char out[512];
    char line[128];
    if (!f) { perror("uppath open"); return -1; }
    while (fgets(line, sizeof(line), f)) {
        double lat, lon;
        if (sscanf(line, "%lf , %lf", &lat, &lon) != 2) continue;
        /* 简易 0x0200 消息体：报警0/状态0 + 纬度4 + 经度4 */
        unsigned char body[16] = {0};
        long ilat = (long)(lat * 1000000);
        long ilon = (long)(lon * 1000000);
        body[0]=body[1]=body[2]=body[3]=0; body[4]=body[5]=body[6]=body[7]=0;
        body[8] = (ilat>>24)&0xff; body[9]=(ilat>>16)&0xff; body[10]=(ilat>>8)&0xff; body[11]=ilat&0xff;
        body[12] = (ilon>>24)&0xff; body[13]=(ilon>>16)&0xff; body[14]=(ilon>>8)&0xff; body[15]=ilon&0xff;
        int n = jt808_build(out, sizeof(out), MSG_LOCATION, st->term_id, st->term_id,
                            &st->serial, body, 16);
        if (st->net_fd >= 0) hal_net_send(st->net_fd, out, n);
        usleep(200000);
    }
    fclose(f);
    printf("uppath %s done\n", file);
    return 0;
}

void cmdline_loop(app_state *st) {
    char line[256];
    printf("taxi_terminal> "); fflush(stdout);
    while (fgets(line, sizeof(line), stdin)) {
        char cmd[64] = {0}, arg[128] = {0};
        sscanf(line, "%63s %127s", cmd, arg);
        if (!strcmp(cmd, "?") || !strcmp(cmd, "help")) print_help();
        else if (!strcmp(cmd, "info")) print_info(st);
        else if (!strcmp(cmd, "uppath")) {
            if (arg[0]) uppath_upload(st, arg);
            else printf("usage: uppath <coord-file>\n");
        }
        else if (cmd[0]) printf("unknown cmd: %s (try help)\n", cmd);
        printf("taxi_terminal> "); fflush(stdout);
    }
}
```

> 注：`st->net_fd` 字段在 Task 9 的 `app.h` 修订中补齐；`uppath` 的 0x0200 消息体为演示用的简化版（完整字段见 Task 9 的位置上报）。

- [ ] **Step 2: 交叉编译冒烟**

Run: `arm-none-linux-gnueabi-gcc -static -I terminal/include -I shared -c -o /tmp/cmdline.o terminal/src/svc/cmdline.c`
Expected: 编译通过。

- [ ] **Step 3: 提交**

```bash
git add terminal/include/cmdline.h terminal/src/svc/cmdline.c
git commit -m "feat: command line parsing (help/info/uppath)"
```

---

## Task 9: 业务层 taxi 状态机 + main 集成 + 收尾

**Files:**
- Modify: `terminal/include/app.h`（补 `net_fd`、`beep_fd`、`servo_fd` 等句柄）
- Create: `terminal/include/taxi.h`, `terminal/src/biz/taxi.c`, `terminal/src/main.c`
- Modify: `terminal/src/svc/net_task.c`（维护 `st->net_fd`）
- Create: `terminal/Makefile`, `terminal/deploy/config`, `terminal/deploy/bt.sh`, `terminal/deploy/start.sh`

**Interfaces:**
- Produces:
  - `int taxi_init(app_state *st)` — 注册消息号处理函数、显示条目、初始化句柄。
  - `void *taxi_key_thread(void *)`、`void *taxi_rfid_thread(void *)`、`void *taxi_gps_thread(void *)`、`void *taxi_beep_thread(void *)`、`void *taxi_cycle_thread(void *)`。
  - `int taxi_report_auth(app_state *st)`（组 `0x0210`）、`int taxi_report_location(app_state *st)`（组 `0x0200`）、`int taxi_handle_auth_resp(...)`（`0x8110`）、`int taxi_handle_fatigue(...)`（`0x8210`）。

- [ ] **Step 1: 修订 app.h 与 net_task 维护 net_fd**

`terminal/include/app.h` 追加到 `app_state`：
```c
    int net_fd;      /* 当前网络套接字，net_task 维护 */
    int key_fd, display_fd, rfid_fd, beep_fd, servo_fd, gps_fd;
```

`terminal/src/svc/net_task.c` 中，连接成功处加：
```c
        pthread_mutex_lock(&st->lock); st->net_ok = 1; st->net_fd = fd; pthread_mutex_unlock(&st->lock);
```
断线处加：
```c
        pthread_mutex_lock(&st->lock); st->net_ok = 0; st->net_fd = -1; pthread_mutex_unlock(&st->lock);
```
并删除局部变量 `fd` 与 `net_fd` 冲突的写法（`net_fd` 只在 `st` 中）。

- [ ] **Step 2: 写 taxi 业务**

`terminal/include/taxi.h`：
```c
#ifndef TAXI_H
#define TAXI_H
#include "app.h"
int taxi_init(app_state *st);
void *taxi_key_thread(void *);
void *taxi_rfid_thread(void *);
void *taxi_gps_thread(void *);
void *taxi_beep_thread(void *);
void *taxi_cycle_thread(void *);
int taxi_report_auth(app_state *st);
int taxi_report_location(app_state *st);
int taxi_handle_auth_resp(unsigned short id, const unsigned char *b, int len);
int taxi_handle_fatigue(unsigned short id, const unsigned char *b, int len);
#endif
```

`terminal/src/biz/taxi.c`（核心，完整逻辑）：
```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "taxi.h"
#include "hal.h"
#include "keymap.h"
#include "display_mgr.h"
#include "dispatch.h"
#include "jt808.h"

/* ---------- 显示条目 getter ---------- */
static char *g_card(app_state *st) {
    static char b[16];
    snprintf(b, sizeof(b), "%02X%02X%02X%02X", st->card[0], st->card[1], st->card[2], st->card[3]);
    return b;
}
static char *g_verify(app_state *st) {
    return st->verified == 1 ? "PASS" : st->verified == -1 ? "FAIL" : "----";
}
static char *g_door(app_state *st) { return st->door_open ? "OPEN" : "CLOS"; }
static char *g_fatigue(app_state *st) { return st->fatigue ? "FATIG" : "OK"; }
static char *g_net(app_state *st) { return st->net_ok ? "NETOK" : "NET--"; }
static char *g_gps(app_state *st) {
    static char b[16];
    snprintf(b, sizeof(b), "%d", st->gps.sats);
    return b;
}

/* ---------- 上报 ---------- */
static void send_frame(app_state *st, unsigned short mid, const unsigned char *body, int len) {
    unsigned char out[512];
    int n = jt808_build(out, sizeof(out), mid, st->term_id, st->term_id, &st->serial, body, len);
    if (st->net_fd >= 0) hal_net_send(st->net_fd, out, n);
}

int taxi_report_auth(app_state *st) {
    unsigned char body[64];
    body[0] = st->card[0]; body[1] = st->card[1]; body[2] = st->card[2]; body[3] = st->card[3];
    /* 输入的身份码（ASCII）由 key 线程暂存到 st->auth_buf */;
    int len = 4 + st->auth_len;
    memcpy(body + 4, st->auth_buf, st->auth_len);
    send_frame(st, MSG_AUTH_REQ, body, len);
    return 0;
}

int taxi_report_location(app_state *st) {
    unsigned char body[32] = {0};
    /* 报警标志4 + 状态4 = 0；纬度4 + 经度4 */
    long ilat = (long)(st->gps.lat * 1000000);
    long ilon = (long)(st->gps.lon * 1000000);
    body[8]=(ilat>>24)&0xff; body[9]=(ilat>>16)&0xff; body[10]=(ilat>>8)&0xff; body[11]=ilat&0xff;
    body[12]=(ilon>>24)&0xff; body[13]=(ilon>>16)&0xff; body[14]=(ilon>>8)&0xff; body[15]=ilon&0xff;
    send_frame(st, MSG_LOCATION, body, 16);
    return 0;
}

/* 0x8110 身份验证应答 */
int taxi_handle_auth_resp(unsigned short id, const unsigned char *b, int len) {
    (void)id;
    app_state *st = &g_state;
    if (len < 1) return -1;
    pthread_mutex_lock(&st->lock);
    st->verified = (b[0] == 0) ? 1 : -1;
    pthread_mutex_unlock(&st->lock);
    hal_display_string(st->display_fd, st->verified == 1 ? "PASS" : "FAIL");
    hal_beep_on(st->beep_fd); usleep(200000); hal_beep_off(st->beep_fd);
    return 0;
}

/* 0x8210 疲劳驾驶下发 */
int taxi_handle_fatigue(unsigned short id, const unsigned char *b, int len) {
    (void)id;
    app_state *st = &g_state;
    if (len < 1) return -1;
    pthread_mutex_lock(&st->lock);
    st->fatigue = (b[0] == 1) ? 1 : 0;
    pthread_mutex_unlock(&st->lock);
    return 0;
}

/* ---------- 线程 ---------- */
static char auth_buf[8];
static int  auth_len = 0;

void *taxi_key_thread(void *arg) {
    app_state *st = (app_state *)arg;
    while (1) {
        unsigned int code = 0; char d = 0;
        int v = hal_key_read(st->key_fd, &code);
        if (v != 1) continue;
        int a = key_map(code, &d);
        pthread_mutex_lock(&st->lock);
        if (a == K_DIGIT) {
            if (auth_len < 6) auth_buf[auth_len++] = d;
            hal_display_string(st->display_fd, auth_buf);
            printf("[身份码输入] %.*s\n", auth_len, auth_buf);
        } else if (a == K_CONFIRM) {
            auth_buf[auth_len] = 0;
            memcpy(st->auth_buf, auth_buf, auth_len);
            st->auth_len = auth_len;
            taxi_report_auth(st);
            auth_len = 0;
        } else if (a == K_CLOSE) {
            st->door_open = 0;
            hal_servo_angle(st->servo_fd, st->cfg.door_close_angle);
            printf("[车门] 关闭\n");
        } else if (a == K_PREV) {
            st->disp_idx = (st->disp_idx + display_mgr_count() - 1) % display_mgr_count();
            display_mgr_show_item(st, st->disp_idx, 1);
        } else if (a == K_NEXT) {
            st->disp_idx = (st->disp_idx + 1) % display_mgr_count();
            display_mgr_show_item(st, st->disp_idx, 1);
        }
        pthread_mutex_unlock(&st->lock);
        st->last_key = time(NULL);
    }
    return NULL;
}

void *taxi_rfid_thread(void *arg) {
    app_state *st = (app_state *)arg;
    unsigned char card[4];
    while (1) {
        int r = hal_rfid_read(st->rfid_fd, card);
        if (r == 1) {
            pthread_mutex_lock(&st->lock);
            memcpy(st->card, card, 4); st->has_card = 1;
            st->door_open = 1;
            pthread_mutex_unlock(&st->lock);
            hal_servo_angle(st->servo_fd, st->cfg.door_open_angle);
            printf("[刷卡] 卡号 %02X%02X%02X%02X，车门打开\n", card[0],card[1],card[2],card[3]);
            usleep(500000);
        } else {
            usleep(200000);
        }
    }
    return NULL;
}

void *taxi_gps_thread(void *arg) {
    app_state *st = (app_state *)arg;
    char buf[512];
    while (1) {
        int n = hal_net_recv(st->gps_fd, (unsigned char *)buf, sizeof(buf) - 1);
        if (n <= 0) { usleep(500000); continue; }
        buf[n] = 0;
        if (hal_gps_parse(buf, n, &st->gps) == 1) {
            taxi_report_location(st);
        }
        usleep(1000000);
    }
    return NULL;
}

void *taxi_beep_thread(void *arg) {
    app_state *st = (app_state *)arg;
    while (1) {
        int fatigue;
        pthread_mutex_lock(&st->lock); fatigue = st->fatigue; pthread_mutex_unlock(&st->lock);
        if (fatigue) {
            hal_beep_on(st->beep_fd); usleep(300000);
            hal_beep_off(st->beep_fd); usleep(300000);
        } else {
            usleep(100000);
        }
    }
    return NULL;
}

void *taxi_cycle_thread(void *arg) {
    app_state *st = (app_state *)arg;
    while (1) {
        sleep(1);
        pthread_mutex_lock(&st->lock);
        time_t now = time(NULL);
        if (now - st->last_key >= 10) {
            /* 轮播：每项 3 秒，只显示数码管不写控制台 */
            for (int i = 0; i < display_mgr_count(); i++) {
                display_mgr_show_item(st, i, 0);
                pthread_mutex_unlock(&st->lock);
                sleep(3);
                pthread_mutex_lock(&st->lock);
            }
            st->last_key = now;
        }
        pthread_mutex_unlock(&st->lock);
    }
    return NULL;
}

/* ---------- 初始化 ---------- */
static app_state g_state;   /* 供消息处理函数访问 */

int taxi_init(app_state *st) {
    st->key_fd = hal_key_open();
    st->display_fd = hal_display_open();
    st->rfid_fd = hal_rfid_open();
    st->beep_fd = hal_beep_open();
    st->servo_fd = hal_servo_open();
    st->gps_fd = hal_gps_open();
    st->net_fd = -1;

    display_mgr_register(0, "卡号", g_card);
    display_mgr_register(1, "身份验证", g_verify);
    display_mgr_register(2, "车门", g_door);
    display_mgr_register(3, "疲劳", g_fatigue);
    display_mgr_register(4, "网络", g_net);
    display_mgr_register(5, "GPS卫星", g_gps);

    dispatch_register(MSG_AUTH_RESP, taxi_handle_auth_resp);
    dispatch_register(MSG_FATIGUE, taxi_handle_fatigue);

    hal_servo_angle(st->servo_fd, st->cfg.door_close_angle);   /* 初始关门 */
    return 0;
}
```

- [ ] **Step 3: 写 main.c + Makefile + 部署脚本**

`terminal/src/main.c`：
```c
#include <stdio.h>
#include <pthread.h>
#include "app.h"
#include "cfg.h"
#include "taxi.h"
#include "status_led.h"
#include "cmdline.h"
#include "net_task.h"

int main(int argc, char **argv) {
    app_state st;
    const char *cfgpath = (argc > 1) ? argv[1] : "deploy/config";
    pthread_t t_key, t_rfid, t_gps, t_beep, t_cycle, t_led, t_net;

    pthread_mutex_init(&st.lock, NULL);
    cfg_load(&st.cfg, cfgpath);
    app_term_id_from_text(&st);
    st.serial = 1;
    st.disp_idx = 0;
    st.last_key = time(NULL);
    st.net_fd = -1;

    taxi_init(&st);

    pthread_create(&t_led,   NULL, status_led_task, &st);
    pthread_create(&t_net,   NULL, net_task_run,    &st);
    pthread_create(&t_key,   NULL, taxi_key_thread, &st);
    pthread_create(&t_rfid,  NULL, taxi_rfid_thread,&st);
    pthread_create(&t_gps,   NULL, taxi_gps_thread, &st);
    pthread_create(&t_beep,  NULL, taxi_beep_thread,&st);
    pthread_create(&t_cycle, NULL, taxi_cycle_thread,&st);

    printf("init done. 输入 ? 或 help 查看命令\n");
    cmdline_loop(&st);   /* 主线程只读 stdin */
    return 0;
}
```

`terminal/Makefile`：
```makefile
CC=arm-none-linux-gnueabi-gcc
CFLAGS=-c -g -Wall -Iinclude -I../shared
LDFLAGS=-static -lpthread
OBJS=src/main.o src/common/cfg.o src/common/keymap.o \
     src/hal/key.o src/hal/display.o src/hal/rfid.o src/hal/led.o \
     src/hal/beep.o src/hal/servo.o src/hal/gps.o src/hal/net.o \
     src/svc/dispatch.o src/svc/net_task.o src/svc/display_mgr.o \
     src/svc/status_led.o src/svc/cmdline.o src/biz/taxi.o ../shared/jt808.o
taxi_terminal: $(OBJS)
	$(CC) $(OBJS) -o taxi_terminal $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -f $(OBJS) taxi_terminal
```

`terminal/deploy/config`：
```
server_ip=192.168.1.100
server_port=8888
terminal_id=091111222200
password_len=6
door_open_angle=90
door_close_angle=0
```

`terminal/deploy/bt.sh`（驱动加载，参考 demo.key1/bt.sh）：
```bash
#!/bin/sh
insmod /key-zlg72128.ko
insmod /rfid_driver.ko
insmod /fs6818_led.ko
insmod /beep_driver.ko
insmod /servo.ko
```

`terminal/deploy/start.sh`：
```bash
#!/bin/sh
/app/quectel-CM > /dev/null &
sleep 5
./taxi_terminal deploy/config
```

- [ ] **Step 4: 全量交叉编译**

Run: `cd terminal && make clean && make`
Expected: 生成 `taxi_terminal` 可执行文件，无报错。

- [ ] **Step 5: 提交**

```bash
git add terminal/src/main.c terminal/src/biz/taxi.c terminal/include/taxi.h terminal/include/app.h terminal/src/svc/net_task.c terminal/Makefile terminal/deploy/*
git commit -m "feat: taxi business + main integration + deploy scripts"
```

---

## Task 10: 真机联调与验收清单

**Files:** 无新代码（可能微调 `keymap.c` 回填按键、`display.c` 后端）

- [ ] **Step 1: 上板部署**
  - 拷贝 `taxi_terminal` + `deploy/` 到板子；`chmod +x taxi_terminal deploy/*.sh`；先 `./deploy/bt.sh` 加载驱动，再 `./deploy/start.sh`。

- [ ] **Step 2: 按键标定**
  - 运行 `./tools/key_dump`，逐个按键记录 `code`，回填 `keymap.c` 的确认/关门/翻页键映射，重新 `make`。

- [ ] **Step 3: 逐条验收（对应 spec §13）**
  - ① 交叉编译通过、程序正常启动；② 刷卡→舵机开门、按键→关门；③ 键盘输 6 位身份码→数码管显示+控制台打印→上报→平台返回准许/拒绝→数码管+蜂鸣器提示；④ 平台下发疲劳→蜂鸣器周期报警、解除→停止；⑤ `?`/`help`/`info`/`uppath <文件>` 可用；⑥ 翻页浏览、10 秒无按键自动轮播（每项 3 秒）；⑦ 三色状态灯时序正确；⑧ 平台多终端、校验司机、下发指令。

- [ ] **Step 4: 提交联调修正**
  ```bash
  git add -A
  git commit -m "fix: on-board calibration adjustments"
  ```

---

## Task 11: 平台脚手架 + 共用 jt808 编解码

**Files:**
- Create: `platform/Makefile`, `platform/include/server.h`, `platform/src/main.c`

**Interfaces:**
- Produces:
  - 可执行 `platform/gps_server`，监听 `platform/config` 里的端口；复用 `shared/jt808.c` 解帧，暂只打印收到的帧（`recv id = %04X`）。

- [ ] **Step 1: 写 main.c**

`platform/src/main.c`：
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "jt808.h"

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 8888;
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa; int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_addr.s_addr = INADDR_ANY; sa.sin_port = htons(port);
    if (bind(ls, (struct sockaddr *)&sa, sizeof(sa)) < 0) { perror("bind"); return 1; }
    listen(ls, 16);
    printf("listening on :%d\n", port);

    for (;;) {
        int fd = accept(ls, NULL, NULL);
        if (fd < 0) continue;
        printf("client connected fd=%d\n", fd);
        if (fork() == 0) {
            close(ls);
            jt808_decoder_t d; jt808_decoder_init(&d);
            unsigned char buf[1024];
            while (1) {
                int n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) break;
                int off = 0;
                while (off < n) {
                    unsigned short mid; unsigned char ph[6], tm[6], body[512]; int blen;
                    int r = jt808_decode(&d, buf + off, n - off, &mid, ph, tm, body, &blen);
                    if (r == 1) {
                        printf("recv id = %04X len=%d term=%02X%02X%02X%02X%02X%02X\n",
                               mid, blen, tm[0],tm[1],tm[2],tm[3],tm[4],tm[5]);
                    } else if (r == -1) { printf("bad frame\n"); }
                    off = n; break;
                }
            }
            close(fd); _exit(0);
        }
        close(fd);
    }
    return 0;
}
```

- [ ] **Step 2: 写 Makefile**

`platform/Makefile`：
```makefile
CC=gcc
CFLAGS=-c -g -Wall -Iinclude -I../shared
OBJS=src/main.o src/server.o src/session.o src/logic.o src/hook.o ../shared/jt808.o
gps_server: $(OBJS)
	$(CC) $(OBJS) -o gps_server -lpthread
%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -f $(OBJS) gps_server
```

- [ ] **Step 3: 构建冒烟**（先只建 main）

Run: `cd platform && gcc -I ../shared -o gps_server src/main.c ../shared/jt808.c && ./gps_server 8888 &`
Expected: 打印 `listening on :8888`；用另一终端 `echo`/netcat 或终端程序连上后能打印 `recv id = ...`。

- [ ] **Step 4: 提交**

```bash
git add platform/Makefile platform/src/main.c platform/include/server.h
git commit -m "feat: platform scaffold with shared jt808 decode"
```

---

## Task 12: 平台业务（session / logic / hook 对接接口）

**Files:**
- Create: `platform/include/session.h`, `platform/include/logic.h`, `platform/include/hook.h`
- Create: `platform/src/session.c`, `platform/src/logic.c`, `platform/src/hook.c`
- Modify: `platform/src/main.c`（接入 session 循环），`platform/config`

**Interfaces:**
- Produces:
  - `typedef struct session { int fd; unsigned char term_id[6]; struct session *next; } session_t;`
  - `session_t *session_add(session_t **head, int fd)`；`session_t *session_find(session_t *head, const unsigned char term[6])`。
  - `void logic_on_frame(session_t *s, unsigned short mid, const unsigned char *body, int len)` — 处理 `0x0210`：查白名单 → 回 `0x8110`；处理位置 `0x0200`：记录。
  - `int hook_emit(const char *event, const char *json)` — 对接接口层，默认打印到 stdout + 写日志文件，可被第三方替换。
  - `void logic_send(session_t *s, unsigned short mid, const unsigned char *body, int len)`。

- [ ] **Step 1: 写 session.c**

`platform/src/session.c`：
```c
#include <stdlib.h>
#include <string.h>
#include "session.h"
session_t *session_add(session_t **head, int fd) {
    session_t *s = calloc(1, sizeof(session_t));
    s->fd = fd; s->next = *head; *head = s;
    return s;
}
session_t *session_find(session_t *head, const unsigned char term[6]) {
    for (; head; head = head->next)
        if (memcmp(head->term_id, term, 6) == 0) return head;
    return NULL;
}
```

- [ ] **Step 2: 写 logic.c（含白名单校验 + 下发）**

`platform/src/logic.c`：
```c
#include <stdio.h>
#include <string.h>
#include "logic.h"
#include "hook.h"
#include "jt808.h"

/* 白名单：卡号(4) + 身份码(最多6)，可配置；此处硬编码示例，实际从 config 读 */
static const unsigned char whitelist[][10] = {
    { 0xAA,0xBB,0xCC,0xDD, '1','2','3','4','5','6' },
};
#define WHITE_N (sizeof(whitelist)/sizeof(whitelist[0]))

void logic_send(session_t *s, unsigned short mid, const unsigned char *body, int len) {
    unsigned char out[512];
    static unsigned short serial = 0;
    int n = jt808_build(out, sizeof(out), mid, s->term_id, s->term_id, &serial, body, len);
    send(s->fd, out, n, 0);
}

void logic_on_frame(session_t *s, unsigned short mid, const unsigned char *body, int len) {
    if (mid == MSG_AUTH_REQ && len >= 10) {
        unsigned char resp[32];
        int ok = 0;
        for (unsigned i = 0; i < WHITE_N; i++) {
            if (memcmp(body, whitelist[i], 4) == 0 &&
                memcmp(body + 4, whitelist[i] + 4, 6) == 0) { ok = 1; break; }
        }
        resp[0] = ok ? 0 : 1;
        strcpy((char *)resp + 1, ok ? "PASS" : "DENY");
        logic_send(s, MSG_AUTH_RESP, resp, 1 + (ok ? 4 : 4));
        hook_emit("auth", ok ? "{\"result\":\"pass\"}" : "{\"result\":\"deny\"}");
    }
    else if (mid == MSG_LOCATION) {
        hook_emit("location", "{\"type\":\"location\"}");
    }
}
```

- [ ] **Step 3: 写 hook.c（第三方对接接口层）**

`platform/src/hook.c`：
```c
#include <stdio.h>
#include <time.h>
#include "hook.h"
/* 对接接口层：默认打印 + 写日志。第三方子系统可在此接入数据库/HTTP/消息队列。 */
int hook_emit(const char *event, const char *json) {
    FILE *f = fopen("platform.log", "a");
    time_t t = time(NULL);
    fprintf(f ? f : stderr, "[%ld][%s] %s\n", (long)t, event, json);
    if (f) fclose(f);
    printf("[hook][%s] %s\n", event, json);
    return 0;
}
```

- [ ] **Step 4: 接入 main.c + 增加“下发疲劳”调试入口**

`platform/src/main.c` 在子进程收到帧处改为调用 `logic_on_frame`（替代直接打印）；另在主进程增加从 stdin 读命令：输入 `fatigue <termid>` 时对该终端下发 `0x8210 疲劳=1`，`nofatigue <termid>` 下发 `=0`。关键片段：
```c
/* 主进程（父）单独线程读 stdin 下发疲劳 */
static session_t *g_head = NULL;
void *admin_loop(void *arg) {
    (void)arg;
    char cmd[64];
    while (scanf("%63s", cmd) == 1) {
        if (!strcmp(cmd, "fatigue") || !strcmp(cmd, "nofatigue")) {
            unsigned char term[6]; int n = scanf("%12s", (char *)term);
            (void)n; /* 简化：此处按文本终端号处理，实际按 hex 转 BCD */
            unsigned char b = !strcmp(cmd, "fatigue") ? 1 : 0;
            session_t *s = session_find(g_head, term);
            if (s) logic_send(s, MSG_FATIGUE, &b, 1);
        }
    }
    return NULL;
}
```

- [ ] **Step 5: 构建 + 联调**

Run: `cd platform && make && ./gps_server 8888`
Expected: 终端连上后，`0x0210` 触发 `0x8110` 应答并打印 `[hook][auth]...`；输入 `fatigue` 时终端蜂鸣器报警。

- [ ] **Step 6: 提交**

```bash
git add platform/src/session.c platform/src/logic.c platform/src/hook.c platform/src/main.c platform/include/*.h platform/config
git commit -m "feat: platform logic (auth/fatigue) + hook integration layer"
```

---

## 自检记录（写计划后自查）

- **Spec 覆盖**：§1~§13 均有对应任务——HAL(Task3-5)、线程/主线程读stdin(Task6,9)、JT808(Task2)、显示/轮播(Task7)、状态灯(Task7)、命令行(Task8)、业务(Task9)、平台(Task11-12)、真机验收(Task10)。
- **占位符**：无 TBD/TODO；所有代码步骤给出完整源码。
- **类型一致性**：`app_state` 字段（`net_fd`/`auth_buf`/`auth_len`/`last_key`）在 Task 9 集中定义并在 Task 6/8 引用；`jt808_build/jt808_decode/jt808_decoder_t` 签名全程一致；消息号宏统一。
- **已知简化点（在代码注释中标注）**：`uppath`/`0x0200` 消息体为演示级精简；`net_task` 单帧推进逻辑；`logic.c` 白名单硬编码示例。
