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
