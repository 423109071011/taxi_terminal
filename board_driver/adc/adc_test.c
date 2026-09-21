#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "adc_driver.h"

/* 用户态编译时 CONFIG_FS6818B/M4 未定义，通道宏缺失，这里给兜底值
 * （与 FS6818B 配置一致；M4 板则编译时自行 -DCONFIG_FS6818M4 覆盖） */
#ifndef ALCOHOL_CHANNEL
#define ALCOHOL_CHANNEL 7
#endif
#ifndef LIGHT_CHANNEL
#define LIGHT_CHANNEL   1
#endif
#ifndef SMOKE_CHANNEL
#define SMOKE_CHANNEL   5
#endif
#ifndef FLAME_CHANNEL
#define FLAME_CHANNEL   6
#endif

int main(int argc, const char *argv[])
{
	int fd;
	int data;
	char ch;

	printf("*******************************\n");
	printf("please change Sensor type:\n");
	printf("A: alcohol sensor(switch AD1)\n");
	printf("L: light sensor(switch AD2)\n");
	printf("S: smoke sensor(switch AD4)\n");
	printf("F: flame sensor(switch AD3)\n");
	printf("P: potentiometer(no switch)\n");
	printf("*******************************\n");

	fprintf(stderr, "input type:");

	fd = open("/dev/adc", O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	ch = getchar();

	switch(ch) {
	case 'A':
		ioctl(fd, SET_CHANNEL, ALCOHOL_CHANNEL);
		break;
	case 'L':
		ioctl(fd, SET_CHANNEL, LIGHT_CHANNEL);
		break;
	case 'S':
		ioctl(fd, SET_CHANNEL, SMOKE_CHANNEL);
		break;
	case  'F':
		ioctl(fd, SET_CHANNEL, FLAME_CHANNEL);
		break;
	case  'P':
		ioctl(fd, SET_CHANNEL, POTENTIOMETER);
		break;
	default:
		break;
	}

	while(1) {
		read(fd, &data, sizeof(data));
		printf("Vol: %0.2fV\n", 1.8 * data / 4096);
		usleep(1000000);
	}


	return 0;
}
