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

* OS : Linux Kernel Version 3.6 - 3.8, 3.18, 4.4, 4.8, 4.12, 4.14, 4.19, 5.0 - 5.10, 6.1 (the author tested on 3.18, 4.4, 4.8, 4.12, 4.14, 4.19, 5.4, 5.10, 6.1).
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

### Makefile

This repository contains a [Makefie](./Makefile).
Makefile has the following Parameters:

| Parameter Name                       | Description                  | Default Value                        |
|--------------------------------------|------------------------------|--------------------------------------|
| ARCH                                 | Architecture Name            | `$(shell uname -m \| sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)` |
| KERNEL_SRC                           | Kernel Source Directory      | `/lib/modules/$(shell uname -r)/build` |
| CONFIG_U_DMA_BUF_DEBUG               | Enable debug report          | y |
| CONFIG_U_DMA_BUF_QUIRK_MMAP          | Enable quirk-mmap            | y |
| CONFIG_U_DMA_BUF_IN_KERNEL_FUNCTIONS | Enable in-kernel functions   | y |
| CONFIG_U_DMA_BUF_IOCTL               | Enable ioctl                 | y |
| CONFIG_U_DMA_BUF_EXPORT              | Enable PRIME DMA-BUFS export | y |

### Cross Compile

If you have a cross-compilation environment for target system, you can compile with:

```console
shell$ make ARCH=arm KERNEL_SRC=/home/fpga/src/linux-5.10.120-zynqmp-fpga-generic all
```
The ARCH variable specifies the architecture name.    
The KERNEL_SRC variable specifies the Linux Kernel source code path.    

### Self Compile

If your target system is capable of self-compiling the Linux Kernel module, you can compile it with:

```console
shell$ make all
```
You need the kernel source code in ```/lib/modules/$(shell uname -r)/build``` to compile.

### Build in Linux Source Tree

It can also be compiled into the Linux Kernel Source Tree.

#### Make directory in Linux Kernel Source Tree.

```console
shell$ mkdir <linux-source-tree>/drivers/staging/u-dma-buf
```

#### Copy files to Linux Kernel Source Tree.

```console
shell$ cp Kconfig Makefile u-dma-buf.c <linux-source-tree>/drivers/staging/u-dma-buf
```

#### Add u-dma-buf to Kconfig

```console
shell$ diff <linux-source-tree>/drivers/staging/Kconfig
  :
+source "drivers/staging/u-dma-buf/Kconfig"
+
```

#### Add u-dma-buf to Makefile

```Makefile
shell$ diff <linux-source-tree>/drivers/staging/Makefile
  :
+obj-$(CONFIG_U_DMA_BUF) += u-dma-buf/
```

#### Set CONFIG_U_DMA_BUF

For make menuconfig, set the following:

```console
Device Drivers --->
  Staging drivers --->
    <M> u-dma-buf(User space mappable DMA Buffer) --->
```

If you write it directly in defconfig:

```console
shell$ diff <linux-source-tree>/arch/arm64/configs/xilinx_zynqmp_defconfig
   :
+CONFIG_U_DMA_BUF=m
```

## Install

### Installation with the insmod

Load the u-dma-buf kernel driver using `insmod`. The size of a DMA buffer should be
provided as an argument as follows.
The device driver is created, and allocates a DMA buffer with the specified size.
The maximum number of DMA buffers that can be allocated using `insmod` is 8 (udmabuf0/1/2/3/4/5/6/7).

```console
zynq$ insmod u-dma-buf.ko udmabuf0=1048576
u-dma-buf udmabuf0: driver version = 5.0.0
u-dma-buf udmabuf0: major number   = 248
u-dma-buf udmabuf0: minor number   = 0
u-dma-buf udmabuf0: phys address   = 0x1e900000
u-dma-buf udmabuf0: buffer size    = 1048576
u-dma-buf u-dma-buf.0: driver installed.
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
u-dma-buf u-dma-buf.0: driver removed.
```

### Installation with the Debian package

For details, refer to the following URL.

*  https://github.com/ikwzm/u-dma-buf-kmod-dpkg


## Configuration via the module parameters

The u-dma-buf kernel module has the following module parameters:

| Parameter Name  | Type  | Default | Description                         |
|:----------------|:------|---------|:------------------------------------|
| udmabuf0        | ulong |    0    | u-dma-buf0 buffer size              |
| udmabuf1        | ulong |    0    | u-dma-buf1 buffer size              |
| udmabuf2        | ulong |    0    | u-dma-buf2 buffer size              |
| udmabuf3        | ulong |    0    | u-dma-buf3 buffer size              |
| udmabuf4        | ulong |    0    | u-dma-buf4 buffer size              |
| udmabuf5        | ulong |    0    | u-dma-buf5 buffer size              |
| udmabuf6        | ulong |    0    | u-dma-buf6 buffer size              |
| udmabuf7        | ulong |    0    | u-dma-buf7 buffer size              |
| info_enable     | int   |    1    | install/uninstall infomation enable |
| dma_mask_bit    | int   |   32    | dma mask bit size                   |
| bind            | charp |   ""    | bind device name                    |
| quirk_mmap_mode | int   | 2 or 3  | quirk mmap mode(1:off,2:on,3:auto,4:page) |

### `udmabuf[0-7]`

This parameter specifies the capacity of the u-dma-buf to be created in bytes.
The number of u-dma-buf that can be created with this parameter is 8.
The device name will be udmabuf[0-7].
If this parameter is 0, the u-dma-buf is not created.

### `info_enable`

This parameter specifies whether or not detailed information about when the u-dma-buf was created should be displayed.

### `dma_mask_bit`

** Note: The value of dma-mask is system dependent.
Make sure you are familiar with the meaning of dma-mask before setting. **

### `bind`

This parameter specifies the parent device of the u-dma-buf.
If this parameter is an empty string (default value), u-dma-buf is created as a new platform device.
If a parent device name is specified for this parameter, u-dma-buf is created as its child device.

The format of the string specified in this parameter is `"<bus>/<device-name>"`.

The `<bus>` is the bus name, currently pci is supported.
The bus name can be omitted.
If omitted, it will be the platform bus.

The `<device-name>` specifies the name of the device under bus management.

For example, to designate "0000:00:15.0" under the pci bus as the parent device, do the following

```console
shell$ sudo insmod u-dma-buf.ko udmabuf0=0x10000 info_enable=3 bind="pci/0000:00:15.0" 
[13422.022482] u-dma-buf udmabuf0: driver version = 5.0.0
[13422.022483] u-dma-buf udmabuf0: major number   = 238
[13422.022483] u-dma-buf udmabuf0: minor number   = 0
[13422.022484] u-dma-buf udmabuf0: phys address   = 0x0000000070950000
[13422.022485] u-dma-buf udmabuf0: buffer size    = 65536
[13422.022485] u-dma-buf udmabuf0: dma device     = 0000:00:15.0
[13422.022486] u-dma-buf udmabuf0: dma bus        = pci
[13422.022486] u-dma-buf udmabuf0: dma coherent   = 1
[13422.022487] u-dma-buf udmabuf0: dma mask       = 0x00000000ffffffff
[13422.022487] u-dma-buf udmabuf0: iommu domain   = NONE
[13422.022487] u-dma-buf udmabuf0: mmap mode      = 3
[13422.022487] u-dma-buf udmabuf0: mmap           = dma_mmap_coherent
[13422.022488] u-dma-buf: udmabuf0 installed.
```

### `quirk_mmap_mode`

This parameter specifies the default value of quirk-mmap-mode. 
quirk-mmap is described in detail below.   
If this parameter is 1, quirk-mmap is prohibited.  
If this parameter is 2, quirk-mmap is used.   
If this parameter is 3, quirk-mmap is not used if the device has a dma-cohrent of true, and quirk-mmap is used only if dma-coherent is false.   
If this parameter is 4, quirk-mmap is used in quirk-mmap-page mode.   

If the architecture is ARM or ARM64, this parameter defaults to 2.   
If the architecture is other than the above, this parameter defaults to 3.   

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
u-dma-buf udmabuf0: driver version = 5.0.0
u-dma-buf udmabuf0: major number   = 248
u-dma-buf udmabuf0: minor number   = 0
u-dma-buf udmabuf0: phys address   = 0x1e900000
u-dma-buf udmabuf0: buffer size    = 1048576
u-dma-buf amba:udmabuf@0x00: driver installed.
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
  *  `quirk-mmap-off`
  *  `quirk-mmap-on`
  *  `quirk-mmap-auto`
  *  `quirk-mmap-page`
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

### `quirk-mmap-off`

If the `quirk-mmap-off` property is specified, quirk-mmap. is not used.

### `quirk-mmap-on`

If the `quirk-mmap-on` property is specified, quirk-mmap. is used.

### `quirk-mmap-auto`

If the `quirk-mmap-auto` property is specified, quirk-mmap is not used if the device has a dma-cohrent of true, and quirk-mmap is used only if dma-coherent is false.

### `quirk-mmap-page`

If the `quirk-mmap-page` property is specified, quirk-mmap. is used in quirk-mmap-page mode.   
In quirk-mmap-page mode, there is no error when u-dma-buf is subject to O_DIRECT.   
This mode is currently under development. Please use with caution.

### `memory-region`

Linux can specify the reserved memory area in the device tree. The Linux kernel
excludes normal memory allocation from the physical memory space specified by
`reserved-memory` property.
In order to access this reserved memory area, it is necessary to use a
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
specified by "image_buf0".

The `memory-region` property is optional.
When the `memory-region` property is not specified, u-dma-buf allocates the DMA buffer
from the CMA area allocated to the Linux kernel.

## Configuration via the `/dev/u-dma-buf-mgr`

Since u-dma-buf v4.0, u-dma-buf devices can be create or delete using u-dma-buf-mgr.
See https://github.com/ikwzm/u-dma-buf-mgr for more information.

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

## ioctl

Starting with u-dma-buf v4.7.0, devices can be controlled by issuing ioctl to the device file.
The ioctl can do the following

 * `U_DMA_BUF_IOCTL_GET_DRV_INFO`
 * `U_DMA_BUF_IOCTL_GET_SIZE`
 * `U_DMA_BUF_IOCTL_GET_DMA_ADDR`
 * `U_DMA_BUF_IOCTL_GET_SYNC_OWNER`
 * `U_DMA_BUF_IOCTL_SET_SYNC_FOR_CPU`
 * `U_DMA_BUF_IOCTL_SET_SYNC_FOR_DEVICE`
 * `U_DMA_BUF_IOCTL_GET_DEV_INFO`
 * `U_DMA_BUF_IOCTL_GET_SYNC`
 * `U_DMA_BUF_IOCTL_SET_SYNC`
 * `U_DMA_BUF_IOCTL_EXPORT`

### u-dma-buf-ioctl.h

The following header file is required to use ioctl.

```C:u-dma-buf-ioctl.h
#ifndef  U_DMA_BUF_IOCTL_H
#define  U_DMA_BUF_IOCTL_H
#include <linux/ioctl.h>

#define DEFINE_U_DMA_BUF_IOCTL_FLAGS(name,type,lo,hi)                     \
static const  int      U_DMA_BUF_IOCTL_FLAGS_ ## name ## _SHIFT = (lo);   \
static const  uint64_t U_DMA_BUF_IOCTL_FLAGS_ ## name ## _MASK  = (((uint64_t)1UL << ((hi)-(lo)+1))-1); \
static inline void SET_U_DMA_BUF_IOCTL_FLAGS_ ## name(type *p, int value) \
{                                                                         \
    const int      shift = U_DMA_BUF_IOCTL_FLAGS_ ## name ## _SHIFT;      \
    const uint64_t mask  = U_DMA_BUF_IOCTL_FLAGS_ ## name ## _MASK;       \
    p->flags &= ~(mask << shift);                                         \
    p->flags |= ((value & mask) << shift);                                \
}                                                                         \
static inline int  GET_U_DMA_BUF_IOCTL_FLAGS_ ## name(type *p)            \
{                                                                         \
    const int      shift = U_DMA_BUF_IOCTL_FLAGS_ ## name ## _SHIFT;      \
    const uint64_t mask  = U_DMA_BUF_IOCTL_FLAGS_ ## name ## _MASK;       \
    return (int)((p->flags >> shift) & mask);                             \
}

typedef struct {
    uint64_t flags;
    char     version[16];
} u_dma_buf_ioctl_drv_info;

DEFINE_U_DMA_BUF_IOCTL_FLAGS(IOCTL_VERSION      , u_dma_buf_ioctl_drv_info ,  0,  7)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(IN_KERNEL_FUNCTIONS, u_dma_buf_ioctl_drv_info ,  8,  8)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(USE_OF_DMA_CONFIG  , u_dma_buf_ioctl_drv_info , 12, 12)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(USE_OF_RESERVED_MEM, u_dma_buf_ioctl_drv_info , 13, 13)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(USE_QUIRK_MMAP     , u_dma_buf_ioctl_drv_info , 16, 16)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(USE_QUIRK_MMAP_PAGE, u_dma_buf_ioctl_drv_info , 17, 17)

typedef struct {
    uint64_t flags;
    uint64_t size;
    uint64_t addr;
} u_dma_buf_ioctl_dev_info;

DEFINE_U_DMA_BUF_IOCTL_FLAGS(DMA_MASK    , u_dma_buf_ioctl_dev_info ,  0,  7)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(DMA_COHERENT, u_dma_buf_ioctl_dev_info ,  9,  9)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(MMAP_MODE   , u_dma_buf_ioctl_dev_info , 10, 12)

typedef struct {
    uint64_t flags;
    uint64_t size;
    uint64_t offset;
} u_dma_buf_ioctl_sync_args;

DEFINE_U_DMA_BUF_IOCTL_FLAGS(SYNC_CMD    , u_dma_buf_ioctl_sync_args,  0,  1)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(SYNC_DIR    , u_dma_buf_ioctl_sync_args,  2,  3)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(SYNC_MODE   , u_dma_buf_ioctl_sync_args,  8, 15)
DEFINE_U_DMA_BUF_IOCTL_FLAGS(SYNC_OWNER  , u_dma_buf_ioctl_sync_args, 16, 16)

enum {
    U_DMA_BUF_IOCTL_FLAGS_SYNC_CMD_FOR_CPU    = 1,
    U_DMA_BUF_IOCTL_FLAGS_SYNC_CMD_FOR_DEVICE = 3
};

typedef struct {
    uint64_t flags;
    uint64_t size;
    uint64_t offset;
    uint64_t addr;
    int      fd;
} u_dma_buf_ioctl_export_args;

DEFINE_U_DMA_BUF_IOCTL_FLAGS(EXPORT_FD_FLAGS, u_dma_buf_ioctl_export_args,  0, 31)

#define U_DMA_BUF_IOCTL_MAGIC               'U'
#define U_DMA_BUF_IOCTL_GET_DRV_INFO        _IOR (U_DMA_BUF_IOCTL_MAGIC, 1, u_dma_buf_ioctl_drv_info)
#define U_DMA_BUF_IOCTL_GET_SIZE            _IOR (U_DMA_BUF_IOCTL_MAGIC, 2, uint64_t)
#define U_DMA_BUF_IOCTL_GET_DMA_ADDR        _IOR (U_DMA_BUF_IOCTL_MAGIC, 3, uint64_t)
#define U_DMA_BUF_IOCTL_GET_SYNC_OWNER      _IOR (U_DMA_BUF_IOCTL_MAGIC, 4, uint32_t)
#define U_DMA_BUF_IOCTL_SET_SYNC_FOR_CPU    _IOW (U_DMA_BUF_IOCTL_MAGIC, 5, uint64_t)
#define U_DMA_BUF_IOCTL_SET_SYNC_FOR_DEVICE _IOW (U_DMA_BUF_IOCTL_MAGIC, 6, uint64_t)
#define U_DMA_BUF_IOCTL_GET_DEV_INFO        _IOR (U_DMA_BUF_IOCTL_MAGIC, 7, u_dma_buf_ioctl_dev_info)
#define U_DMA_BUF_IOCTL_GET_SYNC            _IOR (U_DMA_BUF_IOCTL_MAGIC, 8, u_dma_buf_ioctl_sync_args)
#define U_DMA_BUF_IOCTL_SET_SYNC            _IOW (U_DMA_BUF_IOCTL_MAGIC, 9, u_dma_buf_ioctl_sync_args)
#define U_DMA_BUF_IOCTL_EXPORT              _IOWR(U_DMA_BUF_IOCTL_MAGIC,10, u_dma_buf_ioctl_export_args)
#endif /* #ifndef U_DMA_BUF_IOCTL_H */
```

### Example of required header files

```C:u-dma-buf-ioctl-test.c
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "u-dma-buf-ioctl.h"
```

### `U_DMA_BUF_IOCTL_GET_DRV_INFO`

This ioctl is for get driver information.
The driver information obtained by this ioctl includes the driver support and version number.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        u_dma_buf_ioctl_drv_info drv_info = {0};
        int status = ioctl(fd, U_DMA_BUF_IOCTL_GET_DRV_INFO, &drv_info);
        int ioctl_version       = GET_U_DMA_BUF_IOCTL_FLAGS_IOCTL_VERSION(&drv_info);
        int in_kernel_function  = GET_U_DMA_BUF_IOCTL_FLAGS_IN_KERNEL_FUNCTIONS(&drv_info);
        int use_of_dma_config   = GET_U_DMA_BUF_IOCTL_FLAGS_USE_OF_DMA_CONFIG(&drv_info);
        int use_of_reserved_mem = GET_U_DMA_BUF_IOCTL_FLAGS_USE_OF_RESERVED_MEM(&drv_info);
        int use_quirk_mmap      = GET_U_DMA_BUF_IOCTL_FLAGS_USE_QUIRK_MMAP(&drv_info);
        int use_quirk_mmap_page = GET_U_DMA_BUF_IOCTL_FLAGS_USE_QUIRK_MMAP_PAGE(&drv_info);
	char* drv_version       = strdup(&drv_info.version[0]);
        close(fd);
    }
```

### `U_DMA_BUF_IOCTL_GET_SIZE`

This ioctl is for get size of a DMA Buffer.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        uint64_t buf_size;
        status = ioctl(fd, U_DMA_BUF_IOCTL_GET_SIZE, &buf_size);
        close(fd);
    }
```

### `U_DMA_BUF_IOCTL_GET_DMA_ADDR`

This ioctl is for get physical address of a DMA Buffer.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        uint64_t phys_addr;
        status = ioctl(fd, U_DMA_BUF_IOCTL_GET_DMA_ADDR, &phys_addr);
        close(fd);
    }
```

### `U_DMA_BUF_IOCTL_GET_SYNC_OWNER`

This ioctl is for get owner of the memory block in the manual cache management mode.
If this value is 1, the buffer is owned by the device.
If this value is 0, the buffer is owned by the cpu.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        int sync_owner;
        status = ioctl(fd, U_DMA_BUF_IOCTL_GET_SYNC_OWNER, &sync_owner);
        close(fd);
    }
```

### `U_DMA_BUF_IOCTL_SET_SYNC_FOR_CPU`

This ioctl writes a value to sync_for_cpu.
If '1' is written to sync_for_cpu, if `sync_direction` is 2(=DMA_FROM_DEVICE) or 0(=DMA_BIDIRECTIONAL),
the write to the device file invalidates a cache specified by `sync_offset` and `sync_size`.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        uint64_t sync_for_cpu = 1;
        status = ioctl(fd, U_DMA_BUF_IOCTL_SET_SYNC_FOR_CPU, &sync_for_cpu);
        close(fd);
    }
```

The value written to sync_for_cpu can include sync_offset, sync_size, and sync_direction. 

```C:u-dma-buf_test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        unsigned long sync_offset     = 0;
        unsigned long sync_size       = 0x10000;
        unsigned int  sync_direction  = 0;
        uint64_t      sync_for_cpu    = ((uint64_t)(sync_offset    & 0xFFFFFFFF) << 32) |
	                                ((uint64_t)(sync_size      & 0xFFFFFFF0) <<  0) |
					((uint64_t)(sync_direction & 0x00000003) <<  2) |
					0x00000001;
        status = ioctl(fd, U_DMA_BUF_IOCTL_SET_SYNC_FOR_CPU, &sync_for_cpu);
        close(fd);
    }
```

The sync_offset/sync_size/sync_direction specified by ```sync_for_cpu``` is temporary and does not affect the ```sync_offset``` or ```sync_size``` or ```sync_direction``` device files.

Details of manual cache management is described in the next section.


### `U_DMA_BUF_IOCTL_SET_SYNC_FOR_DEVICE`

This ioctl writes a value to sync_for_device.
If '1' is written to sync_for_device, if `sync_direction` is 1(=DMA_TO_DEVICE) or 0(=DMA_BIDIRECTIONAL),
the write to the device file flushes a cache specified by `sync_offset` and `sync_size` (i.e. the
cached data, if any, will be updated with data on DDR memory).

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        uint64_t sync_for_device = 1;
        status = ioctl(fd, U_DMA_BUF_IOCTL_SET_SYNC_FOR_DEVICE, &sync_for_device);
        close(fd);
    }
```

The value written to sync_for_cpu can include sync_offset, sync_size, and sync_direction. 

```C:u-dma-buf_test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        unsigned long sync_offset     = 0;
        unsigned long sync_size       = 0x10000;
        unsigned int  sync_direction  = 0;
        uint64_t      sync_for_device = ((uint64_t)(sync_offset    & 0xFFFFFFFF) << 32) |
	                                ((uint64_t)(sync_size      & 0xFFFFFFF0) <<  0) |
					((uint64_t)(sync_direction & 0x00000003) <<  2) |
					0x00000001;
        status = ioctl(fd, U_DMA_BUF_IOCTL_SET_SYNC_FOR_DEVICE, &sync_for_device);
        close(fd);
    }
```

The sync_offset/sync_size/sync_direction specified by ```sync_for_cpu``` is temporary and does not affect the ```sync_offset``` or ```sync_size``` or ```sync_direction``` device files.

Details of manual cache management is described in the next section.

### `U_DMA_BUF_IOCTL_GET_DEV_INFO`

This ioctl is for get device information.
The device information obtained by this ioctl includes physical address and size of a DMA Buffer.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        u_dma_buf_ioctl_dev_info dev_info = {0};
        status = ioctl(fd, U_DMA_BUF_IOCTL_GET_DEV_INFO, &dev_info);
        int      dma_mask     = GET_U_DMA_BUF_IOCTL_FLAGS_DMA_MASK(&dev_info);
	int      dma_coherent = GET_U_DMA_BUF_IOCTL_FLAGS_DMA_COHERENT(&dev_info);
	int      mmap_mode    = GET_U_DMA_BUF_IOCTL_FLAGS_MMAP_MODE(&dev_info);
        uint64_t phys_addr    = dev_info.addr;
        uint64_t buf_size     = dev_info.size;
        close(fd);
    }
```

### `U_DMA_BUF_IOCTL_GET_SYNC`

This ioctl is for get sync_offset/sync_size/sync_direction/sync_owner/sync_mode.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        u_dma_buf_ioctl_sync_args sync_args = {0};
        status = ioctl(fd, U_DMA_BUF_IOCTL_GET_SYNC, &sync_args);
	uint64_t sync_offset    = sync_args.offset;
	uint64_t sync_size      = sync_args.size;
	int      sync_direction = GET_U_DMA_BUF_IOCTL_FLAGS_SYNC_DIR(&sync_args);
	int      sync_owner     = GET_U_DMA_BUF_IOCTL_FLAGS_SYNC_OWNER(&sync_args);
	int      sync_mode      = GET_U_DMA_BUF_IOCTL_FLAGS_SYNC_MODE(&sync_args);
        close(fd);
    }
```

### `U_DMA_BUF_IOCTL_SET_SYNC`

This ioctl is for set sync_offset/sync_size/sync_direction/sync_mode.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        u_dma_buf_ioctl_sync_args sync_args = {0};
        uint64_t sync_offset    = 0;
        uint64_t sync_size      = 0x10000;
        int      sync_direction = 0;
	sync_args.offset = sync_offset;
	sync_args.size   = sync_size;
	SET_U_DMA_BUF_IOCTL_FLAGS_SYNC_DIR(&sync_args, sync_direction);
        status = ioctl(fd, U_DMA_BUF_IOCTL_SET_SYNC, &sync_args);
        close(fd);
    }
```

Also, by specifying a sync command in flags of the sync_args of this ioctl, sync_for_cpu or sync_for_device can be triggered.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        u_dma_buf_ioctl_sync_args sync_args = {0};
        uint64_t sync_offset    = 0;
        uint64_t sync_size      = 0x10000;
        int      sync_direction = 0;
	sync_args.offset = sync_offset;
	sync_args.size   = sync_size;
	SET_U_DMA_BUF_IOCTL_FLAGS_SYNC_DIR(&sync_args, sync_direction);
	SET_U_DMA_BUF_IOCTL_FLAGS_SYNC_CMD(&sync_args, U_DMA_BUF_IOCTL_FLAGS_SYNC_CMD_FOR_CPU);
        status = ioctl(fd, U_DMA_BUF_IOCTL_SET_SYNC, &sync_args);
        close(fd);
    }
```

Details of manual cache management is described in the next section.

### `U_DMA_BUF_IOCTL_EXPORT`

This ioctl is currently under development. Please use with caution.

This ioctl exports the specified range of u-dma-buf as PRIME DMA-BUFs.
PRIME DMA-BUFs here is an abbreviation for the Linux kernel's internal DMA buffer sharing API.
It provides a general mechanism for sharing DMA buffers between multiple devices managed by different types of device drivers.

The offset   field of u_dma_buf_ioctl_args specifies the offset of the area.
The size     field of u_dma_buf_ioctl_args specifies the size of the area.
The fd_flags field of u_dma_buf_ioctl_args specifies O_CLOEXEC, O_SYNC, O_RDWR, O_RDONLY, O_WRONLY.
Then execute ioctl U_DMA_BUF_IOCTL_EXPORT.
If successful, the fd field of u_dma_buf_ioctl_export_args contains a file descriptor indicating PRIME DMA-BUFs.
The resulting file descriptors indicating PRIME DMA-BUFs can be used to access the buffers using mmap().
In some cases, it is necessary to synchronize with the CPU cache before and after accessing buffers.
In such a case, execute ioctl DMA_BUF_IOCTL_SYNC with file descriptors indicating PRIME DMA-BUFs.

An example is shown below.

```C:u-dma-buf-ioctl-test.c
    if ((fd = open("/dev/udmabuf0", O_RDWR)) != -1) {
        u_dma_buf_ioctl_export_args export_args;
	export_args.offset = 0x00000000;
	export_args.size   = buf_size;
	SET_U_DMA_BUF_IOCTL_FLAGS_EXPORT_FD_FLAGS(&export_args, O_CLOEXEC | O_RDWR);
        status = ioctl(fd, U_DMA_BUF_IOCTL_EXPORT, &export_args);
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, export_args.fd, 0);
	struct dma_buf_sync sync_start = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW};
	status = ioctl(export_args.fd, DMA_BUF_IOCTL_SYNC, &sync_start);
        /* Do some read/write access to buf */
	struct dma_buf_sync sync_end   = {.flags = DMA_BUF_SYNC_END   | DMA_BUF_SYNC_RW};
	status = ioctl(export_args.fd, DMA_BUF_IOCTL_SYNC, &sync_end  );
        close(fd);
    }
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

  * sync_mode=0: CPU cache is enabled regardless of the `O_SYNC` flag presence.
  * sync_mode=1: If `O_SYNC` is specified, CPU cache is disabled.
    If `O_SYNC` is not specified, CPU cache is enabled.
  * sync_mode=2: If `O_SYNC` is specified, CPU cache is disabled but CPU uses
    write-combine when writing data to DMA buffer improves performance by combining
    multiple write accesses. If `O_SYNC` is not specified, CPU cache is enabled.
  * sync_mode=3: If `O_SYNC` is specified, DMA coherency mode is used.
    If `O_SYNC` is not specified, CPU cache is enabled.
  * sync_mode=4: CPU cache is enabled regardless of the `O_SYNC` flag presence. 
  * sync_mode=5: CPU cache is disabled regardless of the `O_SYNC` flag presence. 
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

If a problem occurs, either cache coherency is maintained by hardware, or use a method described below that manually cache management with CPU cache still being enabled.

### 2. Manual cache management with the CPU cache still being enabled

As explained above, by opening u-dma-buf without specifying the `O_SYNC` flag, CPU cache can be left turned on.
However, for ARM or ARM64, this is only possible if quirk-mmap is enabled.
quirk-mmap will be discussed in detail later.

```C:u-dma-buf_test.c
    /* To enable CPU cache on the DMA buffer, */
    /* open u-dma-buf without specifying the `O_SYNC` flag. */
    if ((fd  = open("/dev/udmabuf0", O_RDWR)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Read/write access to the buffer */
        close(fd);
    }

```

To manually manage cache coherency, users need to follow the 

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
written to `sync_for_device` to change ownership of the buffer to the accelerator.
Upon the write to `sync_for_device`, the CPU cache of the specified memory area is
flushed using data on the main memory.

However, if the `dma-coherent` property is specified in the device tree, CPU cache
is not invalidated and flushed.

**Note: What is quirk-mmap?**

The Linux Kernel mainline turns off caching when doing mmap() for architectures
such as ARM and ARM64 where cache aliasing problems can occur.

However, u-dma-buf provides quirk-mmap to enable caching in cases where the above
architecture does not cause cache alias problems.
The quirk-mmap is u-dma-buf's own mmap mechanism and does not utilize the dma_mmap_coherent()
provided by the dma-mapping API in the linux kernel.
This may cause problems in some cases, so please be careful when using it.

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
    test_size    = udmabuf.buf_size//(np.dtype(test_dtype).itemsize)
    udmabuf.memmap(dtype=test_dtype, shape=(test_size))
    comparison   = np.zeros(test_size, dtype=test_dtype)
    print ("test_size  : %d" % test_size)
    start        = time.time()
    test_1(udmabuf.array)
    elapsed_time = time.time() - start
    print ("udmabuf0   : elapsed_time:{0}".format(elapsed_time) + "[sec]")
    start        = time.time()
    test_1(comparison)
    elapsed_time = time.time() - start
    print ("comparison : elapsed_time:{0}".format(elapsed_time) + "[sec]")
    if np.array_equal(udmabuf.array, comparison):
        print ("udmabuf0 == comparison : OK")
    else:
        print ("udmabuf0 != comparison : NG")
```

## Execution result

Install u-dma-buf. In this example, 8MiB DMA buffer is reserved as "udmabuf0".

```console
zynq# insmod u-dma-buf.ko udmabuf0=8388608
[ 1183.911189] u-dma-buf udmabuf0: driver version = 5.0.0
[ 1183.921238] u-dma-buf udmabuf0: major number   = 240
[ 1183.931275] u-dma-buf udmabuf0: minor number   = 0
[ 1183.936063] u-dma-buf udmabuf0: phys address   = 0x0000000041600000
[ 1183.942328] u-dma-buf udmabuf0: buffer size    = 8388608
[ 1183.947641] u-dma-buf u-dma-buf.0: driver installed.
```

Executing the script in the previous section gives the following results.

```console
zynq# python3 udmabuf_test.py
test_size  : 8388608
udmabuf0   : elapsed_time:0.11204075813293457[sec]
comparison : elapsed_time:0.11488151550292969[sec]
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

