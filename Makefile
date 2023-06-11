# SPDX-License-Identifier: GPL-2.0 OR MIT
# Copyright (C) 2015-2023 Ichiro Kawazome

#
# For in kernel tree variables
# 
obj-$(CONFIG_U_DMA_BUF) += u-dma-buf.o

#
# For out of kernel tree variables
#
HOST_ARCH ?= $(shell uname -m | sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)
ARCH      ?= $(shell uname -m | sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)

ifeq ($(ARCH), arm)
 ifneq ($(HOST_ARCH), arm)
   CROSS_COMPILE ?= arm-linux-gnueabihf-
 endif
endif
ifeq ($(ARCH), arm64)
 ifneq ($(HOST_ARCH), arm64)
   CROSS_COMPILE ?= aarch64-linux-gnu-
 endif
endif

ifdef KERNEL_SRC
  KERNEL_SRC_DIR := $(KERNEL_SRC)
else
  KERNEL_SRC_DIR ?= /lib/modules/$(shell uname -r)/build
endif

ifndef BUILD_TARGET
  ifdef KERNELVERSION
    KERNEL_VERSION_LT_5 ?= $(shell echo $(KERNELVERSION) | awk -F. '{print $$1 < 5}')
  else
    KERNEL_VERSION_LT_5 ?= $(shell awk '/^VERSION/{print int($$3) < 5}' $(KERNEL_SRC_DIR)/Makefile)
  endif
  ifeq ($(KERNEL_VERSION_LT_5), 1)
    BUILD_TARGET ?= modules
  endif
endif

ifndef CONFIG_MODULES
  CONFIG_MODULES := CONFIG_U_DMA_BUF=m
endif

#
# For out of kernel tree rules
#
all:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $(CONFIG_MODULES) $(BUILD_TARGET)

modules_install:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $(CONFIG_MODULES) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

