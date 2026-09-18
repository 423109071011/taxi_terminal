/*
 * FS6818 蜂鸣器驱动（绿圈版 / GPIOC29，修复原版两个致命 bug）
 * ------------------------------------------------------------------
 * 引脚：GPIOC29（绿圈蜂鸣器；无源类型，靠定时器翻转 GPIO 产生方波驱动）
 * 设备：/dev/beep（misc）
 * 命令：BEEP_ON  _IO('B',0)
 *       BEEP_OFF _IO('B',1)
 *       SET_FREQUENCY _IOW('B',2,int)   参数为指针，指向 1~100 的整数（半周期 ms）
 *
 * 相对原版（见 beep_driver.c.orig）修了两处致命 bug：
 *
 *   【bug 1】dev_release() 里 kfree(beep)，但 dev_open() 不重新分配，
 *           而 beep 是模块级全局对象、只在 module_init 分配过一次。
 *           结果：第 1 次 open 正常，close 之后 beep 变野指针，
 *                 第 2 次起的 open 里 init_timer()/mod_timer() 全踩在
 *                 已释放内存上 → 定时器彻底失效 → 一点声音都没有。
 *           修法：只在 module_init 分配一次，release 里只停表、不清内存。
 *
 *   【bug 2】BEEP_OFF 只 del_timer()，没有把引脚拉低，引脚停在最后一次
 *           翻转的电平上；如果正好停在高电平，蜂鸣器就会一直响。
 *           修法：BEEP_OFF 停表后显式输出低电平。
 *
 *   【bug 3】timer_handler 里定时器重装用的是 beep->delay，而 delay 默认 0，
 *           0 延迟会让定时器自旋、翻转频率高到听不见。
 *           修法：delay 下限钳到 1，open 时给默认值 2（约 250Hz）。
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#include <linux/gpio.h>
#include <asm-generic/ioctl.h>
#include <asm/uaccess.h>
#include <mach/platform.h>
#include <mach/devices.h>
#include <mach/soc.h>

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("FS6818 beep driver (green buzzer on GPIOC29)");
MODULE_AUTHOR("Farsight yanfa <support@farsight.com.cn>");
MODULE_VERSION("V2.0");

#define DEVICE_NAME	"beep"

#define LOGI(fmt, args...) {printk(KERN_INFO fmt, ##args);}
#define LOGE(fmt, args...) {printk(KERN_ERR fmt, ##args);}

/************************IOCTL CMD******************************/
#define IOCTL_MAGIC 	'B'
#define BEEP_ON 	_IO(IOCTL_MAGIC, 0)
#define BEEP_OFF 	_IO(IOCTL_MAGIC, 1)
#define SET_FREQUENCY _IOW(IOCTL_MAGIC, 2, int)
/**************************************************************/

/**************************GPIO resource****************************/
/*
 *	GPIOC30 = PAD_GPIO_C + 29
 *	beep	:	GPIOC29
 */
#define USED_GPIO   "GPIOC29"

static unsigned long gpio_table[] = {
	PAD_GPIO_C + 29,
};

#define GPIO_NUM (sizeof(gpio_table) / sizeof(unsigned int))
/*******************************************************************/

#define DELAY_DEFAULT_MS 2      /* 默认翻转半周期 2ms → 约 250Hz */
#define DELAY_MIN_MS     1      /* 下限 1ms，绝不允许 0（0 会让定时器自旋失声） */
#define DELAY_MAX_MS     100

struct beep_dev {
	struct timer_list beep_timer;
	int delay;          /* 翻转半周期，单位 ms（板上 HZ=1000） */
	int running;        /* 定时器是否在跑 */
};

/* 模块级全局对象：只在 module_init 里分配一次，release 绝不释放 */
static struct beep_dev *beep;

static void dev_set(int nr, int state)
{
	gpio_direction_output(gpio_table[nr], state);
}

static void timer_handler(unsigned long arg)
{
	if (gpio_get_value(gpio_table[0]))
		gpio_direction_output(gpio_table[0], PAD_LEVEL_LOW);
	else
		gpio_direction_output(gpio_table[0], PAD_LEVEL_HIGH);

	/* 重装定时器；delay 已在 ioctl 里钳到 1~100，不会是 0 */
	beep->beep_timer.expires = jiffies + beep->delay;
	add_timer(&beep->beep_timer);
}

static int dev_open(struct inode *node, struct file *filp)
{
	int i = 0, err = 0;

	for (i = 0; i < GPIO_NUM; i++) {
		err = gpio_request(gpio_table[i], USED_GPIO);
		if (err) {
			LOGE("Failed to request <%s's gpio :%s> errno = %d\n",
					DEVICE_NAME, USED_GPIO, err);
			if (i > 0)
				goto err_gpio_request;
			return err;
		}
	}
	LOGI("<%s> %s request successfully\n", DEVICE_NAME, USED_GPIO);

	for (i = 0; i < GPIO_NUM; i++) {
		nxp_soc_gpio_set_io_func(gpio_table[i], PAD_FUNC_ALT0);
		nxp_soc_gpio_set_io_dir(gpio_table[i], PAD_MODE_OUT);
		nxp_soc_gpio_set_out_value(gpio_table[i], PAD_LEVEL_LOW);
	}

	/* 每次 open 都重新初始化定时器与状态，避免残留状态影响 */
	del_timer_sync(&beep->beep_timer);
	init_timer(&beep->beep_timer);
	beep->beep_timer.function = timer_handler;
	beep->beep_timer.expires = jiffies + beep->delay;
	beep->running = 0;

	return 0;

err_gpio_request:
	for (i -= 1; i >= 0; i--)
		gpio_free(gpio_table[i]);
	return err;
}

static int dev_release(struct inode *node, struct file *filp)
{
	int i;

	/* 停表 + 拉低，保证关设备后一定静音 */
	del_timer_sync(&beep->beep_timer);
	beep->running = 0;
	for (i = 0; i < GPIO_NUM; i++)
		dev_set(i, PAD_LEVEL_LOW);

	for (i = GPIO_NUM - 1; i >= 0; i--)
		gpio_free(gpio_table[i]);

	/* 注意：绝不能 kfree(beep)！它是模块级全局对象，
	 * 释放后下次 dev_open 会直接踩野指针（原版就是死在这里）。 */
	return 0;
}

static long dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int nr;

	if (_IOC_TYPE(cmd) != IOCTL_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > 2)
		return -ENOTTY;

	if (_IOC_DIR(cmd) == _IOC_WRITE) {
		if (copy_from_user((void *)&nr, (void *)arg, sizeof(nr)))
			return -EFAULT;
		if (nr < DELAY_MIN_MS) nr = DELAY_MIN_MS;   /* 关键：不许为 0 */
		if (nr > DELAY_MAX_MS) nr = DELAY_MAX_MS;
		beep->delay = nr;
	}

	switch (cmd) {
	case BEEP_ON:
		if (!beep->running) {
			beep->beep_timer.expires = jiffies + beep->delay;
			add_timer(&beep->beep_timer);
			beep->running = 1;
		}
		break;
	case BEEP_OFF:
		if (beep->running) {
			del_timer_sync(&beep->beep_timer);
			beep->running = 0;
		}
		dev_set(0, PAD_LEVEL_LOW);      /* 停完必须拉低，否则可能长鸣 */
		break;
	case SET_FREQUENCY:
		/* delay 已在上面拷贝并生效；正在响则用新频率重装 */
		if (beep->running) {
			mod_timer(&beep->beep_timer, jiffies + beep->delay);
		}
		break;
	default:
		LOGE("Please Check User's cmd\n");
		return -EINVAL;
	}

	return 0;
}

static struct file_operations dev_fops = {
	.owner		= THIS_MODULE,
	.open		= dev_open,
	.release	= dev_release,
	.unlocked_ioctl	= dev_ioctl,
};

static struct miscdevice misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DEVICE_NAME,
	.fops	= &dev_fops,
};

static int __init gpio_module_init(void)
{
	int ret;

	beep = kzalloc(sizeof(struct beep_dev), GFP_KERNEL);
	if (!beep)
		return -ENOMEM;
	beep->delay = DELAY_DEFAULT_MS;
	beep->running = 0;
	init_timer(&beep->beep_timer);
	beep->beep_timer.function = timer_handler;
	beep->beep_timer.expires = jiffies + beep->delay;

	ret = misc_register(&misc);
	LOGI("<%s,minor %d> init successfully (%s, half period %dms)\n",
			DEVICE_NAME, misc.minor, USED_GPIO, beep->delay);
	return ret;
}

module_init(gpio_module_init);

static void __exit gpio_module_exit(void)
{
	misc_deregister(&misc);
	if (beep) {
		del_timer_sync(&beep->beep_timer);
		kfree(beep);
		beep = NULL;
	}
	LOGI("<%s> exit successfully\n", DEVICE_NAME);
}

module_exit(gpio_module_exit);
