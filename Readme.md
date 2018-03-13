udmabuf(User space mappable DMA Buffer)
=======================================

# Overview

## Introduction of udmabuf

udmabuf is a Linux device driver that allocates contiguous memory blocks in the kernel space as DMA buffers and makes them available from the user space. It is intended that these memory blocks are used as DMA buffers when a user application implements device driver in user space using UIO (User space I/O).

A DMA buffer allocated by udmabuf can be accessed from the user space by opneing the device file (e.g. /dev/udmabuf0) and mapping to the user memory space, or using the read()/write() functions.

CPU cache for the allocated DMA buffer can be disabled by setting the `O_SYNC` flag when opening the device file. It is also possible to flush or invalidate CPU cache while retaining CPU cache enabled.

The physical address of a DMA buffer allocated by udmabuf can be obtained by reading `/sys/class/udmabuf/udmabuf0/phys_addr`.

The size of a DMA buffer and the device minor number can be specified when the device driver is loaded (e.g. when loaded via the `insmod` command). Some platforms allow to specify them in the device tree.

## Architecture of udmabuf

![Figure 1. Architecture ](./udmabuf1.jpg "Figure 1. Architecture")

Figure 1. Architecture

<br />

## Supported platforms

* OS : Linux Kernel Version 3.6 - 3.8, 3.18, 4.4, 4.8, 4.12, 4.14 (the author tested on 3.18, 4.4, 4.8, 4.12, 4.14).
* CPU: ARM Cortex-A9 (Xilinx ZYNQ / Altera CycloneV SoC)
* CPU: ARM64 Cortex-A53 (Xilinx ZYNQ UltraScale+ MPSoC)
* CPU: x86(64bit) However, verification is not enough. I hope the results from everyone.
  In addition, there is a limit to the following feature at the moment.
  - Can not control of the CPU cache by O_SYNC flag . Always CPU cache is valid.
  - Can not various settings by the device tree.

# Usage

## Compile

The following `Makefile` is included in the repository.

```Makefile:Makefile
HOST_ARCH       ?= $(shell uname -m | sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)
ARCH            ?= $(shell uname -m | sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)
KERNEL_SRC_DIR  ?= /lib/modules/$(shell uname -r)/build

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

obj-m := udmabuf.o

all:
	make -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules

clean:
	make -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

```

## Install

Load the udmabuf kernel driver using `insmod`. The size of a DMA buffer should be provided as an argument as follows. The device driver is created, and allocates a DMA buffer with the specified size. The maximum number of DMA buffers that can be allocated using `insmod` is 8 (udmabuf0/1/2/3/4/5/6/7).

```Shell
zynq$ insmod udmabuf.ko udmabuf0=1048576
udmabuf udmabuf0: driver installed
udmabuf udmabuf0: major number   = 248
udmabuf udmabuf0: minor number   = 0
udmabuf udmabuf0: phys address   = 0x1e900000
udmabuf udmabuf0: buffer size    = 1048576
udmabuf udmabuf0: dma coherent   = 0
zynq$ ls -la /dev/udmabuf0
crw------- 1 root root 248, 0 Dec  1 09:34 /dev/udmabuf0
```

In the above result, the device is only read/write accessible by root. If the permission needs to be changed at the load of the kernel module, create `/etc/udev/rules.d/99-udmabuf.rules` with the following content.

```rules:99-udmabuf.rules
KERNEL=="udmabuf[0-9]*", GROUP="root", MODE="0666"
```

The module can be uninstalled by the `rmmod` command.

```Shell
zynq$ rmmod udmabuf
udmabuf udmabuf0: driver uninstalled
```



## Configuration via the device tree file

In addition to the allocation via the `insmod` command and its arguments, DMA buffers can be allocated by specifying the size in the device tree file. When a device tree file contains an entry like the following, udmabuf will allocate buffers and create device drivers when loaded by `insmod`.

```devicetree:devicetree.dts
		udmabuf@0x00 {
			compatible = "ikwzm,udmabuf-0.10.a";
			device-name = "udmabuf0";
			minor-number = <0>;
			size = <0x00100000>;
		};

```

The DMA buffer size can be specified via the `size` option.

The name of the device can be specified via the `device-name` option.

The `minor-number` option is used to set the minor number. The valid minor number range is 0 to 255. A minor number provided as `insmod` argument will has higher precedence, and when definition in the device tree has colliding number, creation of the device defined in the device tree will fail. When the minor number is not specified, udmabuf automatically assigns an appropriate one.

The device name is determined as follow:

1. If `device-name` is specifed use `device-name`.
2. If `device-name` is not present, and if `minor-number` is specified, `sprintf("udmabuf%d", minor-number)` is used.
3. If `device-name` is not present, and if `minor-number` is not present, the entry name of the device tree is used (`udmabuf@0x00` in this example).

```Shell
zynq$ insmod udmabuf.ko
udmabuf udmabuf0: driver installed
udmabuf udmabuf0: major number   = 248
udmabuf udmabuf0: minor number   = 0
udmabuf udmabuf0: phys address   = 0x1e900000
udmabuf udmabuf0: buffer size    = 1048576
udmabuf udmabuf0: dma coherent   = 0
zynq$ ls -la /dev/udmabuf0
crw------- 1 root root 248, 0 Dec  1 09:34 /dev/udmabuf0
```


## Device file

When udmabuf is loaded into the kernel, the following device files are created.
`<device-name>` is a placeholder for the device name described in the previous section.

* /dev/\<device-name\>
* /sys/class/udmabuf/\<device-name\>/phys_addr
* /sys/class/udmabuf/\<device-name\>/size
* /sys/class/udmabuf/\<device-name\>/sync_mode
* /sys/class/udmabuf/\<device-name\>/sync_offset
* /sys/class/udmabuf/\<device-name\>/sync_size
* /sys/class/udmabuf/\<device-name\>/sync_direction
* /sys/class/udmabuf/\<device-name\>/sync_owner
* /sys/class/udmabuf/\<device-name\>/sync_for_cpu
* /sys/class/udmabuf/\<device-name\>/sync_for_device


### /dev/\<device-name\>

`/dev/<device-name>` is used when `mmap()`-ed to the user space or accessed via `read()`/`write()`.

```C:udmabuf_test.c
    if ((fd  = open("/dev/udmabuf0", O_RDWR)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Do some read/write access to buf */
        close(fd);
    }

```

The device file can be directly read/written by specifying the device as the target of `dd` in the shell.

```Shell
zynq$ dd if=/dev/urandom of=/dev/udmabuf0 bs=4096 count=1024
1024+0 records in
1024+0 records out
4194304 bytes (4.2 MB) copied, 3.07516 s, 1.4 MB/s
```

```Shell
zynq$dd if=/dev/udmabuf4 of=random.bin
8192+0 records in
8192+0 records out
4194304 bytes (4.2 MB) copied, 0.173866 s, 24.1 MB/s
```

### phys_addr

The physical address of a DMA buffer can be retrieved by reading `/sys/class/udmabuf/<device-name>/phys_addr`.

```C:udmabuf_test.c
    unsigned char  attr[1024];
    unsigned long  phys_addr;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/phys_addr", O_RDONLY)) != -1) {
        read(fd, attr, 1024);
        sscanf(attr, "%x", &phys_addr);
        close(fd);
    }

```

### size

The size of a DMA buffer can be retrieved by reading `/sys/class/udmabuf/<device-name>/size`.

```C:udmabuf_test.c
    unsigned char  attr[1024];
    unsigned int   buf_size;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/size", O_RDONLY)) != -1) {
        read(fd, attr, 1024);
        sscanf(attr, "%d", &buf_size);
        close(fd);
    }

```

### sync_mode

The device file `/sys/class/udmabuf/<device-name>/sync_mode`  is used to configure the behavior when udmabuf is opened with the `O_SYNC` flag.

```C:udmabuf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_mode = 2;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/sync_mode", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_mode);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details on `O_SYNC` and cache management will be described in the next section.

### sync_offset

The device file `/sys/class/udmabuf/<device-name>/sync_offset` is used to specify the start address of a memory block of which cache is manually managed.

```C:udmabuf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_offset = 0x00000000;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/sync_offset", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_offset);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details of manual cache management is described in the next section.

### sync_size

The device file `/sys/class/udmabuf/<device-name>/sync_size` is used to specify the size of a memory block of which cache is manually managed.

```C:udmabuf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_size = 1024;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/sync_size", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_size);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details of manual cache management is described in the next section.

### sync_direction

The device file `/sys/class/udmabuf/<device-name>/sync_direction` is used to set the direction of DMA transfer to/from the DMA buffer of which cache is manually managed.

- 0: sets DMA_BIDIRECTIONAL
- 1: sets DMA_TO_DEVICE
- 2: sets DMA_FROM_DEVICE

```C:udmabuf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_direction = 1;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/sync_direction", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_direction);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details of manual cache management is described in the next section.

### sync_owner

The device file `/sys/class/udmabuf/<device-name>/sync_owner` reports the owner of the memory block in the manual cache management mode.

```C:udmabuf_test.c
    unsigned char  attr[1024];
    int sync_owner;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/sync_owner", O_RDONLY)) != -1) {
        read(fd, attr, 1024);
        sscanf(attr, "%x", &sync_owner);
        close(fd);
    }

```

Details of manual cache management is described in the next section.

### sync_for_cpu

In the manual cache management mode, CPU can be the owner of the buffer by writing `1` to the device file `/sys/class/udmabuf/<device-name>/sync_for_cpu`. If `sync_direction` is 2(=DMA_FROM_DEVICE) or 0(=DMA_BIDIRECTIONAL), the write to the device file invalidates a cache specified by `sync_offset` and `sync_size`.

```C:udmabuf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_for_cpu = 1;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/sync_for_cpu", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_for_cpu);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details of manual cache management is described in the next section.

### sync_for_device

In the manual cache management mode, DEVICE can be the owner of the buffer by writing `1` to the device file `/sys/class/udmabuf/<device-name>/sync_for_device`. If `sync_direction` is 1(=DMA_TO_DEVICE) or 0(=DMA_BIDIRECTIONAL), the write to the device file flushes a cache specified by `sync_offset` and `sync_size` (i.e. the cached data, if any, will be updated with data on DDR memory).

```C:udmabuf_test.c
    unsigned char  attr[1024];
    unsigned long  sync_for_device = 1;
    if ((fd  = open("/sys/class/udmabuf/udmabuf0/sync_for_device", O_WRONLY)) != -1) {
        sprintf(attr, "%d", sync_for_device);
        write(fd, attr, strlen(attr));
        close(fd);
    }
```

Details of manual cache management is described in the next section.

# Coherency of data on DMA buffer and CPU cache

CPU usually accesses to a DMA buffer on the main memory using cache, and a hardware accelerator logic accesses to data stored in the DMA buffer on the main memory. In this situation, coherency between data stored on CPU cache and them on the main memory should be considered carefully.

## When the coherency is maintained by hardware

When hardware assures the coherency, CPU cache can be turned on without additional treatment. For example, ZYNQ provides ACP (Accelerator Coherency Port), and the coherency is maintained by hardware as long as the accelerator accesses to the main memory via this port.

In this case, accesses from CPU to the main memory can be fast by using CPU cache as usual. To enable CPU cache on the DMA buffer allocated by udmabuf, open udmabuf without specifying the `O_SYNC` flag.

```C:udmabuf_test.c
    /* To enable CPU cache on the DMA buffer, */
    /* open udmabuf without specifying the `O_SYNC` flag. */
    if ((fd  = open("/dev/udmabuf0", O_RDWR)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Read/write access to the buffer */
        close(fd);
    }

```

The manual management of cache, described in the following section, will not be necessary when hardware maintains the coherency.

## When hardware does not maintain the coherency

To maintain coherency of data between CPU and the main memory, another coherency mechanism is necessary. udmabuf supports two different ways of coherency maintenance; one is to disable CPU cache, and the other is to involve manual cache flush/invalidation with CPU cache being enabled.

### 1. Disabling CPU cache

To disable CPU cache of allocated DMA buffer, specify the `O_SYNC` flag when opening udmabuf.

```C:udmabuf_test.c
    /* To disable CPU cache on the DMA buffer, */
    /* open udmabuf with the `O_SYNC` flag. */
    if ((fd  = open("/dev/udmabuf0", O_RDWR | O_SYNC)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Read/write access to the buffer */
        close(fd);
    }

```


As listed below, `sync_mode` can be used to configure the cache behavior when the `O_SYNC` flag is present in `open()`:

* sync_mode=0:  CPU cache is enabled regardless of the `O_SYNC` flag presense.
* sync_mode=1: If `O_SYNC` is specified, CPU cache is disabled. If `O_SYNC` is not specified, CPU cache is enabled.
* sync_mode=2: If `O_SYNC` is specified, CPU cache is diabled but CPU uses write-combine when writing data to DMA buffer improves performance by combining multiple write accesses. If `O_SYNC` is not specified, CPU cache is enabled.
* sync_mode=3: If `O_SYNC` is specified, DMA coherency mode is used. If `O_SYNC` is not specified, CPU cache is enabled.
* sync_mode=4:  CPU cache is enabled regardless of the `O_SYNC` flag presense. 
* sync_mode=5: CPU cache is disabled regardless of the `O_SYNC` flag presense. 
* sync_mode=6: CPU uses write-combine to write data to DMA buffer regardless of `O_SYNC` presence.
* sync_mode=7: DMA coherency mode is used regardless of `O_SYNC` presence.

As a practical example, the execution times of a sample program listed below were measured under several test conditions as presented in the table.

```C:udmabuf_test.c
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


### 2. Manual cache management with the CPU canche still being enabled

As explained above, by opening udmabuf without specifying the `O_SYNC` flag, CPU cache can be left turned on.

```C:udmabuf_test.c
    /* To enable CPU cache on the DMA buffer, */
    /* open udmabuf without specifying the `O_SYNC` flag. */
    if ((fd  = open("/dev/udmabuf0", O_RDWR)) != -1) {
        buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        /* Read/write access to the buffer */
        close(fd);
    }

```

To manualy manage cache coherency, users need to follow the 

1. Specify a memory area shared between CPU and accelerator via `sync_offset` and `sync_size` device files. `sync_offset` accepts an offset from the start address of the allocated buffer in units of bytes. The size of the shared memory area should be set to `sync_size` in units of bytes.
2. Data transfer direction should be set to `sync_direction`. If the accelerator performs only read accesses to the memory area, `sync_direction` should be set to `1(=DMA_TO_DEVICE)`, and to `2(=DMA_FROM_DEVICE)` if only write accesses. 
3. If the accelerator reads and writes data from/to the memory area, `sync_direction` should be set to `0(=DMA_BIDIRECTIONAL)`.

Following the above configuration, `sync_for_cpu` and/or `sync_for_device` should be used to set the owner of the buffer specified by the above-mentioned offset and the size. 

When CPU accesses to the buffer, '1' should be written to `sync_for_cpu` to set CPU as the owner. Upon the write to `sync_for_cpu`, CPU cache is invalidated if `sync_direction` is `2(=DMA_FROM_DEVICE)` or `0(=DMA_BIDIRECTIONAL)`. Once CPU is becomes the owner of the buffer, the accelerator cannot access the buffer. 

On the other hand, when the accelerator needs to access the buffer, '1' should be written to `sync_for_device` to change owership of the buffer to the accelerator. Upon the write to `sync_for_device`, the CPU cache of the specified memory area is flushed using data on the main memory.