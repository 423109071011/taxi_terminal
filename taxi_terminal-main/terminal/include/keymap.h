#ifndef KEYMAP_H
#define KEYMAP_H
enum { K_NONE = 0, K_DIGIT, K_CONFIRM, K_CLOSE, K_PREV, K_NEXT };
int key_map(unsigned int code, char *digit);
#endif
