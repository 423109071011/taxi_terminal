ifeq ($(KERNELRELEASE),)

KERNELDIR ?= /home/linux/workdir/linux-3.4.39/ 
ROOTFS ?= /source/rootfs/
CROSS_COMPILE = arm-linux-
PWD := $(shell pwd)

modules:
	$(MAKE) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(PWD) modules
modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) INSTALL_MOD_PATH=$(ROOTFS) modules_install

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module* modules*

.PHONY: modules modules_install clean

else
	obj-m += rfid_driver.o
endif
