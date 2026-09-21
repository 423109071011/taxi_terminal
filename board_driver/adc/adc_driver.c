#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h> 
#include <linux/fs.h> /*file_operations*/
#include <linux/slab.h> /* kzalloc */
#include <linux/clk.h>  /* clk_get */

#include <linux/platform_device.h>

/*	Nexell soc headers */
#include <mach/platform.h>
#include <mach/devices.h>
#include <mach/soc.h>

#include "adc_driver.h"

/*
 *Description of This Driver
 * */
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Driver for S5P6818's adc");
MODULE_AUTHOR("Farsight yanfa <support@farsight.com.cn>");
MODULE_VERSION("V1.0");

//关系到设备的名字 /dev/DEVICE_NAME
#define DEVICE_NAME "adc"
/*
 *	调试使用
 * */
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

#define ADC_INTERRUPT

/*
 *	adc device struct
 * */
struct adc_dev{
	struct resource *mem_res;
	struct resource *irq_res;
	void __iomem *base_addr;
#ifdef ADC_INTERRUPT
	wait_queue_head_t waitq;
	int flag_irq;
#endif
	struct clk *clk;
};

struct adc_dev *adc_dev;

static void dev_init(struct adc_dev *dev)
{
	unsigned int adccon = 0;

	/*set prescaler, APEN ADCON_STBY */
#ifdef S5P4418_ADC
	writel(((readl(dev->base_addr + CON) | 0xff << APSV_BITP) 
		| (1 << APEN_BITP) | (0 << ADCON_STBY), dev->base_addr + CON);
#else
	adccon = ((DATA_SEL_VAL & 0xf) << DATA_SEL_BITP) |
			 ((CLK_CNT_VAL & 0xf) << CLK_CNT_BITP) |
			 (0 << ADCON_STBY);

	writel(adccon, dev->base_addr + CON);

	writel(((readl(dev->base_addr + PRESCALERCON) & ~(0x3FF << PRES_BITP)) 
		| 0xFF << PRES_BITP), dev->base_addr + PRESCALERCON);
	writel((readl(dev->base_addr + PRESCALERCON) | 0x1 << APEN_BITP), 
		dev->base_addr + PRESCALERCON);
#endif

	writel(1 << AICL_BITP, dev->base_addr + CLRINT);
	writel(1 << AIEN_BITP, dev->base_addr + INTENB);
	
	LOGI("<%s> register init successfully\n", DEVICE_NAME);
}

 irqreturn_t adc_irq_handler(int irqno, void *devid)
{
	adc_dev->flag_irq = 1;
	writel(1, adc_dev->base_addr + CLRINT);
	wake_up_interruptible(&adc_dev->waitq);

	return IRQ_HANDLED; 
}

static ssize_t dev_read(struct file *filp, char *buf, size_t count, loff_t *loff)
{
	int data = 0;
	//获得文件私有指针
	struct adc_dev *dev = filp->private_data;
	if (count != sizeof(data))
		return -EINVAL;

	/* start A/D conversion */
	writel((readl(dev->base_addr + CON) 
		| 1 << ADEN_BITP), dev->base_addr + CON);
	/*
	 *进程会睡眠直到condition = true
	 * */
#ifdef ADC_INTERRUPT
	wait_event_interruptible(dev->waitq, dev->flag_irq == 1);
#endif
	//ADC resolution 12bit
	data = readl(dev->base_addr + DAT) & 0xfff;
	LOGD("read data is %d\n", data);
	if (copy_to_user(buf, &data, sizeof(data)))
		return -EFAULT;

#ifdef ADC_INTERRUPT
	dev->flag_irq = 0;
#endif
	return count;
}

static long dev_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	int ch = -1;
	struct adc_dev *dev =filp->private_data;

#if 0
	if (copy_from_user((void *)&ch, (void *)arg, sizeof(ch)))
		return -EFAULT;
#endif
	ch = arg;
	if (ch < 0 || ch > 7)
		return -EINVAL;
	
	LOGD("select channel: <%d>\n", ch);
	switch(cmd) {
	case SET_CHANNEL:
		//选择adc通道
		writel(((readl(dev->base_addr + CON) & ~(0x7 << ASEL_BITP)) 
			| ch << ASEL_BITP), dev->base_addr + CON);
		break;
	default:
		LOGE("Please check your command\n");
		return -EINVAL;
	}

	return 0;
}
static int dev_open(struct inode *inode, struct file *filp)
{ 
	int err;
#ifdef ADC_INTERRUPT	
	//初始化等待队列
	init_waitqueue_head(&adc_dev->waitq);
#endif
	//地址映射
	adc_dev->base_addr = 
		ioremap(adc_dev->mem_res->start, 
			adc_dev->mem_res->end - adc_dev->mem_res->start);

	//申请中断
#ifdef ADC_INTERRUPT
	err = request_irq(adc_dev->irq_res->start, adc_irq_handler,
			IRQF_DISABLED, DEVICE_NAME, NULL);
	if (err) {
		LOGE("Failed to request <%s's irq(%d)\n", 
			DEVICE_NAME, adc_dev->irq_res->start);
		return err;
	}

	adc_dev->flag_irq = 0;
#endif

	//开启adc时钟
	adc_dev->clk = clk_get(NULL, "adc");
	clk_enable(adc_dev->clk);

	dev_init(adc_dev);

	//文件私有数据
	filp->private_data = adc_dev;
	return 0;
}
static int dev_release(struct inode *inode, struct file *filp)
{
#ifdef ADC_INTERRUPT
	free_irq(adc_dev->irq_res->start, NULL);
#endif
	iounmap(adc_dev->base_addr);
	return 0;
}
/*
 *设备操作集，根据需要填充
 * */
static struct file_operations dev_fops = {
	.owner	= THIS_MODULE,
	.open	= dev_open,
	.release	= dev_release,
	.read	= dev_read,
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
 *	device & driver matches 
 * */
static int adc_probe(struct platform_device *pdev)
{
	int err;
	LOGI("<%s> matches successfully\n", DEVICE_NAME);
	
	//	alloc adc_dev's mem
	if (!(adc_dev = kzalloc(sizeof(struct adc_dev), GFP_KERNEL)))
		return -ENOMEM;

	//获得platform_device中描述的地址资源
	if (!(adc_dev->mem_res = platform_get_resource(pdev,
				IORESOURCE_MEM, 0))) {
		LOGE("<%s> has no memroy resource\n", DEVICE_NAME);
		err = -ENODEV;
		goto err1;
	}
	//获得platform_device中描述的中断资源
	if (!(adc_dev->irq_res = platform_get_resource(pdev,
				IORESOURCE_IRQ, 0))) {
		LOGE("<%s> has no irq resource\n", DEVICE_NAME);
		err = -ENODEV;
		goto err1;
	}
	LOGD("mem = %x, irq = %d\n",
			adc_dev->mem_res->start, adc_dev->irq_res->start);

	err = misc_register(&misc);
	if (err)
		goto err1;
	LOGI("<%s> register successfully\n", DEVICE_NAME);

	return 0;
err1:
	kfree(adc_dev);
	return err;
}
static int adc_remove(struct platform_device *pdev)
{
	misc_deregister(&misc);
	kfree(adc_dev);

	LOGI("<%s> remove successfully\n", DEVICE_NAME);
	return 0;
}
/*
 *	for adc_device unregister
 * */
static int adc_device_release(struct platform_device *pdev)
{
	LOGI("plat: remove adevice <%s>\n", DEVICE_NAME);
	return 0;
}
#if 0
/*
 *	platform_device 
 *
 * */
static struct resource adc_resource[] = {
	[0] = DEFINE_RES_MEM(PHY_BASEADDR_ADC, SZ_1K),
	[1] = DEFINE_RES_IRQ(IRQ_PHY_ADC),
};
/*
 *	device & driver's name must be consitent
 * */
#ifndef CONFIG_ADC_SAMPLE_RATE
#define CONFIG_ADC_SAMPLE_RATE 200000 //200K
#endif
static struct platform_device adc_device = {
	.name 			= "fs6818-adc",
	.id				= -1,
	.num_resources	= ARRAY_SIZE(adc_resource),
	.resource		= adc_resource,
	.dev			= {
		.release = adc_device_release,
		.platform_data = CONFIG_ADC_SAMPLE_RATE,
	}
};
#endif
/*
 *	platform_driver
 * */
static struct platform_driver adc_driver = {
	.driver	= {
		.name = "fs6818-adc",
		.owner = THIS_MODULE,
	},
	.probe 	= adc_probe,
	.remove	= adc_remove,
};
/*
 *Driver Init
 * */
static int __init adc_module_init(void)
{
#if 0
	/*
	 * 注册platform device：adc
	 * */
	platform_device_register(&adc_device);
	LOGI("plat: add adevice <%s>\n", DEVICE_NAME);
#endif
	return platform_driver_register(&adc_driver);
}

module_init(adc_module_init);

/*
 *Driver Exit
 * */
static void __exit adc_module_exit(void)
{
//	platform_device_unregister(&adc_device);
	/*
	 * 卸载platform driver：adc
	 * */
	platform_driver_unregister(&adc_driver);
	LOGI("<%s> exit successfully\n", DEVICE_NAME);
}

module_exit(adc_module_exit);

