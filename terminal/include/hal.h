#ifndef HAL_H
#define HAL_H
/* key */
int hal_key_open(void);
int hal_key_read(int fd, unsigned int *code);
#endif
