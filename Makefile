HOST_ARCH       ?= $(shell uname -m | sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)
ARCH            ?= $(shell uname -m | sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)

ifdef KERNEL_SRC
  KERNEL_SRC_DIR  := $(KERNEL_SRC)
else
  KERNEL_SRC_DIR  ?= /lib/modules/$(shell uname -r)/build
endif

ifeq ($(ARCH), arm)
 ifneq ($(HOST_ARCH), arm)
   CROSS_COMPILE  ?= arm-linux-gnueabihf-
 endif
endif
ifeq ($(ARCH), arm64)
 ifneq ($(HOST_ARCH), arm64)
   CROSS_COMPILE  ?= aarch64-linux-gnu-
 endif
endif

u-dma-buf-obj               := u-dma-buf.o
u-dma-buf-mgr-obj           := u-dma-buf-mgr.o
obj-$(CONFIG_U_DMA_BUF)     += $(u-dma-buf-obj)
obj-$(CONFIG_U_DMA_BUF_MGR) += $(u-dma-buf-mgr-obj)

ifndef UDMABUF_MAKE_TARGET
  KERNEL_VERSION_LT_5 ?= $(shell awk '/^VERSION/{print int($$3) < 5}' $(KERNEL_SRC_DIR)/Makefile)
  ifeq ($(KERNEL_VERSION_LT_5), 1)
    UDMABUF_MAKE_TARGET ?= modules
  else
    UDMABUF_MAKE_TARGET ?= u-dma-buf.ko
    ifdef CONFIG_U_DMA_BUF_MGR
      UDMABUF_MAKE_TARGET += u-dma-buf-mgr.ko
    endif
    ifdef CONFIG_U_DMA_BUF_KMOD_TEST
      UDMABUF_MAKE_TARGET += u-dma-buf-kmod-test.ko
    endif
  endif
endif

OBJ-MODULES := obj-m=$(u-dma-buf-obj)

ifdef CONFIG_U_DMA_BUF_MGR
  OBJ-MODULES += obj-m+=$(u-dma-buf-mgr-obj)
endif
ifdef CONFIG_U_DMA_BUF_KMOD_TEST
  OBJ-MODULES += obj-m+=u-dma-buf-kmod-test.o
endif

all:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $(OBJ-MODULES) $(UDMABUF_MAKE_TARGET)

modules_install:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) $(OBJ-MODULES) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

