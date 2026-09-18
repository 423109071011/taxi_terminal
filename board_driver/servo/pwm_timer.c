#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/io.h>
#include <linux/export.h>
#include <asm/delay.h>


/*
 *Description of This Driver
 * */
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Driver for S5P6818's pwm-timer");
MODULE_AUTHOR("Farsight yanfa <support@farsight.com.cn>");
MODULE_VERSION("V1.0");


#define DEVICE_NAME "servo"

/*
 *	调试使用
 * */

//#define DEBUG

//正常打印信息
#define LOGI(fmt, args...) printk(KERN_INFO "device "fmt, ##args);
//错误打印信息
#define LOGE(fmt, args...) printk(KERN_ERR "device "fmt, ##args);
//调试打印信息
#ifdef DEBUG
#define LOGD(fmt, args...) \
	{printk(KERN_INFO "<<-Device:%s->> line %d at %s ",DEVICE_NAME, __LINE__, __func__);\
			printk(KERN_INFO fmt, ##args);}
#else
#define LOGD(fmt, args...) (void)(0);
#endif

/*
 *	pwm-timer register offset
 * */

#define TCFG0	0x00
#define TCFG1	0x04
#define TCON	0x08
#define TCNTB1	0x18
#define TCMPB1	0x1C
#define TCNTO1 	0x20

#if 0
struct pwm_reg {
	uint_32	TCFG0;
	uint_32 TCFG1;
	uint_32 TCON;
	uint_32	TCNTB0;
	uint_32	TCMPB0;
	uint_32 TCNTO0;
	uint_32	TCNTB1;
	uint_32 TCMPB1;
	uint_32	TCNTO1;
	uint_32	TCNTB2;
	uint_32	TCMPB2;
	uint_32	TCNTO2;
	uint_32	TCNTB3;
	uint_32 TCMPB3;
	uint_32	TCNTO3;
	uint_32	TCNTB4;
	uint_32	TCNTO4;
	uint_32	TINT_CSTAT;
};
#endif
/*
 *	timer0:0 timer1:8 timer2:12
 * */
#define TCON_BITP	8
#define PRES_BITP	0
#define MUX_BITP	4

//#define CONFIG_1MHZ
struct pwm_timer {
	//pwm-timer base address
	void __iomem *base;
	int counter;
};

int start_pwm_timer(struct pwm_timer *pwm)
{
	unsigned int tcon;
	writel(pwm->counter, pwm->base + TCNTB1);
	writel(pwm->counter >> 1, pwm->base + TCMPB1);

	LOGD("timer count buffer is %d\n", 
			readl(pwm->base + TCNTB1));
	LOGD("timer compare buffer is %d\n", 
			readl(pwm->base + TCMPB1));
	
	tcon = readl(pwm->base + TCON);
	/* auto reloaded starts timer X */
	tcon = (tcon & ~(0xf << TCON_BITP)) | (0x9 << TCON_BITP);
	
	writel(tcon, pwm->base + TCON);

	udelay(5);
//	udelay(50); //do not modify
	return !(readl(pwm->base + TCON) & (0x1 << TCON_BITP));
}
EXPORT_SYMBOL(start_pwm_timer);

int stop_pwm_timer(struct pwm_timer *pwm)
{
	unsigned int tcon;

	tcon = readl(pwm->base + TCON);

	/* stop timerX */
	tcon &= ~(0xf << TCON_BITP);
	writel(tcon, pwm->base + TCON);
	return readl(pwm->base + TCON);
}
EXPORT_SYMBOL(stop_pwm_timer);
/*
 *	Time Input Clock Frequency
 *	PCLK = 150M
 *	PCLK/{prescaler value + 1}/{divider value}
 *	150/74+1/2 = 1M 
 *	150/14+1/1 = 10M 
 * */
void init_pwm_timer(struct pwm_timer *pwm)
{
	unsigned int tcfg0, tcfg1, tcon;
	//we used pwm timer1
	/* set TCFG0 prescaler1 [7:0] */
	tcfg0 = readl(pwm->base + TCFG0);
#ifdef CONFIG_1MHZ
	//set prescaler value = 74D
	tcfg0 = (tcfg0 & ~(0xff << PRES_BITP)) | (0x4A << PRES_BITP);
	writel(tcfg0, pwm->base + TCFG0);
	
	/* set DIVIDER MUX1 = 1/2*/
	tcfg1 = readl(pwm->base + TCFG1);
	tcfg1 = (tcfg1 & ~(0xf << MUX_BITP)) | (0x1 << MUX_BITP);
	writel(tcfg1, pwm->base + TCFG1);	
#else
	//set prescaler value = 14D
	tcfg0 = (tcfg0 & ~(0xff << PRES_BITP)) | (0xE << PRES_BITP);
	writel(tcfg0, pwm->base + TCFG0);
	
	/* set DIVIDER MUX1 = 1/1*/
	tcfg1 = readl(pwm->base + TCFG1);
	tcfg1 = (tcfg1 & ~(0xf << MUX_BITP)) | (0x0 << MUX_BITP);
	writel(tcfg1, pwm->base + TCFG1);	
#endif
	writel(pwm->counter, pwm->base + TCNTB1);
	writel(pwm->counter >> 1, pwm->base + TCMPB1);

	tcon = readl(pwm->base + TCON);
	/*manual update TCNTB1, TCMPB1*/
	tcon = (tcon & ~(0xf << TCON_BITP)) | (0x2 << TCON_BITP);
	writel(tcon, pwm->base + TCON);

	LOGD("clock prescaler = %#x\n", readl(pwm->base + TCFG0));
	LOGD("clock multiplexers = %#x\n", readl(pwm->base + TCFG1));

	LOGD("timer control reg = %#x\n",readl(pwm->base + TCON));
}
EXPORT_SYMBOL(init_pwm_timer);

long get_timer_count(struct pwm_timer *pwm)
{
	return readl(pwm->base + TCNTO1);	
}
EXPORT_SYMBOL(get_timer_count);
#if 0
static int __init pwm_timer_module_init(void)
{
	LOGI("<%s> register successfully\n", DEVICE_NAME);
	return 0;
}
module_init(pwm_timer_module_init);

static void __exit pwm_timer_module_exit(void)
{
	LOGI("<%s> unregister successfully\n", DEVICE_NAME);
}
module_exit(pwm_timer_module_exit);
#endif
