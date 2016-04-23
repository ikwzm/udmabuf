ARCH            := arm
KERNEL_SRC_DIR  ?= /lib/modules/$(shell uname -r)/build
ifeq ($(shell uname -m | sed -e s/arm.*/arm/),arm)
else
 CROSS_COMPILE  ?= arm-linux-gnueabihf-
endif

obj-m := udmabuf.o

all:
	make -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules

clean:
	make -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

