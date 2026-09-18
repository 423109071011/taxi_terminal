#ifndef DISPLAY_MGR_H
#define DISPLAY_MGR_H
#include "app.h"
typedef char *(*disp_getter_t)(app_state *st);
void display_mgr_register(int idx, const char *name, disp_getter_t get);
void display_mgr_show_item(app_state *st, int idx, int to_console);
int  display_mgr_count(void);
#endif
