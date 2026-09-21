#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched.h>

#define RFID_HF_NAME  "rfid_hf"
#define RFID_LF_NAME  "rfid_lf"
#define RFID_NFC_NAME "rfid_nfc"

#define HF_DATA_BUYE  0x4
#define LF_DATA_BYTE  0x5
#define NFC_DATA_BYTE 0x4

#define SET_VAL _IO('R', 0)

struct rfid_data{
	unsigned char card_id[8];
	unsigned int data_byte;
};
#define GET_CARD_ID _IOR('R',0,struct rfid_data)

unsigned int rfid_major = 800;
unsigned int rfid_minor = 8;

unsigned char rx_buf[32];

static struct class *rfid_class;
static int module_number = 0;

/*rfid device struct*/
struct rfid_module                  
{
	struct i2c_client *client;
	struct cdev cdev;
	struct device *device;
	unsigned int data_byte;
	dev_t devno;
};

struct rfid_module  *rfid_module;

/* I2C 传输失败只报第一次：应用层 200ms 轮询读卡，硬件未接时原驱动每次
 * 都 dev_err，串口被刷屏无法操作。首次提示后保持静默重试，插回硬件即恢复。 */
static int rfid_err_reported = 0;
static void rfid_report_i2c_err(struct rfid_module *ctr_rfid, int ret)
{
	if (!rfid_err_reported) {
		printk("ret=%d,addr=%x\n", ret, ctr_rfid->client->addr);
		dev_err(&ctr_rfid->client->dev,
			"i2c read error (no ACK, keep retrying silently)\n");
		rfid_err_reported = 1;
	}
}

/*****************Character device File operations application interface part
 * begin********/

static int rfid_module_hw_write(struct  rfid_module *ctr_rfid,  int len, size_t *retlen, char *buf)
{
	struct i2c_client *client = ctr_rfid->client;
	int ret;

	struct i2c_msg msg[] = { 
		{ client->addr, 0, len, buf},   /*the buf contains register address*/
	};

	ret =i2c_transfer(client->adapter, msg, 1);
	if (ret < 0)
	{
		rfid_report_i2c_err(ctr_rfid, ret);
		return -EIO;
	}

	*retlen = len;

	return 0;
}

static int rfid_module_hw_read(struct rfid_module *ctr_rfid , int len, size_t *retlen, char *buf)
{
	struct i2c_client *client = ctr_rfid->client;
	int ret;
	struct i2c_msg msg[] = {
		{ client->addr, I2C_M_RD, len, buf },/*the buf contains register value*/
	};

	ret =i2c_transfer(client->adapter, msg, 1);
	if (ret < 0)
	{
		rfid_report_i2c_err(ctr_rfid, ret);
		return -EIO;
	}
	*retlen = len;
	return 0;
}

static int rfid_module_open(struct inode *inode, struct file *file)
{
	struct rfid_module *rfid_dev = container_of(inode->i_cdev, struct rfid_module, cdev);

	file->private_data = rfid_dev;

	return 0;
}

static int rfid_module_release(struct inode *inode, struct file *file)
{
	return 0;
}


static long rfid_module_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	struct rfid_module *rfid_dev = file->private_data;
	unsigned char buf[8] = {0};
	ssize_t len = 0;
	unsigned char val[2] = {0};
	struct rfid_data data;
	int i = 0;
	memset (&data, 0, sizeof(data));

	data.data_byte = rfid_dev->data_byte;

	switch (cmd) {
		case SET_VAL:
			if (copy_from_user(buf, (void *)arg, 8))
				return -EFAULT;
				rfid_module_hw_write(rfid_dev, 2, &len, val);
			break;
		case GET_CARD_ID:
			printk(KERN_ERR"GET_CARD_ID\n");
			rfid_module_hw_read(rfid_dev,data.data_byte,&len,data.card_id);
			for (i = 0; i < len ; i++)
				printk(KERN_ERR"GET_CARD_ID card_id : %02x\n",data.card_id[i]);
			
			if (copy_to_user((void *)arg, &data, len)){
				printk("%s:bad address !\n",__func__);
				return -EFAULT;
			}
			break;
		default:
				printk("%s:bad cmd !\n",__func__);
			break;
	}

	return 0;
}

static ssize_t rfid_module_read(struct file *file, char *buf, size_t count, loff_t* f_pos)
{
	struct rfid_module *rfid_dev = file->private_data;
	size_t len = 0;
	rfid_module_hw_read(rfid_dev,count,&len,rx_buf);

	if (copy_to_user(buf, rx_buf, len))
		return -EFAULT;
	return len;
}

static struct file_operations rfid_module_fops = {
	.owner = THIS_MODULE,
	.open = rfid_module_open,
	.read = rfid_module_read,
	.release = rfid_module_release,
	.unlocked_ioctl = rfid_module_ioctl,
};

static int register_rfid_module(struct rfid_module *rfid_dev) 
{
	int ret; 
	rfid_dev->devno = MKDEV(rfid_major, rfid_minor + module_number);

	ret = register_chrdev_region(rfid_dev->devno, 1, "rfid");
	if (ret < 0) {
		printk("Failed: register_chrdev_region\n");
		return -1;
	}

	cdev_init(&rfid_dev->cdev, &rfid_module_fops);
	rfid_dev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&rfid_dev->cdev, rfid_dev->devno, 1);

	if (ret < 0) {
		printk("Failed: cdev_add\n");
		goto err1;
	}


	/* create device file /dev/rfid_modulex */
	rfid_dev->device  = device_create(rfid_class, NULL, rfid_dev->devno, NULL, "rfid_module%d", module_number);
	if (!rfid_dev->device) {
		printk("%s, %s, device_create failed\n", __FILE__, __func__);
	}

	return 0;
err2:
	cdev_del(&rfid_dev->cdev);
err1:
	unregister_chrdev_region(rfid_dev->devno, 1);
	return ret;
}

static int unregister_rfid_module(struct rfid_module *rfid_dev) 
{
	cdev_del(&rfid_dev->cdev);
	unregister_chrdev_region(rfid_dev->devno, 1);
	device_destroy(rfid_class, rfid_dev->devno);
	return 0;
}

#if 0
irqreturn_t zlg7290_interrupt(int irq, void *devid)
{
	printk("irq = %d\n", irq);
	schedule_work(&zlg7290->work);
	return IRQ_HANDLED;
}
#endif

static int rfid_module_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	int err = -EINVAL;
	struct rfid_module *rfid_dev = rfid_module + module_number;

	printk("rfid_module probe\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		return err;
	}

	rfid_dev = kzalloc(sizeof(*rfid_dev), GFP_KERNEL);
	if (rfid_dev == NULL) {
		ret = -ENOMEM;
		goto err1;
	}
		
	rfid_dev->client = client;
	rfid_dev->data_byte = id->driver_data;

	i2c_set_clientdata(client, rfid_dev);

	ret = register_rfid_module(rfid_dev);
	if (ret < 0)
		goto err1;

	module_number++;
	return 0;

err1:
	return ret;
}

static int rfid_module_remove(struct i2c_client *client) 
{
	struct rfid_module *rfid_dev;

	rfid_dev = i2c_get_clientdata(client);
	unregister_rfid_module(rfid_dev);
	i2c_set_clientdata(client, NULL);
	kfree(rfid_dev);
	return 0;
}

static const struct i2c_device_id rfid_module_id[] = {
	{RFID_HF_NAME, HF_DATA_BUYE },
	{RFID_LF_NAME, LF_DATA_BYTE },
	{RFID_NFC_NAME, NFC_DATA_BYTE },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rfid_module_id);

static struct i2c_driver  rfid_module_driver= {
	.probe		= rfid_module_probe,
	.remove		= rfid_module_remove,
	.id_table	= rfid_module_id,
	.driver	= {
		.name	= "rfid",
		.owner	= THIS_MODULE,
	},
};

static int __init rfid_module_init(void)
{
	rfid_class = class_create(THIS_MODULE, "rfid");
	if (!rfid_class) {
		printk("%s, %s,class_create failed\n", __FILE__, __func__);
		return -EINVAL;		
	}
	return i2c_add_driver(&rfid_module_driver);
}

static void __exit rfid_module_exit(void)
{
	class_destroy(rfid_class);
	i2c_del_driver(&rfid_module_driver);
}


MODULE_AUTHOR("farsight");
MODULE_DESCRIPTION("fs6818 i2c driver");
MODULE_LICENSE("GPL");

module_init(rfid_module_init);
module_exit(rfid_module_exit);
