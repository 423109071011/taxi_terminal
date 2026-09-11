#ifndef HAL_H
#define HAL_H
/* key */
int hal_key_open(void);
int hal_key_read(int fd, unsigned int *code);
/* display */
int hal_display_open(void);
int hal_display_clear(int fd);
int hal_display_string(int fd, const char *s);
int hal_display_number(int fd, long v, int width);
#endif
