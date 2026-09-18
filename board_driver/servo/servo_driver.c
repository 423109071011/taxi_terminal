#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h> 
#include <linux/fs.h> /*file_operations*/

#include <linux/gpio.h> /*gpio_*()*/
#include <asm-generic/ioctl.h>
#include <asm/uaccess.h> /*copy_from_user*/
#include <linux/slab.h> /* kzalloc */
#include <linux/clk.h>
#include <linux/delay.h>

//nexell soc headers
#include <mach/platform.h>
#include <mach/devices.h> /*PAD_GPIO_*Need*/
#include <mach/soc.h>   /*设置gpio模式使用*/

/*
 *Description of This Driver
 * */
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Driver for 6818M4's servo");
MODULE_AUTHOR("Farsight yanfa <support@farsight.com.cn>");
MODULE_VERSION("V1.0");

//关系到设备的名字 /dev/DEVICE_NAME
#define DEVICE_NAME	"servo"
/*********************调试使用*************************************/
//#define DEBUG
//调试打印信息
#ifdef DEBUG
#define LOGD(fmt, args...) \
	{printk(KERN_INFO "<<-Device:%s->> line %d at %s ", DEVICE_NAME, __LINE__, __func__); \
		printk(KERN_INFO fmt, ##args);}
#else
#define LOGD(fmt, args...) (void)(0);
#endif
//正常提示信息
#define LOGI(fmt, args...) {printk(KERN_INFO fmt, ##args);}
//错误提示信息
#define LOGE(fmt, args...) {printk(KERN_ERR fmt, ##args);}
/***************************************************************/


/************************IOCTL CMD******************************/
/*
 *IOCTL CMD
 *_IOWR(type, nr, size)
 * */
#define IOCTL_MAGIC 	'S'
#define ROTATE_0 	_IO(IOCTL_MAGIC, 0) 
#define ROTATE_45 	_IO(IOCTL_MAGIC, 1) 
#define ROTATE_90 	_IO(IOCTL_MAGIC, 2) 
#define ROTATE_135 	_IO(IOCTL_MAGIC, 3) 
#define ROTATE_180 	_IO(IOCTL_MAGIC, 4) 
#define SET_ANGLE	_IO(IOCTL_MAGIC, 5)
/**************************************************************/

/**************************GPIO resource****************************/
#define USED_GPIO   "GPIOC7"
//GPIO列表
static unsigned long gpio_table[] = {
	PAD_GPIO_C + 7,
};

#define GPIO_NUM (sizeof(gpio_table) / sizeof(unsigned int))

/****************************************************************/
#define PWM_TIMER_ADDR	(0xC0018000) 

/*
 *	servo control time
 * 	servo control period 20ms 
 *  high_level_time:
 *	500us 	--> 0°
 *	1000us 	--> 45°
 *	1500us	--> 90°
 *	2000us	--> 135°
 *	2500us	--> 180°
 * */
#define	T20MS	(20 * 10000)
#define T500US	(5000)
#define T600US	(6000)
#define T1000US	(2 * T500US)
#define T1500US	(T500US + T1000US)
#define T2000US	(2 * T1000US)
#define T2500US	(T2000US + T500US)
#define T1S		(10000000)

struct pwm_timer {
	//pwm-timer base address
	void __iomem *base;
	int counter;
};

struct servo {
	struct pwm_timer pwm;
	struct clk *clk_t;
	struct clk *clk_p;
	struct delayed_work work;
	int high_level_time;
	int running;   /* 1=设备已打开，工作任务可继续；释放后禁止再动 GPIO */
};

extern int start_pwm_timer(struct pwm_timer *pwm);
extern int stop_pwm_timer(struct pwm_timer *pwm);
extern void init_pwm_timer(struct pwm_timer *pwm);
extern int get_timer_count(struct pwm_timer *pwm);

static struct servo *servo_dev; 
/*
 *开启某一个具体设备操作 
 * */
static inline void dev_on(int nr)
{
	gpio_direction_output(gpio_table[nr], PAD_LEVEL_HIGH);
}
/*
 *关闭某一个具体设备操作
 * */
static inline void dev_off(int nr)
{
	gpio_direction_output(gpio_table[nr], PAD_LEVEL_LOW);
}
/*
 *重置某一个设备状态操作
 * */
static void dev_reset(int nr, int state)
{
	gpio_direction_output(gpio_table[nr], state);
}
static void dev_work(struct work_struct *work)
{
	long time = 0;

	/* 设备已关闭：不再触碰 GPIO，也不再自我重排。
	 * 否则 release 释放 gpio 后，仍在飞行中的工作任务会操作该引脚，
	 * 导致内核以 "?auto?" 标签重新申请(gpio_ensure_requested 警告)，
	 * 下次 open 时 gpio_request 返回 -EBUSY(-16)。 */
	if (!servo_dev->running)
		return;

	start_pwm_timer(&servo_dev->pwm);
	udelay(20); //等待定时器初始化完成
	time = get_timer_count(&servo_dev->pwm);
	dev_on(0);

	while(!(time - get_timer_count(&servo_dev->pwm)
				> servo_dev->high_level_time));
	dev_off(0);

	stop_pwm_timer(&servo_dev->pwm);

	schedule_delayed_work(&servo_dev->work, HZ / 50);
}
/*
 *User Space open /dev/xxx
 * */
static int dev_open(struct inode *node, struct file *filp)
{
	int i = 0, err = 0;
	void __iomem *base = NULL;

	base = ioremap(PWM_TIMER_ADDR, 0x44);
	if (!base) 
		return -ENOMEM;
	
	servo_dev->pwm.base = base;
	
	servo_dev->pwm.counter = T1S;
	servo_dev->high_level_time = T500US;

	servo_dev->clk_p = clk_get(NULL, "pclk");
	clk_enable(servo_dev->clk_p);
#if 0
	rate = clk_get_rate(servo_dev->clk_p);
	servo_dev->clk_t = clk_get(NULL, "timer");
	rate = clk_get_rate(servo_dev->clk_t);
#endif
	init_pwm_timer(&servo_dev->pwm);
	/*
	 *Request GPIO resource
	 * */
	for(i = 0; i < GPIO_NUM; i++) {
		err = gpio_request(gpio_table[i], USED_GPIO);
		if (err) {
			LOGE("Failed to request <%s's gpio :%s> errno = %d\n", 
					DEVICE_NAME, USED_GPIO, err);
			if (i > 0)
				goto err_gpio_request;

			return err;
		}
	}
	LOGI("<%s> %s request successfully", DEVICE_NAME, USED_GPIO);
	/**
	 * Init Used Gpio
	 */
	for (i = 0; i < GPIO_NUM; i++) {
		//设置引脚功能为GPIO模式
		nxp_soc_gpio_set_io_func(gpio_table[i], PAD_FUNC_ALT1);
		//配置引脚为输出模式
		nxp_soc_gpio_set_io_dir(gpio_table[i], PAD_MODE_OUT);
		nxp_soc_gpio_set_out_value(gpio_table[i], PAD_LEVEL_LOW);
		nxp_soc_gpio_set_io_pull_sel(gpio_table[i], 1);
		nxp_soc_gpio_set_io_pull_enb(gpio_table[i], 1);
	}

	servo_dev->running = 1;
	schedule_delayed_work(&servo_dev->work, HZ / 50);
	return 0;

err_gpio_request:
	iounmap(servo_dev->pwm.base);
	for(i -= 1; i >=0; i--)
		gpio_free(gpio_table[i]);
	return err;
}
/*
 *User Space Close fd
 * */
static int dev_release(struct inode *node, struct file *filp)
{
	int i = 0;

	/*
	 *释放IO Resource,和申请顺序相反,遵循先申请后释放
	 * */
	for(i = GPIO_NUM - 1; i >= 0; i--) {
		//关闭设备
		dev_reset(i, PAD_LEVEL_LOW);
		gpio_free(gpio_table[i]);	
	}

	/* 先置为停止，让工作任务尽快退出；再用同步取消确保
	 * 所有在飞行/已排队的工作都执行完毕，避免释放后仍操作 GPIO */
	servo_dev->running = 0;
	cancel_delayed_work_sync(&servo_dev->work);

	stop_pwm_timer(&servo_dev->pwm);

	iounmap(servo_dev->pwm.base);

	return 0;
}
/*
 * cmd命令一般为(魔数，基数，变量型)
 * */
static long dev_ioctl(struct file *filp, 
			unsigned int cmd, unsigned long arg)
{
	//判断魔数是否一致
	if (_IOC_TYPE(cmd) != IOCTL_MAGIC)
		return -ENOTTY;
	//判断基数是否超出范围
	if (_IOC_NR(cmd) > 5)
		return -ENOTTY;

	switch(cmd) {
	case ROTATE_0:
		servo_dev->high_level_time = T500US;
//		防止舵机出现吱吱响的声音
//		servo_dev->high_level_time = T700US;
		break;
	case ROTATE_45:
		servo_dev->high_level_time = T1000US;
		break;
	case ROTATE_90:
		servo_dev->high_level_time = T1500US;
		break;
	case ROTATE_135:
		servo_dev->high_level_time = T2000US;
		break;
	case ROTATE_180:
		servo_dev->high_level_time = T2500US;
		break;
	case SET_ANGLE:
		servo_dev->high_level_time = 
			T500US + (T2500US - T500US) / 180 * arg;
		break;
	default:
		LOGE("Please Check User's cmd\n");
		return -EINVAL;

		}
	LOGD("high_level_time = %d\n", servo_dev->high_level_time);	
	return 0;
}

/*
 *设备操作集，根据需要填充
 * */
static struct file_operations dev_fops = {
	.owner	= THIS_MODULE,
	.open	= dev_open,
	.release	= dev_release,
//	.read	= dev_read,
//	.write 	= dev_write,
	.unlocked_ioctl	= dev_ioctl,
};

/*
 *Define User's Misc Device 
 * */
static struct miscdevice misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DEVICE_NAME,
	.fops	= &dev_fops,
};
/*
 *Driver Init
 * */
static int __init gpio_module_init(void)
{
	int ret;

	servo_dev = kzalloc(sizeof(struct servo), GFP_KERNEL);
	if (!servo_dev)
		return -ENOMEM;

	//初始化工作队列
	INIT_DELAYED_WORK(&servo_dev->work, dev_work);

	
	ret = misc_register(&misc);// 将设备注册为一个杂项设备
	LOGI("<%s,minor %d> init successfully\n", 
			DEVICE_NAME, misc.minor);

	return ret;
}

module_init(gpio_module_init);

/*
 *Driver Exit
 * */
static void __exit gpio_module_exit(void)
{
	kfree(servo_dev);
	misc_deregister(&misc); //删除杂项设备
	LOGI("<%s> exit successfully\n", 
			DEVICE_NAME);
}

module_exit(gpio_module_exit);


