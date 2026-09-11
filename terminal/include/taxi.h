#ifndef TAXI_H
#define TAXI_H
#include "app.h"
int taxi_init(app_state *st);
void *taxi_key_thread(void *);
void *taxi_rfid_thread(void *);
void *taxi_gps_thread(void *);
void *taxi_beep_thread(void *);
void *taxi_cycle_thread(void *);
int taxi_report_auth(app_state *st);
int taxi_report_location(app_state *st);
int taxi_handle_auth_resp(unsigned short id, const unsigned char *b, int len);
int taxi_handle_fatigue(unsigned short id, const unsigned char *b, int len);
#endif
