CC=arm-none-linux-gnueabi-gcc
CFLAGS=-c -g -Wall -std=gnu99 -Iinclude -I../shared
LDFLAGS=-static -lpthread
OBJS=src/main.o src/common/cfg.o src/common/keymap.o \
     src/hal/key.o src/hal/display.o src/hal/rfid.o src/hal/led.o \
     src/hal/beep.o src/hal/servo.o src/hal/gps.o src/hal/net.o src/hal/adc.o \
     src/svc/dispatch.o src/svc/net_task.o src/svc/display_mgr.o \
     src/svc/status_led.o src/svc/cmdline.o src/biz/taxi.o ../shared/jt808.o
taxi_terminal: $(OBJS)
	$(CC) $(OBJS) -o taxi_terminal $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
clean:
	rm -f $(OBJS) taxi_terminal
