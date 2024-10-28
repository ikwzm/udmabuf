# SPDX-License-Identifier: GPL-2.0 OR MIT
# Copyright (C) 2015-2023 Ichiro Kawazome

#
# For in kernel tree variables
# 
obj-$(CONFIG_U_DMA_BUF)                         += u-dma-buf.o

ccflags-$(CONFIG_U_DMA_BUF_DEBUG)               += -DU_DMA_BUF_DEBUG
ccflags-$(CONFIG_U_DMA_BUF_QUIRK_MMAP)          += -DU_DMA_BUF_QUIRK_MMAP
ccflags-$(CONFIG_U_DMA_BUF_IN_KERNEL_FUNCTIONS) += -DU_DMA_BUF_IN_KERNEL_FUNCTIONS
ccflags-$(CONFIG_U_DMA_BUF_IOCTL)               += -DU_DMA_BUF_IOCTL
ccflags-$(CONFIG_U_DMA_BUF_EXPORT)              += -DU_DMA_BUF_EXPORT

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
CONFIG_OPTIONS += CONFIG_U_DMA_BUF_DEBUG=$(CONFIG_U_DMA_BUF_DEBUG)
CONFIG_OPTIONS += CONFIG_U_DMA_BUF_QUIRK_MMAP=$(CONFIG_U_DMA_BUF_QUIRK_MMAP)
CONFIG_OPTIONS += CONFIG_U_DMA_BUF_IN_KERNEL_FUNCTIONS=$(CONFIG_U_DMA_BUF_IN_KERNEL_FUNCTIONS)
CONFIG_OPTIONS += CONFIG_U_DMA_BUF_IOCTL=$(CONFIG_U_DMA_BUF_IOCTL)
CONFIG_OPTIONS += CONFIG_U_DMA_BUF_EXPORT=$(CONFIG_U_DMA_BUF_EXPORT)

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

#
# For out of kernel tree rules
#
all:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $(CONFIG_OPTIONS) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $(CONFIG_OPTIONS) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

