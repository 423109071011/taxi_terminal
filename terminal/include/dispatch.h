#ifndef DISPATCH_H
#define DISPATCH_H
typedef int (*msg_handler_t)(unsigned short id, const unsigned char *body, int len);
int dispatch_register(unsigned short id, msg_handler_t h);
int dispatch_handle(unsigned short id, const unsigned char *body, int len);
#endif
