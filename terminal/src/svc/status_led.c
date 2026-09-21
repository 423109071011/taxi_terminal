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
        int fatigue, netok, gps_status, smoke;
        pthread_mutex_lock(&st->lock);
        fatigue = st->fatigue; netok = st->net_ok; gps_status = st->gps.status;
        smoke = st->smoke_alarm;            /* 烟雾(火警)同样触发报警灯序列 */
        pthread_mutex_unlock(&st->lock);

        if (startup) {            /* 启动：绿200/灭300，约2s后转运行 */
            for (int i = 0; i < 4; i++) { hal_led_set(fd,'g',1); ms(200); hal_led_set(fd,'g',0); ms(300); }
            startup = 0; continue;
        }
        if (fatigue || smoke) {   /* 报警：红/绿/蓝序列（疲劳驾驶 或 烟雾火警） */
            hal_led_set(fd,'r',1); ms(100); hal_led_set(fd,'r',0); ms(50);
            hal_led_set(fd,'r',1); ms(100); hal_led_set(fd,'r',0); ms(50);
            hal_led_set(fd,'g',1); ms(100); hal_led_set(fd,'g',0); ms(50);
            hal_led_set(fd,'g',1); ms(100); hal_led_set(fd,'g',0); ms(50);
            hal_led_set(fd,'b',1); ms(100); hal_led_set(fd,'b',0); ms(50);
            hal_led_set(fd,'b',1); ms(100); hal_led_set(fd,'b',0); ms(350);
        } else if (!netok || gps_status == 0) {  /* 无网/无GPS */
            hal_led_set(fd,'r',1); ms(300); hal_led_set(fd,'r',0); ms(200);
            hal_led_set(fd,'g',1); ms(300); hal_led_set(fd,'g',0); ms(200);
            hal_led_set(fd,'b',1); ms(300); hal_led_set(fd,'b',0); ms(200);
        } else {                  /* 正常运行：绿200/灭1000 */
            hal_led_set(fd,'g',1); ms(200); hal_led_set(fd,'g',0); ms(1000);
        }
    }
    return NULL;
}
