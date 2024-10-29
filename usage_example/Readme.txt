This readme is just for our internal usage scenarios. 

A far more detailed readme is available in the repository
https://github.com/ikwzm/udmabuf
https://github.com/tsisw/udmabuf (Our fork)

==============================================================================
Compiling (in aws arm setup):

To compile (compiles against the running kernel version and header files): 

(cd to udmabuf directory)
make clean
make all

To clean:   

(cd to udmabuf directory)
make clean


==============================================================================
Cross compiling (For FPGA setup):

[Needs the cross compilation related env setup and the target kernel source.]

[cross compilation env variables]

(cd to top folder)

export TOP_FOLDER=`pwd`
cd $TOP_FOLDER
echo "Current working dir (TOP_FOLDER): $TOP_FOLDER"
export PATH=/proj/local/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu/bin:$PATH
export ARCH=arm64
export CROSS_COMPILE=aarch64-none-linux-gnu-
set -e


[Target kernel source:
Out of tree module compilation requires the Module.symvers file in the 
target kernel directory. So it is necessary to execute a 'make modules' 
command as part of Linux kernel compilation/build steps.]
 
(cd to top folder)

cd $TOP_FOLDER
if [ -e linux-socfpga ]
then
echo "linux-socfpga exists"
cd linux-socfpga
export KERNEL_SRC=`pwd`
echo "KERNEL_SRC: $KERNEL_SRC"
cd $TOP_FOLDER
else
echo "linux-socfpga does not exist"
fi


[module compilation]

if [ -e contig_mem ]
then
echo "contig_mem exists"    
cd contig_mem/udmabuf
make clean
make all
else
mkdir contig_mem
cd contig_mem
cp -r /proj/work/dmohapatra/contig_mem/udmabuf ./
cd udmabuf
make clean
make all
fi

==============================================================================
Kernel configuration and boot-up command:

Depending on the need for size of the contiguous memory buffer(s), 
appropriate amount of physical memory needs to be reserved at boot up time 
for later usage. This can be achieved by either reserving memory at boot 
up time (by using device tree specifications or other mechanisms) or by 
configuring the Contiguous Memory Allocator 
mechanism inside the Linux kernel and providing kernel bootup command line 
parameter to reserve appropriate amount of CMA memory. 

Relevant kernel config options (debug options are not strictly necessary):
CONFIG_CMA=y

CONFIG_CMA_DEBUG=y
CONFIG_CMA_DEBUGFS=y
CONFIG_CMA_SYSFS=y
CONFIG_CMA_AREAS=19
CONFIG_DMA_CMA=y
CONFIG_DMA_PERNUMA_CMA=y

CONFIG_CMA_SIZE_MBYTES=16 (This can be changed appropriately)
CONFIG_CMA_SIZE_SEL_MBYTES=y
CONFIG_CMA_ALIGNMENT=8
CONFIG_DMA_API_DEBUG=y
CONFIG_DMA_API_DEBUG_SG=y


For reserving higher sizes of CMA memory (beyond the 0-4GB physical memory 
range), following config options need to be turned off.

# CONFIG_ZONE_DMA is not set
# CONFIG_ZONE_DMA32 is not set



Settings for other related config options, some of these may be automatically
switched on based on previous config settings:

CONFIG_HAVE_DMA_CONTIGUOUS=y
CONFIG_ARCH_HAS_ZONE_DMA_SET=y
CONFIG_DMA_SHARED_BUFFER=y
CONFIG_DMA_ENGINE=y
CONFIG_DMA_VIRTUAL_CHANNELS=y
CONFIG_DMA_ACPI=y
CONFIG_DMA_OF=y
CONFIG_IOMMU_DEFAULT_DMA_LAZY=y
CONFIG_IOMMU_DMA=y
CONFIG_HAS_DMA=y
CONFIG_DMA_OPS=y
CONFIG_NEED_SG_DMA_FLAGS=y
CONFIG_NEED_SG_DMA_LENGTH=y
CONFIG_NEED_DMA_MAP_STATE=y
CONFIG_ARCH_DMA_ADDR_T_64BIT=y
CONFIG_DMA_DECLARE_COHERENT=y
CONFIG_ARCH_HAS_SETUP_DMA_OPS=y
CONFIG_ARCH_HAS_TEARDOWN_DMA_OPS=y
CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE=y
CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU=y
CONFIG_ARCH_HAS_DMA_PREP_COHERENT=y
CONFIG_DMA_BOUNCE_UNALIGNED_KMALLOC=y
CONFIG_DMA_NONCOHERENT_MMAP=y
CONFIG_DMA_COHERENT_POOL=y
CONFIG_DMA_DIRECT_REMAP=y

# CONFIG_DMABUF_HEAPS_CMA is not set


The primary advantage of the CMA based approach over reserved memory is: 
physical memory reserved by CMA can be utilized by Linux kernel for other 
purposes (page/buffer cache) until it is claimed by DMA apis, after which 
it is no longer used for other purposes. CMA memory is not used by kernel 
memory allocators (SLAB/SLUB etc.) at any point. 

Reserved physical memory is not used by kernel and has to be explicitly 
configured to be utilized by modules/drivers. 

Passing 'cma=xxx' as part of kernel command line reserves the necessary 
amount of memory for CMA if kernel is configured with CMA feature enabled.

[The kernel config option in the earlier step could be changed to reserve
the necessary amount of CMA, but this may not be the right approach]

Examples:
AWS setup grub command line addition:

menuentry 'Ubuntu, with Linux 6.5.13 cma' --class ubuntu --class gnu-linux --class gnu --class os $menuentry_id_option 'gnulinux-6.5.13-advanced-b993fcf5-2d55-493c-b232-97ca4539f4e9' {
	recordfail
	load_video
	gfxmode $linux_gfx_mode
	insmod gzio
	if [ x$grub_platform = xxen ]; then insmod xzio; insmod lzopio; fi
	insmod part_gpt
	insmod ext2
	search --no-floppy --fs-uuid --set=root b993fcf5-2d55-493c-b232-97ca4539f4e9
	echo	'Loading Linux 6.5.13 ...'
	if [ "${initrdfail}" = 1 ]; then
	  echo	'GRUB_FORCE_PARTUUID set, initrdless boot failed. Attempting with initrd.'
	  linux	/boot/vmlinuz-6.5.13 root=PARTUUID=26f22d2b-892e-459e-ba29-0fdab351f296 ro  console=tty1 console=ttyS0 cma=8G nvme_core.io_timeout=4294967295
	  echo	'Loading initial ramdisk ...'
	  initrd	/boot/initrd.img-6.5.13
	else
	  echo	'GRUB_FORCE_PARTUUID set, attempting initrdless boot.'
	  linux	/boot/vmlinuz-6.5.13 root=PARTUUID=26f22d2b-892e-459e-ba29-0fdab351f296 ro  console=tty1 console=ttyS0 cma=8G nvme_core.io_timeout=4294967295 panic=-1
	fi
	initrdfail
}


In FPGA setup, setting Uboot command line by modifying the nandfitboot env variable: 

setenv nandfitboot "setenv bootargs earlycon panic=-1 root=${nandroot} rw rootwait rootfstype=ubifs ubi.mtd=1 cma=256M;  bootm ${loadaddr} "

==============================================================================
Relevant module parameters:
The parameters most relevant for our usage are the size parameters. The 
kernel module supports creation of upto 8 buffers (udmabuf0,...udmabuf7), 
the sizes (in bytes) for which can be passed as module parameters during 
insmod. Our usage scenario requires creation of one (or at the most two) 
contiguous physical memory buffer(s).

Another relevant parameter is the dma_mask_bit. This is relevant when trying 
to allocate buffers of larger size. If the physical address of the reserved 
memory or the CMA memory (at system startup) is not in the 0-4GB range, then 
for allocating dma buffers, a mask of 64 bits (dma_mask_bit = 64) 
is needed, otherwise insmod will fail to create
the necessary physical (DMA) memory buffers. 

For fpga setup, where less than 4GB of physical memory is present, the 
default value of 32 for dma_mask_bit works.

There is the 'mmap' operation specific parameter 'quirk_mmap_mode' (which 
depends on the coherency between DMA buffers and cpu cache) and is turned on 
by default (in the module) for ARM setups. This parameter and its usage
implications needs more investigation. Refer to miscellaneous section third
item for more details. 

==============================================================================
Inserting the module:

Example:

(For creating 1 buffer of size 1MB)
sudo insmod u-dma-buf.ko udmabuf0=1048576

(For creating 2 buffers of size 4GB and 2GB. Setting dma_mask_bit = 64 will 
be necessary in this case )
sudo insmod u-dma-buf.ko udmabuf0=4294967296 udmabuf1=2147483648 dma_mask_bit=64

For each buffer, an entry is created in /dev like /dev/udmabuf0 
/dev/udmabuf1 etc. By default these are only accessible to root.
Do the following to allow access to non-root users:

sudo chmod 777 /dev/udmabuf0 (change 777 to appropriate value)
sudo chmod 777 /dev/udmabuf1

[The above can also be done by creating appropriate udev rules]

==============================================================================
Miscellaneous:
1. Example usage code is provided in devbuf_map.c. A virtual to physical 
   address translation routine has been added which translates virtual 
   addresses to physical address for the mapped range of a particular device
   buffer. The device buffer is identified by the 'bufnum' parameter (bufnum 
   0 corresponds to buffer udmabuf0, 1 to udmabuf1 ..)
2. At present the address translation happens by getting the starting physical
   memory address for the buffer from the the kernel module by making IOCTL
   commands.
3. The generic virtual to physical address translation in the user space
   happens using the procfs interfaces /proc/<pid>/maps and /proc/<pid>/pagemap.
   At present doing this translation results in the corresponding pfns (for 
   the virtual address ranges created by memory mapping the device buffers) 
   showing up as 0. This is despite the fact that the virtual address range 
   is read/ write accessible from multiple processes and the changes made by 
   one process is seen by the other process. The cause for this behavior 
   and if the 'quirk_mmap_mode' mode is causing this needs more investigation.    
        

==============================================================================

TODO Items:
- Investigate the issue of physical pfns (page frame number) showing up as 
  zero when doing virt-to-phys translation in user space using procfs 
  interfaces.
- Investigate the 'quirk_mmap_mode' and find out if its usage is missing some 
  ioremap step which is causing the issue of zero pfn.
- Investigate the usage of device tree and reserved memory and configuring 
  those to be available for DMA apis and the udmabuf module/driver. We are
  already using reserved memory in the FPGA setup using device tree.
- In tree compilation of the module as opposed to out-of-tree compilation
  if needed.
