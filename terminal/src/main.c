#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
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

    memset(&st, 0, sizeof(st));
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
