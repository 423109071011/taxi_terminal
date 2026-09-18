#include <linux/module.h>  
#include <linux/kernel.h>  
#include <linux/types.h>  
#include <linux/fs.h>  
#include <linux/init.h>  
#include <linux/delay.h>  
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/errno.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/uaccess.h>  
#include <asm/irq.h>  
#include <asm/io.h>  
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/cdev.h>  
#include <linux/device.h>  
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <asm/mach/map.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
//#include <mach/platform.h>
//#include <mach/devices.h> /*PAD_GPIO_*Need*/
//#include <mach/soc.h>   /*设置gpio模式使用*/
//#include "zlg72128.h"

/**********************头文件添加************************/
/*---------------------------------zlg72128寄存器定义------------------------------*/
#define ZLG72128_REG_SYSTEMREG				0x00
#define ZLG72128_REG_KEY					0x01
#define ZLG72128_REG_REPEATCNT				0x02
#define ZLG72128_REG_FUNCTIONKEY			0x03
#define ZLG72128_REG_CMDBUF0				0x07
#define ZLG72128_REG_CMDBUF1				0x08
#define ZLG72128_REG_FLAGONOFF				0x0b
#define ZLG72128_REG_DISPCTRL0				0x0c
#define ZLG72128_REG_DISPCTRL1				0x0d
#define ZLG72128_REG_FLASH0					0x0e
#define ZLG72128_REG_FLASH1					0x0f
#define ZLG72128_REG_DISPBUF0				0x10
#define ZLG72128_REG_DISPBUF(n)				(0x10|(n))
/*---------------------------------------------------------------------------------*/
#define ZLG72128_LED_DISCOUNT				12

/*---------------------------------控制命令定义------------------------------------*/
#define ZLG72128_CMD_SEGONOFF				(0x01<<4) 
#define ZLG72128_CMD_DOWNLOAD				(0x02<<4)
#define ZLG72128_CMD_RESET					(0x03<<4)
#define ZLG72128_CMD_TEST					(0x04<<4)
#define ZLG72128_CMD_SHIFTLEFT				(0x05<<4)
#define ZLG72128_CMD_CYCLICSHIFTLEFT		(0x06<<4)
#define ZLG72128_CMD_SHIFTRIGHT				(0x07<<4)
#define ZLG72128_CMD_CYCLICSHIFTRIGHT		(0x08<<4)
#define ZLG72128_CMD_SCANNING				(0x09<<4)
/*---------------------------------------------------------------------------------*/

#define contain_of(ptr, type, member) ({						\
		const typeof( ((type *)0)->member ) *__mptr = (struct delayed_work *)(ptr);    \
		(type *)( (char *)__mptr - offsetof(type,member) );})

#define ZLG72128_DRIVER_NAME				"zlg72128"
/**
 * \brief ZLG72128从机地址（用户可检测A4的电平来确定当前ZLG72128模块的从机地址）
 *        若引脚A4为浮空或者高电平，则从机地址为0x30
 *        若引脚A4为低电平，则从机地址为0x20
 */
#define  ZLG72128_PIN_HIGH_SLV_ADDR  0x30
#define  ZLG72128_PIN_LOW_SLV_ADDR   0x20
/*zlg72128设备名*/
#define ZLG72128_LEDDEV_NAME				"i2cLED"
#define ZLG72128_KEYDEV_NAME				"i2cKEY"
/*普通按键延时释放*/
#define ZLG72128_KEY_RELEASE				100
#define ZLG72128_KEY_READ_RELEASE_COUNT		3
/*最大支持按键数*/
#define ZLG72128_MAX_KEY_COUNT				32
#define ZLG72128_MAX_COMMON_KEY_COUNT		24

#define ZLG72128_MAX_DEV_COUNT				8


/*-------------------------------------------------------------------*/
/*----------------------------ioctl 控制命令-------------------------*/
/*-------------------------------------------------------------------*/
#define ZLG72128_MAGIC                  'F'
#if 1
#define ZLG72128_DIGITRON_DISP_CTRL			_IOC(_IOC_WRITE,ZLG72128_MAGIC,0,0)
#define ZLG72128_DIGITRON_DISP_CHAR			_IOC(_IOC_WRITE,ZLG72128_MAGIC,1,0)
#define ZLG72128_DIGITRON_DISP_STR			_IOC(_IOC_WRITE,ZLG72128_MAGIC,2,0)
#define ZLG72128_DIGITRON_DISP_NUM			_IOC(_IOC_WRITE,ZLG72128_MAGIC,3,0)
#define ZLG72128_DIGITRON_DISPBUF_SET		_IOC(_IOC_WRITE,ZLG72128_MAGIC,4,0)
#define ZLG72128_DIGITRON_SEG_CTRL			_IOC(_IOC_WRITE,ZLG72128_MAGIC,5,0)
#define ZLG72128_DIGITRON_FLASH_CTRL		_IOC(_IOC_WRITE,ZLG72128_MAGIC,6,0)
#define ZLG72128_DIGITRON_FLASH_TIME_CFG	_IOC(_IOC_WRITE,ZLG72128_MAGIC,7,0)
#define ZLG72128_DIGITRON_SHIFT				_IOC(_IOC_WRITE,ZLG72128_MAGIC,8,0)
#define ZLG72128_DIGITRON_DISP_RESET		_IOC(_IOC_WRITE,ZLG72128_MAGIC,9,0)
#define ZLG72128_DIGITRON_DISP_TEST			_IOC(_IOC_WRITE,ZLG72128_MAGIC,10,0)
#define ZLG72128_DIGITRON_RESET				_IOC(_IOC_WRITE,ZLG72128_MAGIC,11,0)
#define ZLG72128_DIGITRON_KEY_READ          _IOR(ZLG72128_MAGIC,1,int)  

#endif
/*-------------------------------------------------------------------*/
/*-----------------------------相关宏定义----------------------------*/
/*-------------------------------------------------------------------*/

/*
 * ioctl命令返回值 : -2参数错误, -1执行失败, 0执行成功
 * */
#define ZLG72128_RETURN_PARAMETER_ERROR		-2
#define	ZLG72128_RETURN_ERROR				-1
#define ZLG72128_RETURN_OK					0


int irq_flag = 0;
int transmit_value[1] = {0};
//static spinlock_t lock;
/**
 * \name 数码管显示移位的方向
 * 用于 \sa zlg72128_digitron_shift() 函数的 \a dir 参数。
 */
#define  ZLG72128_DIGITRON_SHIFT_RIGHT   0   /**< \brief 右移  */
#define  ZLG72128_DIGITRON_SHIFT_LEFT    1   /**< \brief 左移  */

/*-------------------------------------------------------------------*/
/*---------------------对应ioctl命令所使用的结构体-------------------*/
/*-------------------------------------------------------------------*/

/* 功能 : 显示字符串
 * 成员 : 
 *		start_pos	:	字符串显示起始位置 
 *		p_str		:	字符串
 * 说明 :
 *		字符串显示遇到字符结束标志"\0"将自动结束，或当超过有效的字符显示区域时，也会自动结束。显示的字符应确保是ZLG72128能够自动完成译码的，包括字符'0'~'9'与AbCdEFGHiJLopqrtUychT（区分大小写）。如遇到有不支持的字符，对应位置将不显示任何内容
 * */
struct zlg72128_digitron_disp_str_t{
	unsigned char start_pos;
	unsigned char p_str[12];
};

/* 功能 : 显示0~9的数字
 * 成员 : 
 *		pos : 数字显示位置(0~11)
 *		num : 显示的数字(0~9)
 *		is_dp_disp	: 是否显示小数点(TRUE显示, FALSE不显示)
 *		is_flash	: 是否闪烁(TRUE闪烁, FALSE不闪烁)
 * 说明 : 
 *		该函数仅用于显示一个0~9的数字，若数字大于9，应自行根据需要分别显示各个位。注意，num参数为数字0~9，不是字符'0'~'9'
 * */
struct zlg72128_digitron_disp_num_t{
	unsigned char pos;
	unsigned char num;
	unsigned char is_dp_disp;
	unsigned char is_flash;
};


/************************头文件添加********************************/

/*默认按键键值*/
/*static int key_list_def[ZLG72128_MAX_KEY_COUNT] = {
	KEY_1, KEY_2, KEY_3, KEY_4,    KEY_5, KEY_6, KEY_7, KEY_8, 
	KEY_9, KEY_A, KEY_B, KEY_C,    KEY_D, KEY_E, KEY_F, KEY_G, 
	KEY_H, KEY_I, KEY_J, KEY_K,    KEY_L, KEY_M, KEY_N, KEY_O, 
	KEY_F1, KEY_F2, KEY_F3, KEY_F4,KEY_F5, KEY_F6, KEY_F7, KEY_F8, 
}; */

static int key_list_def[ZLG72128_MAX_KEY_COUNT] = {
	  29,  2,  3,  4,    5, 6, 7, 8,
	   9, 10, 11, 12, 12, 13, 14, 15, 16,
	  17, 18, 19, 20, 21, 22, 23, 24,
	  25, 26, 27, 253, 239, 223, 191, 127,
};
/* 按键信息结构体
 * keyId        :   按键号，1～24为普通按键，0xf0~0xf7为功能按键，0为无效按键
 * keyFlag      :   按键的类型，0为普通按键，1为功能按键
 * keyCode      :   键值
 * keyRepeatCnt :   如果为普通按键，则代表连按次数
 * keyStatus    :   按键状态，1为按下，0为弹起
 * */
struct ZLG72128_KEY{
    int keyId;
    int keyFlag;
    int keyCode;
    int keyRepeatCnt;
    int keyStatus;
};

/*zlg72128设备信息结构体*/
struct zlg72128_dev_t {
	int addr;								/*从设备地址*/
	int status;								/*用于表示当前设备状态 0:未被使用 1:已使用*/
	struct miscdevice zlg72128_misc;		/*杂项设备*/
	struct i2c_adapter *zlg72128_adapter;	/*i2c adapter*/
	int irq_gpio;							/*中断所使用的gpio*/   //PD10
	struct work_struct keyboard_work;		/*用于键盘读取中断*/
	struct workqueue_struct *rescue_wq;		/*用于键盘读取中断*/
	struct input_dev *zlg72128_key;			/*键盘输入设备*/
	struct timer_list timer;   /*定时器, 用于普通按键释放延时*/
	int keyRelease;							/*普通按键实际延时*/
	int keys_count;							/*有效注册的按键*/
	int functionKey;						/*记录特殊按键的值，用于判断释放顺序*/
	struct ZLG72128_KEY keys[ZLG72128_MAX_KEY_COUNT]; /*自定义按键*/
	struct delayed_work work;
};

int retflag = 0;
int replace[12] = {0};
/*设备树信息获取结构体*/
struct zlg72128_dev_data {
    int i2cline;    //i2c总线
    int sla;		//i2c设备地址
    int *key_list;  //键盘值映射，第0~23个元素分别代表K1~K23普通按键键值，第24~31个元素分别代表F1~F8特殊按键键值，0为无效键值
};

/*-----------------------------相关全局变量---------------------------*/
static struct zlg72128_dev_t zlg72128_devs[ZLG72128_MAX_DEV_COUNT];  //结构体数组
static char *zlg72128_dev_name[ZLG72128_MAX_DEV_COUNT] = {
	"zlg72128-0","zlg72128-1", "zlg72128-2", "zlg72128-3", 
	"zlg72128-4","zlg72128-5", "zlg72128-6", "zlg72128-7",
};



/*--------------------------------i2c 数据传输----------------------------------*/
/*双字节写入*/
 static int zlg72128_write(struct zlg72128_dev_t *zdev, int offset, char value0, char value1, int count)
{
	unsigned char data[2];
	struct i2c_msg msg;

	data[0]     =   offset;
	data[1]     =   value0 | value1;

	msg.addr    =   zdev->addr;
	msg.flags   =   0;
	msg.buf     =   data;
	msg.len     =   count;

	if(i2c_transfer(zdev->zlg72128_adapter,&msg,1)!=1)
		return ZLG72128_RETURN_ERROR;

	return ZLG72128_RETURN_OK;

} 
/*读单字节数据*/
static int zlg72128_read(struct zlg72128_dev_t *zdev,int offset,char *data,int count)
{
	unsigned char wr_data[1];
	struct i2c_msg msg[2];

	wr_data[0] = offset;
	msg[0].addr     =   zdev->addr;
	msg[0].flags    =   0;
	msg[0].buf      =   wr_data;
	msg[0].len      =   1;

	msg[1].addr     =   zdev->addr;
	msg[1].flags    =   I2C_M_RD;
	msg[1].buf      =   data;    //读取到的数据
	msg[1].len      =   count;

	if(i2c_transfer(zdev->zlg72128_adapter,msg,2)!=2)
		return ZLG72128_RETURN_ERROR;
//	printk("read over \n");
	return ZLG72128_RETURN_OK;
}

/* 校验keyid是否在keys列表中
 * 返回值：
 *		0：按键无效
 *		1：按键有效
 * */
static int is_keyid_in_list(struct zlg72128_dev_t *zdev,int keyid)
{
	int i;
	for(i=0;i<zdev->keys_count;i++){
		if(keyid == zdev->keys[i].keyId)
			return i+1;
	}
	return 0;
}

/*中断处理*/  
static void keyboard_work_handle(struct work_struct *data)
{
	unsigned char regfunctionkey=0;
	int regkey;
	unsigned char ret=0,tmp=0;
	struct zlg72128_dev_t *zdev=(struct zlg72128_dev_t *)contain_of(data,struct zlg72128_dev_t,work);  //keyboard_work;
	regkey = 100;

	/*首先读取key寄存器的值，如果key为0则可能为功能按键，否则为普通按键*/
		ret = zlg72128_read(zdev,ZLG72128_REG_KEY,(char *)(&regkey),1);
		if(ret)
			goto error;
#if 1
		if(regkey)
		{
			/*判断按键是否在keys列表中*/
			ret = is_keyid_in_list(zdev,regkey);
			if(!ret){
				printk("the keyid %d is unuse\n",regkey);
				return;
			}
			printk("<0>""press common key is 0x%02x, ret = %d\n",regkey,ret);

			ret -= 1;                //用于数组下标

			input_report_key(zdev->zlg72128_key,zdev->keys[ret].keyCode,0);
			input_report_key(zdev->zlg72128_key,zdev->keys[ret].keyCode,1);
			input_sync(zdev->zlg72128_key);
		}
		else
		{
			if(!zdev->functionKey)
				zdev->functionKey = 0xff;

			ret = zlg72128_read(zdev,ZLG72128_REG_FUNCTIONKEY,(char *)(&regfunctionkey),1);
			if(ret)
				goto error;

			tmp = regfunctionkey ^ zdev->functionKey;
			zdev->functionKey = regfunctionkey;
			regfunctionkey = (~tmp) & 0xff;

			/*有按键改变*/
			if(regfunctionkey != 0xff){
				/*判断按键是否在keys列表中*/
				ret = is_keyid_in_list(zdev,regfunctionkey);
				if(!ret){
					printk("the keyid 0x%x is unuse\n",regfunctionkey);
					return;
				}
				printk("<0>" "--regfunctionkey = %d \n",regfunctionkey);
				ret -= 1;
				if(zdev->keys[ret].keyStatus){
					/*开始释放按下的按键值*/
					zdev->keys[ret].keyStatus = 0;
					input_report_key(zdev->zlg72128_key,zdev->keys[ret].keyCode,0);
					input_sync(zdev->zlg72128_key);
				}
				else{
					//开始上报按下的按键值
					zdev->keys[ret].keyStatus = 1;
					input_report_key(zdev->zlg72128_key,zdev->keys[ret].keyCode,1);
					input_sync(zdev->zlg72128_key);
				}
			}
		}
#endif
		schedule_delayed_work(&zdev->work, HZ / 5);
	return;

error:
	printk("<0>""--> Error : handle keywork <--\n");
}

/*键盘初始化*/
static int zlg72128_btns_creat(struct device *dev, struct zlg72128_dev_t *zdev, int *keys)
{
	int i=0,ret=0;
	/*初始化输入设备*/
	zdev->zlg72128_key = input_allocate_device();    //分配设备结构体

	if(zdev->zlg72128_key == NULL){
		printk("<0>""input allocate device error!!\n");
		ret = -ENOMEM;
		goto input_alloc_err;
	}

	zdev->zlg72128_key->name = ZLG72128_KEYDEV_NAME;
	zdev->zlg72128_key->evbit[0] = BIT_MASK(EV_KEY) ;          //  | BIT_MASK(EV_REP);   //设置按键信息
	zdev->keys_count = 0;

	for(i=0;i<32;i++)
		input_set_capability(zdev->zlg72128_key, EV_KEY, zdev->keys[i].keyCode);

	/*根据zlg72128_keys_list设置支持的按键*/
	for(i=0,zdev->keys_count=0;i<ZLG72128_MAX_KEY_COUNT;i++,zdev->keys_count++){
	
		/*按键值无效*/
		if(keys[i] < 0)
			continue;

		/*初始化可使用的按键列表*/
		if(i < ZLG72128_MAX_COMMON_KEY_COUNT){
			/*普通按键*/
			zdev->keys[zdev->keys_count].keyId = i + 1;   //得到键值
		}else{
			/*功能按键*/
			zdev->keys[zdev->keys_count].keyId = (~(1<<(i-ZLG72128_MAX_COMMON_KEY_COUNT))) & 0xff;
			zdev->keys[zdev->keys_count].keyFlag = 1;
		}
		zdev->keys[zdev->keys_count].keyCode = keys[i];

		/*把键码加入到输入设备*/
		set_bit(zdev->keys[zdev->keys_count].keyCode,zdev->zlg72128_key->keybit);

//		printk("<0>""key bit = %d \n",zdev->keys[zdev->keys_count].keyCode);
	}
 
	/*工作队列*/
	INIT_DELAYED_WORK(&zdev->work, keyboard_work_handle);
	schedule_delayed_work(&zdev->work, HZ / 5);
	
	/*注册输入设备*/
	ret = input_register_device(zdev->zlg72128_key);
	if(ret){
		input_free_device(zdev->zlg72128_key);
		printk("input register error!\n");
	}
	return 0;

input_alloc_err:
	return ret;
}

typedef struct zlg72128_dev_t   *zlg72128_handle_t;

/**
 * \brief 设置数码管显示的字符
 *
 */
static int zlg72128_digitron_disp_char (zlg72128_handle_t  handle,
                                 unsigned char      pos,
                                 char               ch,
                                 unsigned char      is_dp_disp,
                                 unsigned char      is_flash)
{
	/*参数检测*/

		return zlg72128_write(handle,pos,ch,is_dp_disp,2);
}

/**
 * \brief 设置数码管显示的数字
 *
 * \param[in] handle     : ZLG72128的操作句柄
 * \param[in] pos        : 本次显示的位置，有效值 0 ~ 11
 * \param[in] num        : 显示的数字，
 * \param[in] is_dp_disp : 是否显示小数点，1:显示; 0:不显示
 * \param[in] is_flash   : 该位是否闪烁，1:闪烁; 0:不闪烁
 *
 * \retval -2 : 参数错误
 * \retval -1 : 执行失败
 * \retval  0 : 执行成功
 */
static int zlg72128_digitron_disp_num (zlg72128_handle_t  handle,
                                unsigned char      pos,
                                unsigned char      num,
                                unsigned char      is_dp_disp,
                                unsigned char      is_flash)
{
	char regkey[8]={0};
	int ret = 0,i = 0;

	//首先读数码管第一位有没有显示
	printk("addr = %#x \n",handle->addr);
	ret = zlg72128_read(handle,ZLG72128_REG_DISPBUF0,(char *)(regkey),8);
	
	if(regkey[0] == 0)
			zlg72128_digitron_disp_char(handle, ZLG72128_REG_DISPBUF0, num, is_dp_disp, is_flash);
	else
	{
		for(i=0;i<7;i++)
		{
			zlg72128_digitron_disp_char(handle, ZLG72128_REG_DISPBUF(i+1), regkey[i], is_dp_disp, is_flash);
		}
		zlg72128_digitron_disp_char(handle, ZLG72128_REG_DISPBUF0, num, is_dp_disp, is_flash);
	}
	return 0;
}

/*-------------------------------ioctl控制----------------------------*/
static long zlg72128_ioctl(struct file *flip,unsigned int cmd,unsigned long arg)
{
	int ret = ZLG72128_RETURN_OK;
	struct zlg72128_dev_t *zdev = flip->private_data;
	switch(cmd){

		case ZLG72128_DIGITRON_DISP_NUM :
			{
				ret = zlg72128_digitron_disp_num(zdev,1,arg,0,0);
			}break;
		case ZLG72128_DIGITRON_DISPBUF_SET :
			{
				/* 应用层传入 8 字节段码缓冲(用户态指针)，直接整屏写入 DISPBUF0~7。
				 * DISPBUF0 = 最左位。不经过 DISP_NUM 的移位逻辑，可任意清屏/整屏刷新。 */
				unsigned char kbuf[8];
				int di;
				if (copy_from_user(kbuf, (unsigned char __user *)arg, sizeof(kbuf))) {
					ret = -EFAULT;
				} else {
					for (di = 0; di < 8; di++)
						zlg72128_digitron_disp_char(zdev, ZLG72128_REG_DISPBUF(di), kbuf[di], 0, 0);
					ret = ZLG72128_RETURN_OK;
				}
			}break;
		default: ret = ZLG72128_RETURN_PARAMETER_ERROR; break;
	}

	return ret;
}

static int zlg72128_open (struct inode *node, struct file *flip)   //应用层执行open打开
{
	int i=0;
	
	flip->private_data = &zlg72128_devs[i];

	return 0;
}
#if 1
static ssize_t zlg72128_usr_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{

	return 0;
}
#endif
static struct file_operations zlg72128_fops = {  
	.owner  =   THIS_MODULE,
	.open   = zlg72128_open,
	.read	= zlg72128_usr_read,
	.unlocked_ioctl = zlg72128_ioctl,
};  

int zlg72128_probe(struct i2c_client *i2c_client,const  struct i2c_device_id *id)
{
	int zlg72128_dev_count=0;
	int ret=0;
	struct zlg72128_dev_data dev_data;
	printk("<0>"": This is zlg72128 probe function!\n");                   // OK
	for(zlg72128_dev_count=0;zlg72128_dev_count<ZLG72128_MAX_DEV_COUNT;zlg72128_dev_count++){
		if(!zlg72128_devs[zlg72128_dev_count].status){
			break;
		}
	}

	if(zlg72128_dev_count >= ZLG72128_MAX_DEV_COUNT){
		printk("zlg72128 dev count : %d --> too much device for zlg72128!!\n",zlg72128_dev_count);
		return -EINVAL;
	}

	memset(&dev_data,0,sizeof(dev_data));
	
	/*填充设备信息结构体*/	
	dev_data.key_list = key_list_def;
	
	zlg72128_devs[zlg72128_dev_count].addr = ZLG72128_PIN_HIGH_SLV_ADDR;    //地址为0x30
	zlg72128_devs[zlg72128_dev_count].keyRelease = ZLG72128_KEY_RELEASE;
	zlg72128_devs[zlg72128_dev_count].zlg72128_adapter = i2c_client->adapter;

	/*按键设备初始化*/
	ret = zlg72128_btns_creat(&i2c_client->dev,&zlg72128_devs[zlg72128_dev_count],dev_data.key_list);
	if(ret < 0){
		printk("zlg72128-%d btn create error!!!\n",zlg72128_dev_count);    //error
		goto btns_creat_err;
	}

	/*初始化并注册杂项设备*/  
	zlg72128_devs[zlg72128_dev_count].zlg72128_misc.minor	=	MISC_DYNAMIC_MINOR;		//自动分配次设备号
	zlg72128_devs[zlg72128_dev_count].zlg72128_misc.name	=	zlg72128_dev_name[zlg72128_dev_count];	
	zlg72128_devs[zlg72128_dev_count].zlg72128_misc.fops	=	&zlg72128_fops;			//操作函数集

	ret = misc_register(&zlg72128_devs[zlg72128_dev_count].zlg72128_misc);//////////
	if(ret){
		printk("zlg72128 misc register error!!\n");
		goto misc_register_err;
	}
	zlg72128_devs[zlg72128_dev_count].status = 1;
	
	return 0;

misc_register_err:
	input_unregister_device(zlg72128_devs[zlg72128_dev_count].zlg72128_key);
	input_free_device(zlg72128_devs[zlg72128_dev_count].zlg72128_key);
btns_creat_err:
	gpio_free(zlg72128_devs[zlg72128_dev_count].irq_gpio);
	return ret;
}

static int zlg72128_remove(struct i2c_client *i2c_client)
{
	int i=0;
		zlg72128_devs[i].status = 0;
		input_unregister_device(zlg72128_devs[i].zlg72128_key);
		
		misc_deregister(&zlg72128_devs[i].zlg72128_misc);
		cancel_delayed_work(&zlg72128_devs[i].work);
		i2c_set_clientdata(i2c_client, NULL);

	printk("This is zlg72128 remove function!\n");
	return 0;
}

static const struct  i2c_device_id zlg72128_id_table[] = 
{
	{ZLG72128_DRIVER_NAME,0},
	{ }
};
/*
static const struct of_device_id  zlg72128_dt_ids[] = {
    { .compatible = "zlg72128"},
    { }  
};
*/
static struct i2c_driver zlg72128_driver = {
	.probe = zlg72128_probe,
    .remove = zlg72128_remove,
    .driver = {
        .owner = THIS_MODULE,
		.name = ZLG72128_DRIVER_NAME,
 //       .of_match_table = zlg72128_dt_ids
    },
	.id_table = zlg72128_id_table 
};

static int __init zlg72128_init(void)
{
	int ret;
    ret = i2c_add_driver(&zlg72128_driver);
	if(ret < 0)
	{
		printk("zlg72128_driver error!\n");
		return ret;
	}
	printk("zlg72128_driver added!\n");           //OK
	return 0;
}

static void __exit zlg72128_exit(void)
{
    i2c_del_driver(&zlg72128_driver);
	printk("zlg72128 driver deleted!\n");
}

module_init(zlg72128_init);
module_exit(zlg72128_exit);

MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("mengfy1995@hotmail.com");
