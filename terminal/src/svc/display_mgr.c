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
    char disp[12];
    if (idx < 0 || idx >= count || !getters[idx]) return;
    const char *val = getters[idx](st);
    snprintf(buf, sizeof(buf), "%s", val ? val : "-");
    /* 数码管不支持汉字/字母混排规格：功能类型以数字代表，左起第 1 位 =
     * 类型号，'-' 之后为数值（如 0-00001）；手动翻页时控制台同步输出
     * 类型号、类型名与数值，自动轮播时 to_console=0 不输出 */
    snprintf(disp, sizeof(disp), "%d-%.6s", idx, buf);
    if (hal_display_string(0, disp) < 0) { /* 数码管显示 */ }
    if (to_console) printf("[%d %s] = %s\n", idx, names[idx], buf);
}
