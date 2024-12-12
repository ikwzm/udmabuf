# SPDX-License-Identifier: GPL-2.0 OR MIT
# Copyright (C) 2015-2023 Ichiro Kawazome

#
# For in kernel tree variables
# 
obj-$(CONFIG_U_DMA_BUF)  += u-dma-buf.o
ifdef U_DMA_BUF_CONFIG
ccflags-y                += -DU_DMA_BUF_CONFIG=$(U_DMA_BUF_CONFIG)
endif
ifdef U_DMA_BUF_DEBUG
ccflags-y                += -DU_DMA_BUF_DEBUG=$(U_DMA_BUF_DEBUG)
endif
ifdef U_DMA_BUF_QUIRK_MMAP
ccflags-y                += -DU_DMA_BUF_QUIRK_MMAP=$(U_DMA_BUF_QUIRK_MMAP)
endif
ifdef U_DMA_BUF_IN_KERNEL_FUNCTIONS
ccflags-y                += -DU_DMA_BUF_IN_KERNEL_FUNCTIONS=$(U_DMA_BUF_IN_KERNEL_FUNCTIONS)
endif
ifdef U_DMA_BUF_IOCTL
ccflags-y                += -DU_DMA_BUF_IOCTL=$(U_DMA_BUF_IOCTL)
endif
ifdef U_DMA_BUF_EXPORT
ccflags-y                += -DU_DMA_BUF_EXPORT=$(U_DMA_BUF_EXPORT)
endif

#
# For out of kernel tree variables
#
CONFIG_U_DMA_BUF                     ?= m
CONFIG_U_DMA_BUF_DEBUG               ?= y
CONFIG_U_DMA_BUF_QUIRK_MMAP          ?= y
CONFIG_U_DMA_BUF_IN_KERNEL_FUNCTIONS ?= y
CONFIG_U_DMA_BUF_IOCTL               ?= y
CONFIG_U_DMA_BUF_EXPORT              ?= y

CONFIG_OPTIONS := CONFIG_U_DMA_BUF=$(CONFIG_U_DMA_BUF)
CONFIG_OPTIONS += U_DMA_BUF_CONFIG=1
ifeq ($(CONFIG_U_DMA_BUF_DEBUG), y)
CONFIG_OPTIONS += U_DMA_BUF_DEBUG=1
else
CONFIG_OPTIONS += U_DMA_BUF_DEBUG=0
endif
ifeq ($(CONFIG_U_DMA_BUF_QUIRK_MMAP), y)
CONFIG_OPTIONS += U_DMA_BUF_QUIRK_MMAP=1
else
CONFIG_OPTIONS += U_DMA_BUF_QUIRK_MMAP=0
endif
ifeq ($(CONFIG_U_DMA_BUF_IN_KERNEL_FUNCTIONS), y)
CONFIG_OPTIONS += U_DMA_BUF_IN_KERNEL_FUNCTIONS=1
else
CONFIG_OPTIONS += U_DMA_BUF_IN_KERNEL_FUNCTIONS=0
endif
ifeq ($(CONFIG_U_DMA_BUF_IOCTL), y)
CONFIG_OPTIONS += U_DMA_BUF_IOCTL=2
else
CONFIG_OPTIONS += U_DMA_BUF_IOCTL=0
endif
ifeq ($(CONFIG_U_DMA_BUF_EXPORT), y)
CONFIG_OPTIONS += U_DMA_BUF_EXPORT=1
else
CONFIG_OPTIONS += U_DMA_BUF_EXPORT=0
endif

HOST_ARCH ?= $(shell uname -m | sed $(SUBARCH_SCRIPT))
ARCH      ?= $(shell uname -m | sed $(SUBARCH_SCRIPT))

SUBARCH_SCRIPT := -e s/i.86/x86/ -e s/x86_64/x86/ \
		  -e s/sun4u/sparc64/ \
		  -e s/arm.*/arm/ -e s/sa110/arm/ \
		  -e s/s390x/s390/ \
		  -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
		  -e s/sh[234].*/sh/ -e s/aarch64.*/arm64/ \
		  -e s/riscv.*/riscv/ -e s/loongarch.*/loongarch/

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

#
# For out of kernel tree rules
#
all:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $(CONFIG_OPTIONS) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $(CONFIG_OPTIONS) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

