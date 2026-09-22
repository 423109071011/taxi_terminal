#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include "app.h"
#include "cfg.h"
#include "hal.h"
#include "taxi.h"
#include "status_led.h"
#include "cmdline.h"
#include "net_task.h"

static int g_beep_fd = -1;

/* Ctrl+C 时也要把蜂鸣器关掉，否则进程被杀、PWM 还开着就一直响 */
static void on_signal(int sig) {
    (void)sig;
    if (g_beep_fd >= 0) hal_beep_off(g_beep_fd);
    _exit(0);
}

int main(int argc, char **argv) {
    app_state st;
    const char *cfgpath = (argc > 1) ? argv[1] : "/arduino_drivers/taxi.conf";
    pthread_t t_key, t_rfid, t_gps, t_beep, t_cycle, t_led, t_net;

    memset(&st, 0, sizeof(st));
    pthread_mutex_init(&st.lock, NULL);
    if (cfg_load(&st.cfg, cfgpath))
        printf("[cfg] loaded %s -> platform %s:%d\n",
               cfgpath, st.cfg.server_ip, st.cfg.server_port);
    else {
        printf("[cfg] WARNING: cannot open %s, using builtin defaults -> %s:%d\n",
               cfgpath, st.cfg.server_ip, st.cfg.server_port);
        printf("[cfg] hint: put config at /arduino_drivers/taxi.conf "
               "or pass path as argv[1]\n");
    }
    app_term_id_from_text(&st);
    st.serial = 1;
    st.disp_idx = 0;
    st.last_key = time(NULL);
    st.net_fd = -1;

    taxi_init(&st);
    g_beep_fd = st.beep_fd;
    signal(SIGINT,  on_signal);   /* Ctrl+C */
    signal(SIGTERM, on_signal);   /* kill */

    pthread_create(&t_led,   NULL, status_led_task, &st);
    pthread_create(&t_net,   NULL, net_task_run,    &st);
    pthread_create(&t_key,   NULL, taxi_key_thread, &st);
    pthread_create(&t_rfid,  NULL, taxi_rfid_thread,&st);
    pthread_create(&t_gps,   NULL, taxi_gps_thread, &st);
    pthread_t t_hb;
    pthread_create(&t_hb,    NULL, taxi_heartbeat_thread, &st);
    pthread_create(&t_beep,  NULL, taxi_beep_thread,&st);
    pthread_t t_smoke;
    pthread_create(&t_smoke, NULL, taxi_smoke_thread,&st);
    pthread_create(&t_cycle, NULL, taxi_cycle_thread,&st);

    printf("init done. \xca\xe4\xc8\xeb ? \xbb\xf2 help \xb2\xe9\xbf\xb4\xc3\xfc\xc1\xee\n");
    printf("[BUILD] 2026-09-22-2105 card 30s validity in 0x0210\n");
    cmdline_loop(&st);   /* 主线程只读 stdin */
    hal_beep_off(st.beep_fd);   /* 退出前关掉蜂鸣器，避免余音 */
    return 0;
}
