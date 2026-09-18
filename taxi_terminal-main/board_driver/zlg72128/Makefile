ifeq ($(KERNELRELEASE),)

#KERNELDIR ?= ~/workdir/linux/19.zlg72128/drv/
KERNELDIR ?=  /home/linux/workdir/linux-3.4.39/
#CROSS-COMPILE:=/usr/local/toolchain/toolchain-4.5.1/bin/arm-none-linux-gnueabi-
CROSS_COMPILE = arm-linux-
ROOTFS ?= /source/rootfs
PWD := $(shell pwd)

modules:
		$(MAKE) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) INSTALL_MOD_PATH=$(ROOTFS) modules_install

clean:
		rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module* modules*

.PHONY: modules modules_install clean

else
	    obj-m := key-zlg72128.o
endif

