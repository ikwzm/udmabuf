u-dma-buf(User space mappable DMA Buffer)
==================================================================================

# Overview

## Introduction of u-dma-buf

u-dma-buf is a Linux device driver that allocates contiguous memory blocks in the
kernel space as DMA buffers and makes them available from the user space.
It is intended that these memory blocks are used as DMA buffers when a user 
application implements device driver in user space using UIO (User space I/O).

A DMA buffer allocated by u-dma-buf can be accessed from the user space by opening
the device file (e.g. /dev/udmabuf0) and mapping to the user memory space, or
using the read()/write() functions.

CPU cache for the allocated DMA buffer can be disabled by setting the `O_SYNC` flag
when opening the device file. It is also possible to flush or invalidate CPU cache
while retaining CPU cache enabled.

The physical address of a DMA buffer allocated by u-dma-buf can be obtained by
reading `/sys/class/u-dma-buf/udmabuf0/phys_addr`.

The size of a DMA buffer and the device minor number can be specified when 
the device driver is loaded (e.g. when loaded via the `insmod` command).
Some platforms allow to specify them in the device tree.

## Architecture of u-dma-buf

![Figure 1. Architecture ](./u-dma-buf-1.jpg "Figure 1. Architecture")

Figure 1. Architecture

<br />

## Supported platforms

* OS : Linux Kernel Version 3.6 - 3.8, 3.18, 4.4, 4.8, 4.12, 4.14, 4.19, 5.0 - 5.10 (the author tested on 3.18, 4.4, 4.8, 4.12, 4.14, 4.19, 5.4, 5.10).
* CPU: ARM Cortex-A9 (Xilinx ZYNQ / Altera CycloneV SoC)
* CPU: ARM64 Cortex-A53 (Xilinx ZYNQ UltraScale+ MPSoC)
* CPU: x86(64bit) However, verification is not enough. I hope the results from everyone.
  In addition, there is a limit to the following feature at the moment.
  - Can not control of the CPU cache by O_SYNC flag . Always CPU cache is valid.
  - Can not various settings by the device tree.

## Note: udmabuf to u-dma-buf

### Why u-dma-buf instead of udmabuf

The predecessor of u-dma-buf is udmabuf. The kernel module name has been changed
from "udmabuf" to "u-dma-buf". The purpose of this is to avoid duplicate names
because another kernel module with the same name as "udmabuf" has been added since
Linux Kernel 5.x.

### Changes from udmabuf to u-dma-buf

| Categoly            | udmabuf                | u-dma-buf               |
|:--------------------|:-----------------------|:------------------------|
| module name         | udmabuf.ko             | u-dma-buf.ko            |
| source file         | udmabuf.c              | u-dma-buf.c             |
| sys class name      | /sys/class/udmabuf/    | /sys/class/u-dma-buf/   |
| DT compatible prop. | "ikwzm,udmabuf-0.10.a" | "ikwzm,u-dma-buf"       |

# Usage

## Compile

The following `Makefile` is included in the repository.

```Makefile:Makefile
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

u-dma-buf-obj           := u-dma-buf.o
obj-$(CONFIG_U_DMA_BUF) += $(u-dma-buf-obj)

ifndef UDMABUF_MAKE_TARGET
  KERNEL_VERSION_LT_5 ?= $(shell awk '/^VERSION/{print int($$3) < 5}' $(KERNEL_SRC_DIR)/Makefile)
  ifeq ($(KERNEL_VERSION_LT_5), 1)
    UDMABUF_MAKE_TARGET ?= modules
  else
    UDMABUF_MAKE_TARGET ?= u-dma-buf.ko
  endif
endif

all:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) obj-m=$(u-dma-buf-obj) $(UDMABUF_MAKE_TARGET)

modules_install:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) obj-m=$(u-dma-buf-obj) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

```

## Install

Load the u-dma-buf kernel driver using `insmod`. The size of a DMA buffer should be
provided as an argument as follows.
The device driver is created, and allocates a DMA buffer with the specified size.
The maximum number of DMA buffers that can be allocated using `insmod` is 8 (udmabuf0/1/2/3/4/5/6/7).

```console
zynq$ insmod u-dma-buf.ko udmabuf0=1048576
u-dma-buf udmabuf0: driver installed
u-dma-buf udmabuf0: major number   = 248
u-dma-buf udmabuf0: minor number   = 0
u-dma-buf udmabuf0: phys address   = 0x1e900000
u-dma-buf udmabuf0: buffer size    = 1048576
u-dma-buf udmabuf0: dma coherent   = 0
zynq$ ls -la /dev/udmabuf0
crw------- 1 root root 248, 0 Dec  1 09:34 /dev/udmabuf0
```

In the above result, the device is only read/write accessible by root.
If the permission needs to be changed at the load of the kernel module,
create `/etc/udev/rules.d/99-u-dma-buf.rules` with the following content.

```rules:99-u-dma-buf.rules
SUBSYSTEM=="u-dma-buf", GROUP="root", MODE="0666"
```

The module can be uninstalled by the `rmmod` command.

```console
zynq$ rmmod u-dma-buf
u-dma-buf udmabuf0: driver uninstalled
```

### Installation with the Debian package

For details, refer to the following URL.

*  https://github.com/ikwzm/u-dma-buf-kmod-dpkg


## Configuration via the device tree file

In addition to the allocation via the `insmod` command and its arguments, DMA
buffers can be allocated by specifying the size in the device tree file.
When a device tree file contains an entry like the following, u-dma-buf will
allocate buffers and create device drivers when loaded by `insmod`.

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			device-name = "udmabuf0";
			minor-number = <0>;
			size = <0x00100000>;
		};

```

```console
zynq$ insmod u-dma-buf.ko
u-dma-buf udmabuf0: driver installed
u-dma-buf udmabuf0: major number   = 248
u-dma-buf udmabuf0: minor number   = 0
u-dma-buf udmabuf0: phys address   = 0x1e900000
u-dma-buf udmabuf0: buffer size    = 1048576
u-dma-buf udmabuf0: dma coherent  = 0
zynq$ ls -la /dev/udmabuf0
crw------- 1 root root 248, 0 Dec  1 09:34 /dev/udmabuf0
```

The following properties can be set in the device tree.

  *  `compatible`
  *  `size`
  *  `minor-number`
  *  `device-name`
  *  `sync-mode`
  *  `sync-always`
  *  `sync-offset`
  *  `sync-size`
  *  `sync-direction`
  *  `dma-coherent`
  *  `dma-mask`
  *  `memory-region`


### `compatible`

The `compatible` property is used to set the corresponding device driver when loading
u-dma-buf. The `compatible` property is mandatory. Be sure to specify `compatible`
property as "ikwzm,u-dma-buf" (for u-dma-buf.ko) or "ikwzm,udmabuf-0.10.a" (for udmabuf.ko).

### `size`

The `size` property is used to set the capacity of DMA buffer in bytes.
The `size` property is mandatory.

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			size = <0x00100000>;
		};

```

If you want to specify a buffer size of 4GiB or more, specify a 64bit value as follows.
A 64-bit value is expressed by arranging two in the order of upper 32 bits and lower 32 bits.

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			size = <0x01 0x00000000>;  // size = 0x1_0000_0000
		};

```

### `minor-number`

The `minor-number` property is used to set the minor number.
The valid minor number range is 0 to 255. A minor number provided as `insmod`
argument will has higher precedence, and when definition in the device tree has
colliding number, creation of the device defined in the device tree will fail.

The `minor-number` property is optional. When the `minor-number` property is not
specified, u-dma-buf automatically assigns an appropriate one.

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			minor-number = <0>;
			size = <0x00100000>;
		};

```

### `device-name`

The `device-name` property is used to set the name of device.

The `device-name` property is optional. The device name is determined as follow:

  1. If `device-name` property is specified, the value of `device-name` property is used.
  2. If `device-name` property is not present, and if `minor-number` property is
     specified, `sprintf("udmabuf%d", minor-number)` is used.
  3. If `device-name` property is not present, and if `minor-number` property is
     not present, the entry name of the device tree is used (`udmabuf@0x00` in this example).

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			device-name = "udmabuf0";
			size = <0x00100000>;
		};

```

### `sync-mode`

The `sync-mode` property is used to configure the behavior when u-dma-buf is opened
with the `O_SYNC` flag.

  * `sync-mode`=<1>: If `O_SYNC` is specified or `sync-always` property is specified,
    CPU cache is disabled. Otherwise CPU cache is enabled.
  * `sync-mode`=<2>: If `O_SYNC` is specified or `sync-always` property is specified,
     CPU cache is disabled but CPU uses write-combine when writing data to DMA buffer
     improves performance by combining multiple write accesses. Otherwise CPU cache is
     enabled.
  * `sync-mode`=<3>: If `O_SYNC` is specified or `sync-always` property is specified,
     DMA coherency mode is used. Otherwise CPU cache is enabled.

The `sync-mode` property is optional.
When the `sync-mode` property is not specified, `sync-mode` is set to <1>.

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			size = <0x00100000>;
			sync-mode = <2>;
		};

```

Details on `O_SYNC` and cache management will be described in the next section.

### `sync-always`

If the `sync-always` property is specified, when opening u-dma-buf, it specifies that
the operation specified by the `sync-mode` property will always be performed
regardless of `O_SYNC` specification.

The `sync-always` property is optional. 

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			size = <0x00100000>;
			sync-mode = <2>;
			sync-always;
		};

```

Details on `O_SYNC` and cache management will be described in the next section.

### `sync-offset`

The `sync-offset` property is used to set the start of the buffer range when manually
controlling the cache of u-dma-buf. 

The `sync-offset` property is optional.
When the `sync-offset` property is not specified, `sync-offset` is set to <0>.

Details on cache management will be described in the next section.


### `sync-size`

The `sync-size` property is used to set the size of the buffer range when manually
controlling the cache of u-dma-buf.

The `sync-size` property is optional.
When the `sync-size` property is not specified, `sync-size` is set to <0>.

Details on cache management will be described in the next section.


### `sync-direction`

The `sync-direction` property is used to set the direction of DMA when manually
controlling the cache of u-dma-buf.

  * `sync-direction`=<0>: DMA_BIDIRECTIONAL
  * `sync-direction`=<1>: DMA_TO_DEVICE
  * `sync-direction`=<2>: DMA_FROM_DEVICE

The `sync-direction` property is optional.
When the `sync-direction` property is not specified, `sync-direction` is set to <0>.

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			size = <0x00100000>;
			sync-offset = <0x00010000>;
			sync-size = <0x000F0000>;
			sync-direction = <2>;
		};

```

Details on cache management will be described in the next section.


### `dma-coherent`

If the `dma-coherent` property is specified, indicates that coherency between DMA
buffer and CPU cache can be guaranteed by hardware.

The `dma-coherent` property is optional. When the `dma-coherent` property is not
specified, indicates that coherency between DMA buffer and CPU cache can not be
guaranteed by hardware.

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			size = <0x00100000>;
			dma-coherent;
		};

```

Details on cache management will be described in the next section.


### `dma-mask`

** Note: The value of dma-mask is system dependent.
Make sure you are familiar with the meaning of dma-mask before setting. **

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,u-dma-buf";
			size = <0x00100000>;
			dma-mask = <64>;
		};
```

### `memory-region`

Linux can specify the reserved memory area in the device tree. The Linux kernel
excludes normal memory allocation from the physical memory space specified by
`reserved-memory` property.
In order to access this reserved memory area, it is nessasary to use a
general-purpose memory access driver such as `/dev/mem`, or associate it with
the device driver in the device tree.

By the `memory-region` property, it can be associated the reserved memory area with u-dma-buf.

```devicetree:devicetree.dts
	reserved-memory {
		#address-cells = <1>;
		#size-cells = <1>;
		ranges;
		image_buf0: image_buf@0 {
			compatible = "shared-dma-pool";
			reusable;
			reg = <0x3C000000 0x04000000>; 
			label = "image_buf0";
		};
	};
	udmabuf@0 {
		compatible = "ikwzm,u-dma-buf";
		device-name = "udmabuf0";
		size = <0x04000000>; // 64MiB
		memory-region = <&image_buf0>;
	};
```

In this example, 64MiB of 0x3C000000 to 0x3FFFFFFF is reserved as "image_buf0".
In this "image_buf0", specify "shared-dma-pool" in `compatible` property and specify
the `reusable` property. By specifying these properties, this reserved memory area
will be allocated by the CMA. Also, you need to be careful about address and size
alignment.

The above "image_buf0" is associated with "udmabuf@0" with `memory-region` property.
With this association, "udmabuf@0" reserves physical memory from the CMA area
specifed by "image_buf0".

The `memory-region` property is optional.
When the `memory-region` property is not specified, u-dma-buf allocates the DMA buffer
from the CMA area allocated to the Linux kernel.

## Device file

When u-dma-buf is loaded into the kernel, the following device files are created.
`<device-name>` is a placeholder for the device name described in the previous section.

  * `/dev/<device-name>`
  * `/sys/class/u-dma-buf/<device-name>/phys_addr`
  * `/sys/class/u-dma-buf/<device-name>/size`
  * `/sys/class/u-dma-buf/<device-name>/sync_mode`
  * `/sys/class/u-dma-buf/<device-name>/sync_offset`
  * `/sys/class/u-dma-buf/<device-name>/sync_size`
  * `/sys/class/u-dma-buf/<device-name>/sync_direction`
  * `/sys/class/u-dma-buf/<device-name>/sync_owner`
  * `/sys/class/u-dma-buf/<device-name>/sync_for_cpu`
  * `/sys/class/u-dma-buf/<device-name>/sync_for_device`
  * `/sys/class/u-dma-buf/<device-name>/dma_coherent`


### `/dev/<device-name>`

`/dev/<device-name>` is used when `mmap()`-ed to the user space or accessed via `read()`/`write()`.

```C:u-dma-buf_test.c
    if ((fd  = open("/dev/udmabuf0", O_RDWR)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Do some read/write access to buf */
        close(fd);
    }

```

The device file can be directly read/written by specifying the device as the target of `dd` in the shell.

```console
zynq$ dd if=/dev/urandom of=/dev/udmabuf0 bs=4096 count=1024
1024+0 records in
1024+0 records out
4194304 bytes (4.2 MB) copied, 3.07516 s, 1.4 MB/s
```

```console
zynq$dd if=/dev/udmabuf4 of=random.bin
8192+0 records in
8192+0 records out
4194304 bytes (4.2 MB) copied, 0.173866 s, 24.1 MB/s
```

### `phys_addr`

The physical address of a DMA buffer can be retrieved by reading `/sys/class/u-dma-buf/<device-name>/phys_addr`.

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  phys_addr;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/phys_addr", O_RDONLY)) != -1) {
        read(fd, attr, 1024);
        sscanf(attr, "%x", &phys_addr);
        close(fd);
    }

```

### `size`

The size of a DMA buffer can be retrieved by reading `/sys/class/u-dma-buf/<device-name>/size`.

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned int   buf_size;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/size", O_RDONLY)) != -1) {
        read(fd, attr, 1024);
        sscanf(attr, "%d", &buf_size);
        close(fd);
    }

```

### `sync_mode`

The device file `/sys/class/u-dma-buf/<device-name>/sync_mode` is used to configure
the behavior when u-dma-buf is opened with the `O_SYNC` flag.

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_mode = 2;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_mode", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_mode);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details on `O_SYNC` and cache management will be described in the next section.

### `sync_offset`

The device file `/sys/class/u-dma-buf/<device-name>/sync_offset` is used to specify
the start address of a memory block of which cache is manually managed.

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_offset = 0x00000000;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_offset", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_offset); /* or sprintf(attr, "0x%x", sync_offset); */
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details of manual cache management is described in the next section.

### `sync_size`

The device file `/sys/class/u-dma-buf/<device-name>/sync_size` is used to specify
the size of a memory block of which cache is manually managed.

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_size = 1024;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_size", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_size); /* or sprintf(attr, "0x%x", sync_size); */
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details of manual cache management is described in the next section.

### `sync_direction`

The device file `/sys/class/u-dma-buf/<device-name>/sync_direction` is used to set the
direction of DMA transfer to/from the DMA buffer of which cache is manually managed.

  - 0: sets DMA_BIDIRECTIONAL
  - 1: sets DMA_TO_DEVICE
  - 2: sets DMA_FROM_DEVICE

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_direction = 1;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_direction", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_direction);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details of manual cache management is described in the next section.


### `dma_coherent`

The device file `/sys/class/u-dma-buf/<device-name>/dma_coherent` can read whether
the coherency of DMA buffer and CPU cache can be guaranteed by hardware.
It is able to specify whether or not it is able to guarantee by hardware with the
`dma-coherent` property in the device tree, but this device file is read-only.

If this value is 1, the coherency of DMA buffer and CPU cache can be guaranteed by
hardware. If this value is 0, the coherency of DMA buffer and CPU cache can be not
guaranteed by hardware.

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    int dma_coherent;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/dma_coherent", O_RDONLY)) != -1) {
        read(fd, attr, 1024);
        sscanf(attr, "%x", &dma_coherent);
        close(fd);
    }

```

### `sync_owner`

The device file `/sys/class/u-dma-buf/<device-name>/sync_owner` reports the owner of
the memory block in the manual cache management mode.
If this value is 1, the buffer is owned by the device.
If this value is 0, the buffer is owned by the cpu.

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    int sync_owner;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_owner", O_RDONLY)) != -1) {
        read(fd, attr, 1024);
        sscanf(attr, "%x", &sync_owner);
        close(fd);
    }

```

Details of manual cache management is described in the next section.

### `sync_for_cpu`

In the manual cache management mode, CPU can be the owner of the buffer by writing
non-zero to the device file `/sys/class/u-dma-buf/<device-name>/sync_for_cpu`.
This device file is write only.

If '1' is written to device file, if `sync_direction` is 2(=DMA_FROM_DEVICE) or 0(=DMA_BIDIRECTIONAL),
the write to the device file invalidates a cache specified by `sync_offset` and `sync_size`.

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_for_cpu = 1;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_for_cpu", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_for_cpu);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

The value written to this device file can include sync_offset, sync_size, and sync_direction. 

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_offset    = 0;
    unsigned long  sync_size      = 0x10000;
    unsigned int   sync_direction = 0;
    unsigned long  sync_for_cpu   = 1;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_for_cpu", O_WRONLY)) != -1) {
        sprintf(attr, "0x%08X%08X", (sync_offset & 0xFFFFFFFF), (sync_size & 0xFFFFFFF0) | (sync_direction << 2) | sync_for_cpu);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

The sync_offset/sync_size/sync_direction specified by ```sync_for_cpu``` is temporary and does not affect the ```sync_offset``` or ```sync_size``` or ```sync_direction``` device files.

Details of manual cache management is described in the next section.

### `sync_for_device`

In the manual cache management mode, DEVICE can be the owner of the buffer by
writing non-zero to the device file `/sys/class/u-dma-buf/<device-name>/sync_for_device`.
This device file is write only.

If '1' is written to device file, if `sync_direction` is 1(=DMA_TO_DEVICE) or 0(=DMA_BIDIRECTIONAL),
the write to the device file flushes a cache specified by `sync_offset` and `sync_size` (i.e. the
cached data, if any, will be updated with data on DDR memory).

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_for_device = 1;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_for_device", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_for_device);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

The value written to this device file can include sync_offset, sync_size, and sync_direction. 

```C:u-dma-buf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_offset     = 0;
    unsigned long  sync_size       = 0x10000;
    unsigned int   sync_direction  = 0;
    unsigned long  sync_for_device = 1;
    if ((fd  = open("/sys/class/u-dma-buf/udmabuf0/sync_for_device", O_WRONLY)) != -1) {
        sprintf(attr, "0x%08X%08X", (sync_offset & 0xFFFFFFFF), (sync_size & 0xFFFFFFF0) | (sync_direction << 2) | sync_for_device);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

The sync_offset/sync_size/sync_direction specified by ```sync_for_device``` is temporary and does not affect the ```sync_offset``` or ```sync_size``` or ```sync_direction``` device files.

Details of manual cache management is described in the next section.

## Configuration via the `/dev/u-dma-buf-mgr`

Since u-dma-buf v2.1, `/dev/u-dma-buf-mgr` device driver has been added. u-dma-buf can be
created/deleted by writing the command to `/dev/u-dma-buf-mgr` as a string.

### Create u-dma-buf

u-dma-buf can be created by writing the string "create <device-name> <size>" to `/dev/u-dma-buf-mgr` as follows:
For `<device-name>`, specify the device name of the u-dma-buf to be generated.
For `<size>`, specify the size of the buffer to be allocated.

```console
zynq$ sudo sh -c "echo 'create udmabuf8 0x10000' > /dev/u-dma-buf-mgr"
[   58.790695] u-dma-buf-mgr : create udmabuf8 65536
[   58.798637] u-dma-buf udmabuf8: driver version = 2.1.3
[   58.804114] u-dma-buf udmabuf8: major number   = 245
[   58.809000] u-dma-buf udmabuf8: minor number   = 0
[   58.815628] u-dma-buf udmabuf8: phys address   = 0x1f050000
[   58.822041] u-dma-buf udmabuf8: buffer size    = 65536
[   58.827098] u-dma-buf udmabuf8: dma device     = u-dma-buf.0.auto
[   58.834918] u-dma-buf udmabuf8: dma coherent   = 0
[   58.839632] u-dma-buf u-dma-buf.0.auto: driver installed.
```

### Delete u-dma-buf

u-dma-buf can be deleted by writing the string "delete <device-name>" to `/dev/u-dma-buf-mgr` as follows:
For `<device-name>`, specify `<device-name>` specified with the create command.

```console
zynq$ sudo sh -c "echo 'delete udmabuf8' > /dev/u-dma-buf-mgr"
[  179.089702] u-dma-buf-mgr : delete udmabuf8
[  179.094212] u-dma-buf u-dma-buf.0.auto: driver removed.
```

# Coherency of data on DMA buffer and CPU cache

CPU usually accesses to a DMA buffer on the main memory using cache, and a hardware
accelerator logic accesses to data stored in the DMA buffer on the main memory.
In this situation, coherency between data stored on CPU cache and them on the main
memory should be considered carefully.

## When the coherency is maintained by hardware

When hardware assures the coherency, CPU cache can be turned on without additional
treatment. For example, ZYNQ provides ACP (Accelerator Coherency Port), and the
coherency is maintained by hardware as long as the accelerator accesses to the main
memory via this port.

In this case, accesses from CPU to the main memory can be fast by using CPU cache
as usual. To enable CPU cache on the DMA buffer allocated by u-dma-buf, open u-dma-buf
without specifying the `O_SYNC` flag.

```C:u-dma-buf_test.c
    /* To enable CPU cache on the DMA buffer, */
    /* open u-dma-buf without specifying the `O_SYNC` flag. */
    if ((fd  = open("/dev/udmabuf0", O_RDWR)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Read/write access to the buffer */
        close(fd);
    }

```

The manual management of cache, described in the following section, will not be
necessary when hardware maintains the coherency.

If the `dma-coherent` property is specified in the device tree, specify that
coherency can be guaranteed with hardware. In this case, the cache control described
in "2. Manual cache management with the CPU cache still being enabled" described
later is not performed.

## When hardware does not maintain the coherency

To maintain coherency of data between CPU and the main memory, another coherency
mechanism is necessary. u-dma-buf supports two different ways of coherency maintenance;
one is to disable CPU cache, and the other is to involve manual cache flush/invalidation
with CPU cache being enabled.

### 1. Disabling CPU cache

To disable CPU cache of allocated DMA buffer, specify the `O_SYNC` flag when opening u-dma-buf.

```C:u-dma-buf_test.c
    /* To disable CPU cache on the DMA buffer, */
    /* open u-dma-buf with the `O_SYNC` flag. */
    if ((fd  = open("/dev/udmabuf0", O_RDWR | O_SYNC)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Read/write access to the buffer */
        close(fd);
    }

```


As listed below, `sync_mode` can be used to configure the cache behavior when the
`O_SYNC` flag is present in `open()`:

  * sync_mode=0: CPU cache is enabled regardless of the `O_SYNC` flag presense.
  * sync_mode=1: If `O_SYNC` is specified, CPU cache is disabled.
    If `O_SYNC` is not specified, CPU cache is enabled.
  * sync_mode=2: If `O_SYNC` is specified, CPU cache is disabled but CPU uses
    write-combine when writing data to DMA buffer improves performance by combining
    multiple write accesses. If `O_SYNC` is not specified, CPU cache is enabled.
  * sync_mode=3: If `O_SYNC` is specified, DMA coherency mode is used.
    If `O_SYNC` is not specified, CPU cache is enabled.
  * sync_mode=4: CPU cache is enabled regardless of the `O_SYNC` flag presense. 
  * sync_mode=5: CPU cache is disabled regardless of the `O_SYNC` flag presense. 
  * sync_mode=6: CPU uses write-combine to write data to DMA buffer regardless of `O_SYNC` presence.
  * sync_mode=7: DMA coherency mode is used regardless of `O_SYNC` presence.

As a practical example, the execution times of a sample program listed below were
measured under several test conditions as presented in the table.

```C:u-dma-buf_test.c
int check_buf(unsigned char* buf, unsigned int size)
{
    int m = 256;
    int n = 10;
    int i, k;
    int error_count = 0;
    while(--n > 0) {
      for(i = 0; i < size; i = i + m) {
        m = (i+256 < size) ? 256 : (size-i);
        for(k = 0; k < m; k++) {
          buf[i+k] = (k & 0xFF);
        }
        for(k = 0; k < m; k++) {
          if (buf[i+k] != (k & 0xFF)) {
            error_count++;
          }
        }
      }
    }
    return error_count;
}
int clear_buf(unsigned char* buf, unsigned int size)
{
    int n = 100;
    int error_count = 0;
    while(--n > 0) {
      memset((void*)buf, 0, size);
    }
    return error_count;
}

```


Table-1　The execution time of the sample program `checkbuf`

<table border="2">
  <tr>
    <td align="center" rowspan="2">sync_mode</td>
    <td align="center" rowspan="2">O_SYNC</td>
    <td align="center" colspan="3">DMA buffer size</td>
  </tr>
  <tr>
    <td align="center">1MByte</td>
    <td align="center">5MByte</td>
    <td align="center">10MByte</td>
  </tr>
  <tr>
    <td rowspan="2">0</td>
    <td>Not specified</td>
    <td align="right">0.437[sec]</td>
    <td align="right">2.171[sec]</td>
    <td align="right">4.340[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.437[sec]</td>
    <td align="right">2.171[sec]</td>
    <td align="right">4.340[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">1</td>
    <td>Not specified</td>
    <td align="right">0.434[sec]</td>
    <td align="right">2.179[sec]</td>
    <td align="right">4.337[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">2.283[sec]</td>
    <td align="right">11.414[sec]</td>
    <td align="right">22.830[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">2</td>
    <td>Not specified</td>
    <td align="right">0.434[sec]</td>
    <td align="right">2.169[sec]</td>
    <td align="right">4.337[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">1.616[sec]</td>
    <td align="right">8.262[sec]</td>
    <td align="right">16.562[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">3</td>
    <td>Not specified</td>
    <td align="right">0.434[sec]</td>
    <td align="right">2.169[sec]</td>
    <td align="right">4.337[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">1.600[sec]</td>
    <td align="right">8.391[sec]</td>
    <td align="right">16.587[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">4</td>
    <td>Not specified</td>
    <td align="right">0.437[sec]</td>
    <td align="right">2.171[sec]</td>
    <td align="right">4.337[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.437[sec]</td>
    <td align="right">2.171[sec]</td>
    <td align="right">4.337[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">5</td>
    <td>Not specified</td>
    <td align="right">2.283[sec]</td>
    <td align="right">11.414[sec]</td>
    <td align="right">22.809[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">2.283[sec]</td>
    <td align="right">11.414[sec]</td>
    <td align="right">22.840[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">6</td>
    <td>Not specified</td>
    <td align="right">1.655[sec]</td>
    <td align="right">8.391[sec]</td>
    <td align="right">16.587[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">1.655[sec]</td>
    <td align="right">8.391[sec]</td>
    <td align="right">16.587[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">7</td>
    <td>Not specified</td>
    <td align="right">1.655[sec]</td>
    <td align="right">8.391[sec]</td>
    <td align="right">16.587[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">1.655[sec]</td>
    <td align="right">8.391[sec]</td>
    <td align="right">16.587[sec]</td>
  </tr>
</table>

Table-2　The execution time of the sample program `clearbuf`

<table border="2">
  <tr>
    <td align="center" rowspan="2">sync_mode</td>
    <td align="center" rowspan="2">O_SYNC</td>
    <td align="center" colspan="3">DMA buffer size</td>
  </tr>
  <tr>
    <td align="center">1MByte</td>
    <td align="center">5MByte</td>
    <td align="center">10MByte</td>
  </tr>
  <tr>
    <td rowspan="2">0</td>
    <td>Not specified</td>
    <td align="right">0.067[sec]</td>
    <td align="right">0.359[sec]</td>
    <td align="right">0.713[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.067[sec]</td>
    <td align="right">0.362[sec]</td>
    <td align="right">0.716[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">1</td>
    <td>Not specified</td>
    <td align="right">0.067[sec]</td>
    <td align="right">0.362[sec]</td>
    <td align="right">0.718[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.912[sec]</td>
    <td align="right">4.563[sec]</td>
    <td align="right">9.126[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">2</td>
    <td>Not specified</td>
    <td align="right">0.068[sec]</td>
    <td align="right">0.360[sec]</td>
    <td align="right">0.721[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.063[sec]</td>
    <td align="right">0.310[sec]</td>
    <td align="right">0.620[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">3</td>
    <td>Not specified</td>
    <td align="right">0.068[sec]</td>
    <td align="right">0.361[sec]</td>
    <td align="right">0.715[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.062[sec]</td>
    <td align="right">0.310[sec]</td>
    <td align="right">0.620[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">4</td>
    <td>Not specified</td>
    <td align="right">0.068[sec]</td>
    <td align="right">0.360[sec]</td>
    <td align="right">0.718[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.067[sec]</td>
    <td align="right">0.360[sec]</td>
    <td align="right">0.710[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">5</td>
    <td>Not specified</td>
    <td align="right">0.913[sec]</td>
    <td align="right">4.562[sec]</td>
    <td align="right">9.126[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.913[sec]</td>
    <td align="right">4.562[sec]</td>
    <td align="right">9.126[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">6</td>
    <td>Not specified</td>
    <td align="right">0.062[sec]</td>
    <td align="right">0.310[sec]</td>
    <td align="right">0.618[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.062[sec]</td>
    <td align="right">0.310[sec]</td>
    <td align="right">0.619[sec]</td>
  </tr>
  <tr>
    <td rowspan="2">7</td>
    <td>Not specified</td>
    <td align="right">0.062[sec]</td>
    <td align="right">0.310[sec]</td>
    <td align="right">0.620[sec]</td>
  </tr>
  <tr>
    <td>Specified</td>
    <td align="right">0.062[sec]</td>
    <td align="right">0.310[sec]</td>
    <td align="right">0.621[sec]</td>
  </tr>
</table>

**Note: on using `O_SYNC` flag on ARM64**

For v2.1.1 or earier, udmabuf used ```pgprot_writecombine()``` on ARM64 and sync_mode=1(noncached). The reason is that a bus error occurred in memset() in udmabuf_test.c when using ```pgprot_noncached()```.

However, as reported in https://github.com/ikwzm/udmabuf/pull/28, when using ```pgprot_writecombine()``` on ARM64, it was found that there was a problem with cache coherency.

Therefore, since v2.1.2, when sync_mode = 1, it was changed to use ```pgprot_noncached()```. This is because cache coherency issues are very difficult to understand and difficult to debug. Rather than worrying about the cache coherency problem, we decided that it was easier to understand when the bus error occurred.

This change requires alignment attention when using O_SYNC cache control on ARM64. You probably won't be able to use memset().

If a problem occurs, either cache coherency is maintained by hardware, or use a method described bellow that manually cache management with CPU cache still being enabled.

### 2. Manual cache management with the CPU cache still being enabled

As explained above, by opening u-dma-buf without specifying the `O_SYNC` flag, CPU cache can be left turned on.

```C:u-dma-buf_test.c
    /* To enable CPU cache on the DMA buffer, */
    /* open u-dma-buf without specifying the `O_SYNC` flag. */
    if ((fd  = open("/dev/udmabuf0", O_RDWR)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Read/write access to the buffer */
        close(fd);
    }

```

To manualy manage cache coherency, users need to follow the 

  1. Specify a memory area shared between CPU and accelerator via `sync_offset`
     and `sync_size` device files. `sync_offset` accepts an offset from the start
     address of the allocated buffer in units of bytes.
     The size of the shared memory area should be set to `sync_size` in units of bytes.
  2. Data transfer direction should be set to `sync_direction`. If the accelerator
     performs only read accesses to the memory area, `sync_direction` should be set
     to `1(=DMA_TO_DEVICE)`, and to `2(=DMA_FROM_DEVICE)` if only write accesses. 
  3. If the accelerator reads and writes data from/to the memory area,
     `sync_direction` should be set to `0(=DMA_BIDIRECTIONAL)`.

Following the above configuration, `sync_for_cpu` and/or `sync_for_device` should
be used to set the owner of the buffer specified by the above-mentioned offset and
the size. 

When CPU accesses to the buffer, '1' should be written to `sync_for_cpu` to set
CPU as the owner. Upon the write to `sync_for_cpu`, CPU cache is invalidated if
`sync_direction` is `2(=DMA_FROM_DEVICE)` or `0(=DMA_BIDIRECTIONAL)`.
Once CPU is becomes the owner of the buffer, the accelerator cannot access the buffer. 

On the other hand, when the accelerator needs to access the buffer, '1' should be
written to `sync_for_device` to change owership of the buffer to the accelerator.
Upon the write to `sync_for_device`, the CPU cache of the specified memory area is
flushed using data on the main memory.

However, if the `dma-coherent` property is specified in the device tree, CPU cache
is not invalidated and flushed.


# Example using u-dma-buf with Python

The programming language "Python" provides an extension called "NumPy".
This section explains how to do the same operation as "ndarry" by mapping the DMA
buffer allocated in the kernel with `memmap` of "NumPy" with u-dma-buf.


## Udmabuf Class


```python:udmabuf.py

import numpy as np

class Udmabuf:
    """A simple u-dma-buf class"""
    def __init__(self, name):
        self.name           = name
        self.device_name    = '/dev/%s'                 % self.name
        self.class_path     = '/sys/class/u-dma-buf/%s' % self.name
        self.phys_addr      = self.get_value('phys_addr', 16)
        self.buf_size       = self.get_value('size')
        self.sync_offset    = None
        self.sync_size      = None
        self.sync_direction = None

    def memmap(self, dtype, shape):
        self.item_size = np.dtype(dtype).itemsize
        self.array     = np.memmap(self.device_name, dtype=dtype, mode='r+', shape=shape)
        return self.array

    def get_value(self, name, radix=10):
        value = None
        for line in open(self.class_path + '/' + name):
            value = int(line, radix)
            break
        return value
    def set_value(self, name, value):
        f = open(self.class_path + '/' + name, 'w')
        f.write(str(value))
        f.close

    def set_sync_area(self, direction=None, offset=None, size=None):
        if offset is None:
            self.sync_offset    = self.get_value('sync_offset')
        else:
            self.set_value('sync_offset', offset)
            self.sync_offset    = offset
        if size   is None:
            self.sync_size      = self.get_value('sync_size')
        else:
            self.set_value('sync_size', size)
            self.sync_size      = size
        if direction is None:
            self.sync_direction = self.get_value('sync_direction')
        else:
            self.set_value('sync_direction', direction)
            self.sync_direction = direction

    def set_sync_to_device(self, offset=None, size=None):
        self.set_sync_area(1, offset, size)

    def set_sync_to_cpu(self, offset=None, size=None):
        self.set_sync_area(2, offset, size)

    def set_sync_to_bidirectional(self, offset=None, size=None):
        self.set_sync_area(3, offset, size)

    def sync_for_cpu(self):
        self.set_value('sync_for_cpu', 1)

    def sync_for_device(self):
        self.set_value('sync_for_device', 1)

```

## udmabuf_test.py

```python:udmabuf_test.py

from udmabuf import Udmabuf
import numpy as np
import time
def test_1(a):
    for i in range (0,9):
        a *= 0
        a += 0x31
if __name__ == '__main__':
    udmabuf      = Udmabuf('udmabuf0')
    test_dtype   = np.uint8
    test_size    = udmabuf.buf_size/(np.dtype(test_dtype).itemsize)
    udmabuf.memmap(dtype=test_dtype, shape=(test_size))
    comparison   = np.zeros(test_size, dtype=test_dtype)
    print ("test_size  : %d" % test_size)
    start        = time.time()
    test_1(udmabuf.mem_map)
    elapsed_time = time.time() - start
    print ("udmabuf-0   : elapsed_time:{0}".format(elapsed_time)) + "[sec]"
    start        = time.time()
    test_1(comparison)
    elapsed_time = time.time() - start
    print ("comparison : elapsed_time:{0}".format(elapsed_time)) + "[sec]"
    if np.array_equal(udmabuf.mem_map, comparison):
        print ("udmabuf-0 == comparison : OK")
    else:
        print ("udmabuf-0 != comparison : NG")

```

## Execution result

Install u-dma-buf. In this example, 8MiB DMA buffer is reserved as "udmabuf0".

```console
zynq# insmod u-dma-buf.ko udmabuf0=8388608
[34654.622746] u-dma-buf udmabuf0: driver installed
[34654.627153] u-dma-buf udmabuf0: major number   = 237
[34654.631889] u-dma-buf udmabuf0: minor number   = 0
[34654.636685] u-dma-buf udmabuf0: phys address   = 0x1f300000
[34654.642002] u-dma-buf udmabuf0: buffer size    = 8388608
[34654.642002] u-dma-buf udmabuf0: dma-coherent   = 0

```

Executing the script in the previous section gives the following results.

```console
zynq# python udmabuf_test.py
test_size  : 8388608
udmabuf0   : elapsed_time:1.53304982185[sec]
comparison : elapsed_time:1.536673069[sec]
udmabuf0 == comparison : OK
```

The execution time for "udmabuf0"(buffer area secured in the kernel) and the same
operation with ndarray (comparison) were almost the same.
That is, it seems that "udmabuf0" is also effective CPU cache.


I confirmed the contents of "udmabuf0" after running this script.

```console
zynq# dd if=/dev/udmabuf0 of=udmabuf0.bin bs=8388608
1+0 records in
1+0 records out
8388608 bytes (8.4 MB) copied, 0.151531 s, 55.4 MB/s
shell# 
shell# od -t x1 udmabuf0.bin
0000000 31 31 31 31 31 31 31 31 31 31 31 31 31 31 31 31
*
40000000
```

After executing the script, it was confirmed that the result of the execution remains
in the buffer. Just to be sure, let's check that NumPy can read it.

```console
zynq# python
Python 2.7.9 (default, Aug 13 2016, 17:56:53)
[GCC 4.9.2] on linux2
Type "help", "copyright", "credits" or "license" for more information.
>>> import numpy as np
>>> a = np.memmap('/dev/udmabuf0', dtype=np.uint8, mode='r+', shape=(8388608))
>>> a
memmap([49, 49, 49, ..., 49, 49, 49], dtype=uint8)
>>> a.itemsize
1
>>> a.size
8388608
>>>
```

