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
