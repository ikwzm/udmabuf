/*********************************************************************************
 *
 *       Copyright (C) 2015-2019 Ichiro Kawazome
 *       All rights reserved.
 * 
 *       Redistribution and use in source and binary forms, with or without
 *       modification, are permitted provided that the following conditions
 *       are met:
 * 
 *         1. Redistributions of source code must retain the above copyright
 *            notice, this list of conditions and the following disclaimer.
 * 
 *         2. Redistributions in binary form must reproduce the above copyright
 *            notice, this list of conditions and the following disclaimer in
 *            the documentation and/or other materials provided with the
 *            distribution.
 * 
 *       THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *       "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *       LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *       A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 *       OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *       SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *       LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *       DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *       THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *       (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *       OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 ********************************************************************************/
#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/scatterlist.h>
#include <linux/pagemap.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <asm/page.h>
#include <asm/byteorder.h>

/**
 * DOC: Udmabuf Constants 
 */

MODULE_DESCRIPTION("User space mappable DMA buffer device driver");
MODULE_AUTHOR("ikwzm");
MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_VERSION     "1.4.2-rc1"
#define DRIVER_NAME        "udmabuf"
#define DEVICE_NAME_FORMAT "udmabuf%d"
#define DEVICE_MAX_NUM      256
#define UDMABUF_DEBUG       1

#if     defined(CONFIG_ARM) || defined(CONFIG_ARM64)
#define USE_VMA_FAULT       1
#else
#define USE_VMA_FAULT       0
#endif

#if     ((LINUX_VERSION_CODE >= 0x031300) && (defined(CONFIG_ARM) || defined(CONFIG_ARM64)))
#define USE_DMA_COHERENT    1
#else
#define USE_DMA_COHERENT    0
#endif

#if     (LINUX_VERSION_CODE >= 0x030B00)
#define USE_DEV_GROUPS      1
#else
#define USE_DEV_GROUPS      0
#endif

#if     ((LINUX_VERSION_CODE >= 0x040100) && defined(CONFIG_OF))
#define USE_OF_RESERVED_MEM 1
#else
#define USE_OF_RESERVED_MEM 0
#endif

#if     ((LINUX_VERSION_CODE >= 0x040100) && defined(CONFIG_OF))
#define USE_OF_DMA_CONFIG   1
#else
#define USE_OF_DMA_CONFIG   0
#endif

#if     (UDMABUF_DEBUG == 1)
#define UDMABUF_DEBUG_CHECK(this,debug) (this->debug)
#else
#define UDMABUF_DEBUG_CHECK(this,debug) (0)
#endif

#if     (USE_OF_RESERVED_MEM == 1)
#include <linux/of_reserved_mem.h>
#endif

/**
 * DOC: Udmabuf Static Variables
 *
 * * udmabuf_sys_class - udmabuf system class
 * * init_enable       - udmabuf install/uninstall infomation enable
 * * dma_mask_bit      - udmabuf dma mask bit
 */

/**
 * udmabuf_sys_class - udmabuf system class
 */
static struct class*  udmabuf_sys_class = NULL;

/**
 * info_enable module parameter
 */
static int        info_enable = 1;
module_param(     info_enable , int, S_IRUGO);
MODULE_PARM_DESC( info_enable , "udmabuf install/uninstall infomation enable");

/**
 * dma_mask_bit module parameter
 */
static int        dma_mask_bit = 32;
module_param(     dma_mask_bit, int, S_IRUGO);
MODULE_PARM_DESC( dma_mask_bit, "udmabuf dma mask bit(default=32)");

/**
 * DOC: Udmabuf Device Data Structure
 *
 * This section defines the structure of udmabuf device.
 *
 */

/**
 * struct udmabuf_device_data - udmabuf device data structure.
 */
struct udmabuf_device_data {
    struct device*       sys_dev;
    struct device*       dma_dev;
    struct cdev          cdev;
    dev_t                device_number;
    struct mutex         sem;
    bool                 is_open;
    int                  size;
    size_t               alloc_size;
    void*                virt_addr;
    dma_addr_t           phys_addr;
    int                  sync_mode;
    u64                  sync_offset;
    size_t               sync_size;
    int                  sync_direction;
    bool                 sync_owner;
    u64                  sync_for_cpu;
    u64                  sync_for_device;
#if (USE_OF_RESERVED_MEM == 1)
    bool                 of_reserved_mem;
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_VMA_FAULT == 1))
    bool                 debug_vma;
#endif   
};

/**
 * sync_mode(synchronous mode) value
 */
#define SYNC_MODE_INVALID       (0x00)
#define SYNC_MODE_NONCACHED     (0x01)
#define SYNC_MODE_WRITECOMBINE  (0x02)
#define SYNC_MODE_DMACOHERENT   (0x03)
#define SYNC_MODE_MASK          (0x03)
#define SYNC_MODE_MIN           (0x01)
#define SYNC_MODE_MAX           (0x03)
#define SYNC_ALWAYS             (0x04)

/**
 * DOC: Udmabuf System Class Device File Description
 *
 * This section define the device file created in system class when udmabuf is 
 * loaded into the kernel.
 *
 * The device file created in system class is as follows.
 *
 * * /sys/class/udmabuf/<device-name>/driver_version
 * * /sys/class/udmabuf/<device-name>/phys_addr
 * * /sys/class/udmabuf/<device-name>/size
 * * /sys/class/udmabuf/<device-name>/sync_mode
 * * /sys/class/udmabuf/<device-name>/sync_offset
 * * /sys/class/udmabuf/<device-name>/sync_size
 * * /sys/class/udmabuf/<device-name>/sync_direction
 * * /sys/class/udmabuf/<device-name>/sync_owner
 * * /sys/class/udmabuf/<device-name>/sync_for_cpu
 * * /sys/class/udmabuf/<device-name>/sync_for_device
 * * /sys/class/udmabuf/<device-name>/dma_coherent
 * * 
 */

#define  SYNC_COMMAND_DIR_MASK        (0x000000000000000C)
#define  SYNC_COMMAND_DIR_SHIFT       (2)
#define  SYNC_COMMAND_SIZE_MASK       (0x00000000FFFFFFF0)
#define  SYNC_COMMAND_SIZE_SHIFT      (0)
#define  SYNC_COMMAND_OFFSET_MASK     (0xFFFFFFFF00000000)
#define  SYNC_COMMAND_OFFSET_SHIFT    (32)
#define  SYNC_COMMAND_ARGMENT_MASK    (0xFFFFFFFFFFFFFFFE)
/**
 * udmabuf_sync_command_argments() - get argment for dma_sync_single_for_cpu() or dma_sync_single_for_device()
 *                                  
 * @this:       Pointer to the udmabuf device data structure.
 * @command     sync command (this->sync_for_cpu or this->sync_for_device)
 * @phys_addr   Pointer to the phys_addr for dma_sync_single_for_...()
 * @size        Pointer to the size for dma_sync_single_for_...()
 * @direction   Pointer to the direction for dma_sync_single_for_...()
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_sync_command_argments(
    struct udmabuf_device_data *this     ,
    u64                         command  ,
    dma_addr_t                 *phys_addr,
    size_t                     *size     ,
    enum dma_data_direction    *direction
) {
    u64    sync_offset   ;
    size_t sync_size     ;
    int    sync_direction;
    if ((command & SYNC_COMMAND_ARGMENT_MASK) != 0) {
        sync_offset    = (u64   )((command & SYNC_COMMAND_OFFSET_MASK) >> SYNC_COMMAND_OFFSET_SHIFT);
        sync_size      = (size_t)((command & SYNC_COMMAND_SIZE_MASK  ) >> SYNC_COMMAND_SIZE_SHIFT  );
        sync_direction = (int   )((command & SYNC_COMMAND_DIR_MASK   ) >> SYNC_COMMAND_DIR_SHIFT   );
    } else {
        sync_offset    = this->sync_offset;
        sync_size      = this->sync_size;
        sync_direction = this->sync_direction;
    }
    if (sync_offset + sync_size > this->size)
        return -EINVAL;
    switch(sync_direction) {
        case 1 : *direction = DMA_TO_DEVICE    ; break;
        case 2 : *direction = DMA_FROM_DEVICE  ; break;
        default: *direction = DMA_BIDIRECTIONAL; break;
    }
    *phys_addr = this->phys_addr + sync_offset;
    *size      = sync_size;
    return 0;
} 

/**
 * udmabuf_sync_for_cpu() - call dma_sync_single_for_cpu() when (sync_for_cpu != 0)
 * @this:       Pointer to the udmabuf device data structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_sync_for_cpu(struct udmabuf_device_data* this)
{
    int status = 0;

    if (this->sync_for_cpu) {
        dma_addr_t              phys_addr;
        size_t                  size;
        enum dma_data_direction direction;
        status = udmabuf_sync_command_argments(this, this->sync_for_cpu, &phys_addr, &size, &direction);
        if (status == 0) {
            dma_sync_single_for_cpu(this->dma_dev, phys_addr, size, direction);
            this->sync_for_cpu = 0;
            this->sync_owner   = 0;
        }
    }
    return status;
}

/**
 * udmabuf_sync_for_device() - call dma_sync_single_for_device() when (sync_for_device != 0)
 * @this:       Pointer to the udmabuf device data structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_sync_for_device(struct udmabuf_device_data* this)
{
    int status = 0;

    if (this->sync_for_device) {
        dma_addr_t              phys_addr;
        size_t                  size;
        enum dma_data_direction direction;
        status = udmabuf_sync_command_argments(this, this->sync_for_device, &phys_addr, &size, &direction);
        if (status == 0) {
            dma_sync_single_for_device(this->dma_dev, phys_addr, size, direction);
            this->sync_for_device = 0;
            this->sync_owner      = 1;
        }
    }
    return status;
}

#define DEF_ATTR_SHOW(__attr_name, __format, __value) \
static ssize_t udmabuf_show_ ## __attr_name(struct device *dev, struct device_attribute *attr, char *buf) \
{                                                            \
    ssize_t status;                                          \
    struct udmabuf_device_data* this = dev_get_drvdata(dev); \
    if (mutex_lock_interruptible(&this->sem) != 0)           \
        return -ERESTARTSYS;                                 \
    status = sprintf(buf, __format, (__value));              \
    mutex_unlock(&this->sem);                                \
    return status;                                           \
}

static inline int NO_ACTION(struct udmabuf_device_data* this){return 0;}

#define DEF_ATTR_SET(__attr_name, __min, __max, __pre_action, __post_action) \
static ssize_t udmabuf_set_ ## __attr_name(struct device *dev, struct device_attribute *attr, const char *buf, size_t size) \
{ \
    ssize_t       status; \
    u64           value;  \
    struct udmabuf_device_data* this = dev_get_drvdata(dev);                 \
    if (0 != mutex_lock_interruptible(&this->sem)){return -ERESTARTSYS;}     \
    if (0 != (status = kstrtoull(buf, 0, &value))){            goto failed;} \
    if ((value < __min) || (__max < value)) {status = -EINVAL; goto failed;} \
    if (0 != (status = __pre_action(this)))       {            goto failed;} \
    this->__attr_name = value;                                               \
    if (0 != (status = __post_action(this)))      {            goto failed;} \
    status = size;                                                           \
  failed:                                                                    \
    mutex_unlock(&this->sem);                                                \
    return status;                                                           \
}

DEF_ATTR_SHOW(driver_version , "%s\n"    , DRIVER_VERSION                                 );
DEF_ATTR_SHOW(size           , "%d\n"    , this->size                                     );
DEF_ATTR_SHOW(phys_addr      , "%pad\n"  , &this->phys_addr                               );
DEF_ATTR_SHOW(sync_mode      , "%d\n"    , this->sync_mode                                );
DEF_ATTR_SET( sync_mode                  , 0, 7,        NO_ACTION, NO_ACTION              );
DEF_ATTR_SHOW(sync_offset    , "0x%llx\n", this->sync_offset                              );
DEF_ATTR_SET( sync_offset                , 0, U64_MAX,  NO_ACTION, NO_ACTION              );
DEF_ATTR_SHOW(sync_size      , "%zu\n"   , this->sync_size                                );
DEF_ATTR_SET( sync_size                  , 0, SIZE_MAX, NO_ACTION, NO_ACTION              );
DEF_ATTR_SHOW(sync_direction , "%d\n"    , this->sync_direction                           );
DEF_ATTR_SET( sync_direction             , 0, 2,        NO_ACTION, NO_ACTION              );
DEF_ATTR_SHOW(sync_owner     , "%d\n"    , this->sync_owner                               );
DEF_ATTR_SHOW(sync_for_cpu   , "%llu\n"  , this->sync_for_cpu                             );
DEF_ATTR_SET( sync_for_cpu               , 0, U64_MAX,  NO_ACTION, udmabuf_sync_for_cpu   );
DEF_ATTR_SHOW(sync_for_device, "%llu\n"  , this->sync_for_device                          );
DEF_ATTR_SET( sync_for_device            , 0, U64_MAX,  NO_ACTION, udmabuf_sync_for_device);
#if (USE_DMA_COHERENT == 1)
DEF_ATTR_SHOW(dma_coherent   , "%d\n"    , is_device_dma_coherent(this->dma_dev)          );
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_VMA_FAULT == 1))
DEF_ATTR_SHOW(debug_vma      , "%d\n"    , this->debug_vma                                );
DEF_ATTR_SET( debug_vma                  , 0, 1,        NO_ACTION, NO_ACTION              );
#endif

static struct device_attribute udmabuf_device_attrs[] = {
  __ATTR(driver_version , 0444, udmabuf_show_driver_version  , NULL                       ),
  __ATTR(size           , 0444, udmabuf_show_size            , NULL                       ),
  __ATTR(phys_addr      , 0444, udmabuf_show_phys_addr       , NULL                       ),
  __ATTR(sync_mode      , 0664, udmabuf_show_sync_mode       , udmabuf_set_sync_mode      ),
  __ATTR(sync_offset    , 0664, udmabuf_show_sync_offset     , udmabuf_set_sync_offset    ),
  __ATTR(sync_size      , 0664, udmabuf_show_sync_size       , udmabuf_set_sync_size      ),
  __ATTR(sync_direction , 0664, udmabuf_show_sync_direction  , udmabuf_set_sync_direction ),
  __ATTR(sync_owner     , 0664, udmabuf_show_sync_owner      , NULL                       ),
  __ATTR(sync_for_cpu   , 0664, udmabuf_show_sync_for_cpu    , udmabuf_set_sync_for_cpu   ),
  __ATTR(sync_for_device, 0664, udmabuf_show_sync_for_device , udmabuf_set_sync_for_device),
#if (USE_DMA_COHERENT == 1)
  __ATTR(dma_coherent   , 0664, udmabuf_show_dma_coherent    , NULL                       ),
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_VMA_FAULT == 1))
  __ATTR(debug_vma      , 0664, udmabuf_show_debug_vma       , udmabuf_set_debug_vma      ),
#endif
  __ATTR_NULL,
};

#if (USE_DEV_GROUPS == 1)

#define udmabuf_device_attrs_size (sizeof(udmabuf_device_attrs)/sizeof(udmabuf_device_attrs[0]))

static struct attribute* udmabuf_attrs[udmabuf_device_attrs_size] = {
  NULL
};
static struct attribute_group udmabuf_attr_group = {
  .attrs = udmabuf_attrs
};
static const struct attribute_group* udmabuf_attr_groups[] = {
  &udmabuf_attr_group,
  NULL
};

static inline void udmabuf_sys_class_set_attributes(void)
{
    int i;
    for (i = 0 ; i < udmabuf_device_attrs_size-1 ; i++) {
        udmabuf_attrs[i] = &(udmabuf_device_attrs[i].attr);
    }
    udmabuf_attrs[i] = NULL;
    udmabuf_sys_class->dev_groups = udmabuf_attr_groups;
}
#else

static inline void udmabuf_sys_class_set_attributes(void)
{
    udmabuf_sys_class->dev_attrs  = udmabuf_device_attrs;
}

#endif

#if (USE_VMA_FAULT == 1)
/**
 * DOC: Udmabuf Device VM Area Operations
 *
 * This section defines the operation of vm when mmap-ed the udmabuf device file.
 *
 * * udmabuf_device_vma_open()  - udmabuf device vm area open operation.
 * * udmabuf_device_vma_close() - udmabuf device vm area close operation.
 * * udmabuf_device_vma_fault() - udmabuf device vm area fault operation.
 * * udmabuf_device_vm_ops      - udmabuf device vm operation table.
 */

/**
 * udmabuf_device_vma_open() - udmabuf device vm area open operation.
 * @vma:        Pointer to the vm area structure.
 * Return:	None
 */
static void udmabuf_device_vma_open(struct vm_area_struct* vma)
{
    struct udmabuf_device_data* this = vma->vm_private_data;
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->dma_dev, "vma_open(virt_addr=0x%lx, offset=0x%lx)\n", vma->vm_start, vma->vm_pgoff<<PAGE_SHIFT);
}

/**
 * udmabuf_device_vma_close() - udmabuf device vm area close operation.
 * @vma:        Pointer to the vm area structure.
 * Return:	None
 */
static void udmabuf_device_vma_close(struct vm_area_struct* vma)
{
    struct udmabuf_device_data* this = vma->vm_private_data;
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->dma_dev, "vma_close()\n");
}

/**
 * _udmabuf_device_vma_fault() - udmabuf device vm area fault operation.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      Success(=0) or error status(<0).
 */
static inline int _udmabuf_device_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
{
    struct udmabuf_device_data* this = vma->vm_private_data;
    unsigned long offset             = vmf->pgoff << PAGE_SHIFT;
    unsigned long phys_addr          = this->phys_addr + offset;
    unsigned long page_frame_num     = phys_addr  >> PAGE_SHIFT;
    unsigned long request_size       = 1          << PAGE_SHIFT;
    unsigned long available_size     = this->alloc_size -offset;
    unsigned long virt_addr;

#if (LINUX_VERSION_CODE >= 0x040A00)
    virt_addr = vmf->address;
#else
    virt_addr = (unsigned long)vmf->virtual_address;
#endif
    
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->dma_dev,
                 "vma_fault(virt_addr=%pad, phys_addr=%pad)\n", &virt_addr, &phys_addr
        );

    if (request_size > available_size) 
        return VM_FAULT_SIGBUS;

    if (!pfn_valid(page_frame_num))
        return VM_FAULT_SIGBUS;

#if (LINUX_VERSION_CODE >= 0x041200)
    return vmf_insert_pfn(vma, virt_addr, page_frame_num);
#else
    {
        int err = vm_insert_pfn(vma, virt_addr, page_frame_num);
	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err < 0 && err != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
    }
#endif
}

#if (LINUX_VERSION_CODE >= 0x040B00)
/**
 * udmabuf_device_vma_fault() - udmabuf device vm area fault operation.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_device_vma_fault(struct vm_fault* vmf)
{
    return _udmabuf_device_vma_fault(vmf->vma, vmf);
}
#else
/**
 * udmabuf_device_vma_fault() - udmabuf device vm area fault operation.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_device_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
{
    return _udmabuf_device_vma_fault(vma, vmf);
}
#endif

/**
 * udmabuf device vm operation table.
 */
static const struct vm_operations_struct udmabuf_device_vm_ops = {
    .open    = udmabuf_device_vma_open ,
    .close   = udmabuf_device_vma_close,
    .fault   = udmabuf_device_vma_fault,
};

#endif /* #if (USE_VMA_FAULT == 1) */

/**
 * DOC: Udmabuf Device File Operations
 *
 * This section defines the operation of the udmabuf device file.
 *
 * * udmabuf_device_file_open()    - udmabuf device file open operation.
 * * udmabuf_device_file_release() - udmabuf device file release operation.
 * * udmabuf_device_file_mmap()    - udmabuf device file memory map operation.
 * * udmabuf_device_file_read()    - udmabuf device file read operation.
 * * udmabuf_device_file_write()   - udmabuf device file write operation.
 * * udmabuf_device_file_llseek()  - udmabuf device file llseek operation.
 * * udmabuf_device_file_ops       - udmabuf device file operation table.
 */

/**
 * udmabuf_device_file_open() - udmabuf device file open operation.
 * @inode:	Pointer to the inode structure of this device.
 * @file:	Pointer to the file structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_device_file_open(struct inode *inode, struct file *file)
{
    struct udmabuf_device_data* this;
    int status = 0;

    this = container_of(inode->i_cdev, struct udmabuf_device_data, cdev);
    file->private_data = this;
    this->is_open = 1;

    return status;
}

/**
 * udmabuf_device_file_release() - udmabuf device file release operation.
 * @inode:	Pointer to the inode structure of this device.
 * @file:	Pointer to the file structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_device_file_release(struct inode *inode, struct file *file)
{
    struct udmabuf_device_data* this = file->private_data;

    this->is_open = 0;

    return 0;
}

/**
 * _PGPROT_NONCACHED    : vm_page_prot value when ((sync_mode & SYNC_MODE_MASK) == SYNC_MODE_NONCACHED   )
 * _PGPROT_WRITECOMBINE : vm_page_prot value when ((sync_mode & SYNC_MODE_MASK) == SYNC_MODE_WRITECOMBINE)
 * _PGPROT_DMACOHERENT  : vm_page_prot value when ((sync_mode & SYNC_MODE_MASK) == SYNC_MODE_DMACOHERENT )
 */
#if     defined(CONFIG_ARM)
#define _PGPROT_NONCACHED(vm_page_prot)    pgprot_noncached(vm_page_prot)
#define _PGPROT_WRITECOMBINE(vm_page_prot) pgprot_writecombine(vm_page_prot)
#define _PGPROT_DMACOHERENT(vm_page_prot)  pgprot_dmacoherent(vm_page_prot)
#elif   defined(CONFIG_ARM64)
#define _PGPROT_NONCACHED(vm_page_prot)    pgprot_writecombine(vm_page_prot)
#define _PGPROT_WRITECOMBINE(vm_page_prot) pgprot_writecombine(vm_page_prot)
#define _PGPROT_DMACOHERENT(vm_page_prot)  pgprot_writecombine(vm_page_prot)
#else
#define _PGPROT_NONCACHED(vm_page_prot)    pgprot_noncached(vm_page_prot)
#define _PGPROT_WRITECOMBINE(vm_page_prot) pgprot_writecombine(vm_page_prot)
#define _PGPROT_DMACOHERENT(vm_page_prot)  pgprot_writecombine(vm_page_prot)
#endif

/**
 * udmabuf_device_file_mmap() - udmabuf device file memory map operation.
 * @file:	Pointer to the file structure.
 * @vma:        Pointer to the vm area structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_device_file_mmap(struct file *file, struct vm_area_struct* vma)
{
    struct udmabuf_device_data* this = file->private_data;

    if (vma->vm_pgoff + vma_pages(vma) > (this->alloc_size >> PAGE_SHIFT))
        return -EINVAL;

    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS)) {
        switch (this->sync_mode & SYNC_MODE_MASK) {
            case SYNC_MODE_NONCACHED : 
                vma->vm_flags    |= VM_IO;
                vma->vm_page_prot = _PGPROT_NONCACHED(vma->vm_page_prot);
                break;
            case SYNC_MODE_WRITECOMBINE : 
                vma->vm_flags    |= VM_IO;
                vma->vm_page_prot = _PGPROT_WRITECOMBINE(vma->vm_page_prot);
                break;
            case SYNC_MODE_DMACOHERENT :
                vma->vm_flags    |= VM_IO;
                vma->vm_page_prot = _PGPROT_DMACOHERENT(vma->vm_page_prot);
                break;
            default :
                break;
        }
    }
    vma->vm_private_data = this;

#if (USE_VMA_FAULT == 1)
    {
        unsigned long page_frame_num = (this->phys_addr >> PAGE_SHIFT) + vma->vm_pgoff;
        if (pfn_valid(page_frame_num)) {
            vma->vm_flags |= VM_PFNMAP;
            vma->vm_ops    = &udmabuf_device_vm_ops;
            udmabuf_device_vma_open(vma);
            return 0;
        }
    }
#endif

    return dma_mmap_coherent(this->dma_dev, vma, this->virt_addr, this->phys_addr, this->alloc_size);
}

/**
 * udmabuf_device_file_read() - udmabuf device file read operation.
 * @file:	Pointer to the file structure.
 * @buff:	Pointer to the user buffer.
 * @count:	The number of bytes to be read.
 * @ppos:	Pointer to the offset value.
 * Return:	Transferd size.
 */
static ssize_t udmabuf_device_file_read(struct file* file, char __user* buff, size_t count, loff_t* ppos)
{
    struct udmabuf_device_data* this      = file->private_data;
    int                         result    = 0;
    size_t                      xfer_size;
    size_t                      remain_size;
    dma_addr_t                  phys_addr;
    void*                       virt_addr;

    if (mutex_lock_interruptible(&this->sem))
        return -ERESTARTSYS;

    if (*ppos >= this->size) {
        result = 0;
        goto return_unlock;
    }

    phys_addr = this->phys_addr + *ppos;
    virt_addr = this->virt_addr + *ppos;
    xfer_size = (*ppos + count >= this->size) ? this->size - *ppos : count;

    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS))
        dma_sync_single_for_cpu(this->dma_dev, phys_addr, xfer_size, DMA_FROM_DEVICE);

    if ((remain_size = copy_to_user(buff, virt_addr, xfer_size)) != 0) {
        result = 0;
        goto return_unlock;
    }

    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS))
        dma_sync_single_for_device(this->dma_dev, phys_addr, xfer_size, DMA_FROM_DEVICE);

    *ppos += xfer_size;
    result = xfer_size;
 return_unlock:
    mutex_unlock(&this->sem);
    return result;
}

/**
 * udmabuf_device_file_write() - udmabuf device file write operation.
 * @file:	Pointer to the file structure.
 * @buff:	Pointer to the user buffer.
 * @count:	The number of bytes to be written.
 * @ppos:	Pointer to the offset value
 * Return:	Transferd size.
 */
static ssize_t udmabuf_device_file_write(struct file* file, const char __user* buff, size_t count, loff_t* ppos)
{
    struct udmabuf_device_data* this      = file->private_data;
    int                         result    = 0;
    size_t                      xfer_size;
    size_t                      remain_size;
    dma_addr_t                  phys_addr;
    void*                       virt_addr;

    if (mutex_lock_interruptible(&this->sem))
        return -ERESTARTSYS;

    if (*ppos >= this->size) {
        result = 0;
        goto return_unlock;
    }

    phys_addr = this->phys_addr + *ppos;
    virt_addr = this->virt_addr + *ppos;
    xfer_size = (*ppos + count >= this->size) ? this->size - *ppos : count;

    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS))
        dma_sync_single_for_cpu(this->dma_dev, phys_addr, xfer_size, DMA_TO_DEVICE);

    if ((remain_size = copy_from_user(virt_addr, buff, xfer_size)) != 0) {
        result = 0;
        goto return_unlock;
    }

    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS))
        dma_sync_single_for_device(this->dma_dev, phys_addr, xfer_size, DMA_TO_DEVICE);

    *ppos += xfer_size;
    result = xfer_size;
 return_unlock:
    mutex_unlock(&this->sem);
    return result;
}

/**
 * udmabuf_device_file_llseek() - udmabuf device file llseek operation.
 * @file:	Pointer to the file structure.
 * @offset:	File offset to seek.
 * @whence:	Type of seek.
 * Return:	The new position.
 */
static loff_t udmabuf_device_file_llseek(struct file* file, loff_t offset, int whence)
{
    struct udmabuf_device_data* this = file->private_data;
    loff_t                      new_pos;

    switch (whence) {
        case 0 : /* SEEK_SET */
            new_pos = offset;
            break;
        case 1 : /* SEEK_CUR */
            new_pos = file->f_pos + offset;
            break;
        case 2 : /* SEEK_END */
            new_pos = this->size  + offset;
            break;
        default:
            return -EINVAL;
    }
    if (new_pos < 0         ){return -EINVAL;}
    if (new_pos > this->size){return -EINVAL;}
    file->f_pos = new_pos;
    return new_pos;
}

/**
 * udmabuf device file operation table.
 */
static const struct file_operations udmabuf_device_file_ops = {
    .owner   = THIS_MODULE,
    .open    = udmabuf_device_file_open,
    .release = udmabuf_device_file_release,
    .mmap    = udmabuf_device_file_mmap,
    .read    = udmabuf_device_file_read,
    .write   = udmabuf_device_file_write,
    .llseek  = udmabuf_device_file_llseek,
};

/**
 * DOC: Udmabuf Device Data Operations
 *
 * This section defines the operation of udmabuf device data.
 *
 * * udmabuf_device_ida         - Udmabuf Device Minor Number allocator variable.
 * * udmabuf_device_number      - Udmabuf Device Major Number.
 * * udmabuf_device_create()    - Create udmabuf device data.
 * * udmabuf_device_setup()     - Setup the udmabuf device data.
 * * udmabuf_device_info()      - Print infomation the udmabuf device data.
 * * udmabuf_device_destroy()   - Destroy the udmabuf device data.
 */
static DEFINE_IDA(udmabuf_device_ida);
static dev_t      udmabuf_device_number = 0;

/**
 * udmabuf_device_create() -  Create udmabuf device data.
 * @name:       device name   or NULL.
 * @parent:     parent device or NULL.
 * @minor:      minor_number  or -1.
 * Return:      Pointer to the udmabuf device data or NULL.
 */
static struct udmabuf_device_data* udmabuf_device_create(const char* name, struct device* parent, int minor)
{
    struct udmabuf_device_data* this     = NULL;
    unsigned int                done     = 0;
    const unsigned int          DONE_ALLOC_MINOR   = (1 << 0);
    const unsigned int          DONE_CHRDEV_ADD    = (1 << 1);
    const unsigned int          DONE_DEVICE_CREATE = (1 << 3);
    /*
     * allocate device minor number
     */
    {
        if ((0 <= minor) && (minor < DEVICE_MAX_NUM)) {
            if (ida_simple_get(&udmabuf_device_ida, minor, minor+1, GFP_KERNEL) < 0) {
                printk(KERN_ERR "couldn't allocate minor number(=%d).\n", minor);
                goto failed;
            }
        } else if(minor == -1) {
            if ((minor = ida_simple_get(&udmabuf_device_ida, 0, DEVICE_MAX_NUM, GFP_KERNEL)) < 0) {
                printk(KERN_ERR "couldn't allocate new minor number. return=%d.\n", minor);
                goto failed;
            }
        } else {
                printk(KERN_ERR "invalid minor number(=%d), valid range is 0 to %d\n", minor, DEVICE_MAX_NUM-1);
                goto failed;
        }
        done |= DONE_ALLOC_MINOR;
    }
    /*
     * create (udmabuf_device_data*) this.
     */
    {
        this = kzalloc(sizeof(*this), GFP_KERNEL);
        if (IS_ERR_OR_NULL(this)) {
            int retval = PTR_ERR(this);
            this = NULL;
            printk(KERN_ERR "kzalloc() failed. return=%d\n", retval);
            goto failed;
        }
    }
    /*
     * set device_number
     */
    {
        this->device_number = MKDEV(MAJOR(udmabuf_device_number), minor);
    }
    /*
     * register /sys/class/udmabuf/<name>
     */
    {
        if (name == NULL) {
            this->sys_dev = device_create(udmabuf_sys_class,
                                          parent,
                                          this->device_number,
                                          (void *)this,
                                          DEVICE_NAME_FORMAT, MINOR(this->device_number));
        } else {
            this->sys_dev = device_create(udmabuf_sys_class,
                                          parent,
                                          this->device_number,
                                          (void *)this,
                                         "%s", name);
        }
        if (IS_ERR_OR_NULL(this->sys_dev)) {
            int retval = PTR_ERR(this->sys_dev);
            this->sys_dev = NULL;
            printk(KERN_ERR "device_create() failed. return=%d\n", retval);
            goto failed;
        }
        done |= DONE_DEVICE_CREATE;
    }
    /*
     * add chrdev.
     */
    {
        int retval;
        cdev_init(&this->cdev, &udmabuf_device_file_ops);
        this->cdev.owner = THIS_MODULE;
        if ((retval = cdev_add(&this->cdev, this->device_number, 1)) != 0) {
            printk(KERN_ERR "cdev_add() failed. return=%d\n", retval);
            goto failed;
        }
        done |= DONE_CHRDEV_ADD;
    }
    /*
     * set dma_dev
     */
    if (parent != NULL)
        this->dma_dev = parent;
    else
        this->dma_dev = this->sys_dev;
    /*
     * initialize other variables.
     */
    {
        this->size            = 0;
        this->alloc_size      = 0;
        this->sync_mode       = SYNC_MODE_NONCACHED;
        this->sync_offset     = 0;
        this->sync_size       = 0;
        this->sync_direction  = 0;
        this->sync_owner      = 0;
        this->sync_for_cpu    = 0;
        this->sync_for_device = 0;
    }
#if (USE_OF_RESERVED_MEM == 1)
    {
        this->of_reserved_mem = 0;
    }
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_VMA_FAULT == 1))
    {
        this->debug_vma       = 0;
    }
#endif
    mutex_init(&this->sem);

    return this;

 failed:
    if (done & DONE_CHRDEV_ADD   ) { cdev_del(&this->cdev); }
    if (done & DONE_DEVICE_CREATE) { device_destroy(udmabuf_sys_class, this->device_number);}
    if (done & DONE_ALLOC_MINOR  ) { ida_simple_remove(&udmabuf_device_ida, minor);}
    if (this != NULL)              { kfree(this); }
    return NULL;
}

/**
 * udmabuf_device_setup() - Setup the udmabuf device data.
 * @this:       Pointer to the udmabuf device data.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_device_setup(struct udmabuf_device_data* this)
{
    if (!this)
        return -ENODEV;
    /*
     * set this->dma_dev->dma_mask
     */
    if (this->dma_dev->dma_mask == NULL) {
        this->dma_dev->dma_mask = &this->dma_dev->coherent_dma_mask;
    }
    /*
     * set this->dma_dev->dma_mask
     */
    if (*this->dma_dev->dma_mask == 0) {
        if (dma_set_mask(this->dma_dev, DMA_BIT_MASK(dma_mask_bit)) == 0) {
           dma_set_coherent_mask(this->dma_dev, DMA_BIT_MASK(dma_mask_bit));
        } else {
            printk(KERN_WARNING "dma_set_mask(DMA_BIT_MASK(%d)) failed\n", dma_mask_bit);
            dma_set_mask(this->dma_dev, DMA_BIT_MASK(32));
            dma_set_coherent_mask(this->dma_dev, DMA_BIT_MASK(32));
       }
    }
    /*
     * setup buffer size and allocation size
     */
    this->alloc_size = ((this->size + ((1 << PAGE_SHIFT) - 1)) >> PAGE_SHIFT) << PAGE_SHIFT;
    /*
     * dma buffer allocation 
     */
    this->virt_addr  = dma_alloc_coherent(this->dma_dev, this->alloc_size, &this->phys_addr, GFP_KERNEL);
    if (IS_ERR_OR_NULL(this->virt_addr)) {
        int retval = PTR_ERR(this->virt_addr);
        printk(KERN_ERR "dma_alloc_coherent() failed. return(%d)\n", retval);
        this->virt_addr = NULL;
        return (retval == 0) ? -ENOMEM : retval;
    }
    return 0;
}

/**
 * udmabuf_device_info() - Print infomation the udmabuf device data structure.
 * @this:       Pointer to the udmabuf device data structure.
 */
static void udmabuf_device_info(struct udmabuf_device_data* this)
{
    dev_info(this->sys_dev, "driver version = %s\n"  , DRIVER_VERSION);
    dev_info(this->sys_dev, "major number   = %d\n"  , MAJOR(this->device_number));
    dev_info(this->sys_dev, "minor number   = %d\n"  , MINOR(this->device_number));
    dev_info(this->sys_dev, "phys address   = %pad\n", &this->phys_addr);
    dev_info(this->sys_dev, "buffer size    = %zu\n" , this->alloc_size);
#if (USE_DMA_COHERENT == 1)
    dev_info(this->sys_dev, "dma coherent   = %d\n"  , is_device_dma_coherent(this->dma_dev));
#endif
}

/**
 * udmabuf_device_destroy() -  Destroy the udmabuf device data.
 * @this:       Pointer to the udmabuf device data.
 * Return:      Success(=0) or error status(<0).
 *
 * Unregister the device after releasing the resources.
 */
static int udmabuf_device_destroy(struct udmabuf_device_data* this)
{
    if (!this)
        return -ENODEV;

    if (this->virt_addr != NULL) {
        dma_free_coherent(this->dma_dev, this->alloc_size, this->virt_addr, this->phys_addr);
        this->virt_addr = NULL;
    }
    cdev_del(&this->cdev);
    device_destroy(udmabuf_sys_class, this->device_number);
    ida_simple_remove(&udmabuf_device_ida, MINOR(this->device_number));
    kfree(this);
    return 0;
}

/**
 * DOC: Udmabuf Static Device
 *
 * This section defines the udmabuf device to be created with arguments when loaded
 * into ther kernel with insmod.
 *
 * * STATIC_DEVICE_NUM                  - udmabuf static device list size.
 * * struct udmabuf_static_device       - udmabuf static device structure.
 * * udmabuf_static_device_list         - list of udmabuf static device structure.
 * * udmabuf_static_device_create()     - Create udmabuf static device.
 * * udmabuf_static_device_remove()     - Remove the udmabuf static device.
 * * udmabuf_static_device_search()     - Search udmabuf static device from udmabuf_static_device_list.
 * * udmabuf_static_device_create_all() - Create udmabuf static device list.
 * * udmabuf_static_device_remove_all() - Remove udmabuf static device list.
 */

#define STATIC_DEVICE_NUM   8

/**
 * struct udmabuf_static_device - udmabuf static device structure.
 */
struct udmabuf_static_device {
    struct platform_device* pdev;
    unsigned int            size;
};

/**
 * udmabuf_static_device_list   - list of udmabuf static device structure.
 */ 
struct udmabuf_static_device udmabuf_static_device_list[STATIC_DEVICE_NUM] = {};

/**
 * udmabuf_static_device_create() - Create udmabuf static device.
 * @id:	        device id.
 * @size:       buffer size.
 */
static void udmabuf_static_device_create(int id, unsigned int size)
{
    struct platform_device* pdev;
    int                     retval = 0;

    if ((id < 0) || (id >= STATIC_DEVICE_NUM))
        return;
    
    if (size == 0) {
        udmabuf_static_device_list[id].pdev = NULL;
        udmabuf_static_device_list[id].size = 0;
        return;
    }

    pdev = platform_device_alloc(DRIVER_NAME, id);
    if (IS_ERR_OR_NULL(pdev)) {
        retval = PTR_ERR(pdev);
        pdev   = NULL;
        printk(KERN_ERR "platform_device_alloc(%s,%d) failed. return=%d\n", DRIVER_NAME, id, retval);
        goto failed;
    }

    retval = platform_device_add(pdev);
    if (retval != 0) {
        dev_err(&pdev->dev, "platform_device_add failed. return=%d\n", retval);
        goto failed;
    }

    udmabuf_static_device_list[id].pdev = pdev;
    udmabuf_static_device_list[id].size = size;
    return;

 failed:
    if (pdev != NULL) {
        platform_device_put(pdev);
    }
    udmabuf_static_device_list[id].pdev = NULL;
    udmabuf_static_device_list[id].size = 0;
    return;
}

/**
 * udmabuf_static_device_remove() - Remove the udmabuf static device.
 * @id:	        device id.
 */
static void udmabuf_static_device_remove(int id)
{
    if (udmabuf_static_device_list[id].pdev != NULL) {
        platform_device_del(udmabuf_static_device_list[id].pdev);
        platform_device_put(udmabuf_static_device_list[id].pdev);
        udmabuf_static_device_list[id].pdev = NULL;
        udmabuf_static_device_list[id].size = 0;
    }
}

/**
 * udmabuf_static_device_search() - Search udmabuf static device from udmabuf_static_device_list.
 * @pdev:	Handle to the platform device structure.
 * @pid:	Pointer to device id.
 * @psize:	Pointer to buffer size.
 * Return:      1 = found, 0 = not found
 */
static int udmabuf_static_device_search(struct platform_device *pdev, int* pid, unsigned int* psize)
{
    int id;
    int found = 0;

    for (id = 0; id < STATIC_DEVICE_NUM; id++) {
        if ((udmabuf_static_device_list[id].pdev != NULL) &&
            (udmabuf_static_device_list[id].pdev == pdev)) {
            *pid   = id;
            *psize = udmabuf_static_device_list[id].size;
            found  = 1;
            break;
        }
    }
    return found;
}

/**
 * udmabuf static device description.
 */
#define DEFINE_UDMABUF_STATIC_DEVICE_PARAM(__num)                        \
    static int       udmabuf ## __num = 0;                               \
    module_param(    udmabuf ## __num, int, S_IRUGO);                    \
    MODULE_PARM_DESC(udmabuf ## __num, DRIVER_NAME #__num " buffer size");

#define CALL_UDMABUF_STATIC_DEVICE_CREATE(__num) \
    udmabuf_static_device_create(__num, udmabuf ## __num);

DEFINE_UDMABUF_STATIC_DEVICE_PARAM(0);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(1);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(2);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(3);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(4);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(5);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(6);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(7);

/**
 * udmabuf_static_device_create_all() - Create udmabuf static device list.
 */
static void udmabuf_static_device_create_all(void)
{
    CALL_UDMABUF_STATIC_DEVICE_CREATE(0);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(1);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(2);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(3);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(4);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(5);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(6);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(7);
}

/**
 * udmabuf_static_device_remove_all() - Remove udmabuf static device list.
 */
static void udmabuf_static_device_remove_all(void)
{
    int id;
    for (id = 0; id < STATIC_DEVICE_NUM; id++) {
        udmabuf_static_device_remove(id);
    }
}


/**
 * DOC: Udmabuf Platform Driver
 *
 * This section defines the udmabuf platform driver.
 *
 * * udmabuf_platform_driver_probe()   - Probe call for the device.
 * * udmabuf_platform_driver_remove()  - Remove call for the device.
 * * udmabuf_of_match                  - Open Firmware Device Identifier Matching Table.
 * * udmabuf_platform_driver           - Platform Driver Structure.
 */

/**
 * udmabuf_platform_driver_cleanup()   - Clean Up udmabuf platform driver
 * @pdev:	handle to the platform device structure.
 * @devdata     Pointer to the udmabuf device data structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_platform_driver_cleanup(struct platform_device *pdev, struct udmabuf_device_data *devdata)
{
    int retval = 0;

    if (devdata != NULL) {
#if (USE_OF_RESERVED_MEM == 1)
        bool of_reserved_mem = devdata->of_reserved_mem;
#endif
        retval = udmabuf_device_destroy(devdata);
        dev_set_drvdata(&pdev->dev, NULL);
#if (USE_OF_RESERVED_MEM == 1)
        if (of_reserved_mem) {
            of_reserved_mem_device_release(&pdev->dev);
        }
#endif
    } else {
        retval = -ENODEV;
    }
    return retval;
}

/**
 * udmabuf_platform_driver_probe() -  Probe call for the device.
 * @pdev:	handle to the platform device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * It does all the memory allocation and registration for the device.
 */
static int udmabuf_platform_driver_probe(struct platform_device *pdev)
{
    int                         retval       = 0;
    int                         of_status    = 0;
    unsigned int                of_u32_value = 0;
    unsigned int                size         = 0;
    int                         minor_number = -1;
    struct udmabuf_device_data* device_data  = NULL;
    const char*                 device_name  = NULL;

    dev_dbg(&pdev->dev, "driver probe start.\n");

    if (udmabuf_static_device_search(pdev, &minor_number, &size) == 0) {

        of_status = of_property_read_u32(pdev->dev.of_node, "size", &size);
        if (of_status != 0) {
            dev_err(&pdev->dev, "invalid property size. status=%d\n", of_status);
            retval = -ENODEV;
            goto failed;
        }

        of_status = of_property_read_u32(pdev->dev.of_node, "minor-number", &of_u32_value);
        minor_number = (of_status == 0) ? of_u32_value : -1;

        device_name = of_get_property(pdev->dev.of_node, "device-name", NULL);
        if (IS_ERR_OR_NULL(device_name)) {
            if (minor_number < 0)
                device_name = dev_name(&pdev->dev);
            else
                device_name = NULL;
        }
    }
    /*
     * udmabuf_device_create()
     */
    device_data = udmabuf_device_create(device_name, &pdev->dev, minor_number);
    if (IS_ERR_OR_NULL(device_data)) {
        retval = PTR_ERR(device_data);
        dev_err(&pdev->dev, "driver create failed. return=%d.\n", retval);
        device_data = NULL;
        retval = (retval == 0) ? -EINVAL : retval;
        goto failed;
    }
    dev_set_drvdata(&pdev->dev, device_data);
    /*
     * set size
     */
    device_data->size = size;
    /*
     * of_reserved_mem_device_init()
     */
#if (USE_OF_RESERVED_MEM == 1)
    if (pdev->dev.of_node != NULL) {
        retval = of_reserved_mem_device_init(&pdev->dev);
        if (retval == 0) {
            device_data->of_reserved_mem = 1;
        } else if (retval != -ENODEV) {
            dev_err(&pdev->dev, "of_reserved_mem_device_init failed. return=%d\n", retval);
            goto failed;
        }
    }
#endif
#if (USE_OF_DMA_CONFIG == 1)
    /*
     * of_dma_configure()
     * - set pdev->dev->dma_mask
     * - set pdev->dev->coherent_dma_mask
     * - call of_dma_is_coherent()
     * - call arch_setup_dma_ops()
     */
#if (USE_OF_RESERVED_MEM == 1)
    /* If "memory-region" property is spsecified, of_dma_configure() will not be executed.
     * Because in that case, it is already executed in of_reserved_mem_device_init().
     */
    if (device_data->of_reserved_mem == 0)
#endif
    {
#if (LINUX_VERSION_CODE >= 0x040C00)
#if (LINUX_VERSION_CODE >= 0x041200)
        retval = of_dma_configure(&pdev->dev, pdev->dev.of_node, true);
#else
        retval = of_dma_configure(&pdev->dev, pdev->dev.of_node);
#endif
        if (retval != 0) {
            dev_err(&pdev->dev, "of_dma_configure failed. return=%d\n", retval);
            goto failed;
        }
#else
        of_dma_configure(&pdev->dev, pdev->dev.of_node);
#endif
    }
#endif
    /*
     * sync-mode property
     */
    if (of_property_read_u32(pdev->dev.of_node, "sync-mode", &of_u32_value) == 0) {
        if ((of_u32_value < SYNC_MODE_MIN) || (of_u32_value > SYNC_MODE_MAX)) {
            dev_err(&pdev->dev, "invalid sync-mode property value=%d\n", of_u32_value);
            goto failed;
        }
        device_data->sync_mode &= ~SYNC_MODE_MASK;
        device_data->sync_mode |= (int)of_u32_value;
    }
    /*
     * sync-always property
     */
    if (of_property_read_bool(pdev->dev.of_node, "sync-always")) {
        device_data->sync_mode |= SYNC_ALWAYS;
    }
    /*
     * sync-direction property
     */
    if (of_property_read_u32(pdev->dev.of_node, "sync-direction", &of_u32_value) == 0) {
        if (of_u32_value > 2) {
            dev_err(&pdev->dev, "invalid sync-direction property value=%d\n", of_u32_value);
            goto failed;
        }
        device_data->sync_direction = (int)of_u32_value;
    }
    /*
     * sync-offset property
     */
    if (of_property_read_u32(pdev->dev.of_node, "sync-offset", &of_u32_value) == 0) {
        if (of_u32_value >= device_data->size) {
            dev_err(&pdev->dev, "invalid sync-offset property value=%d\n", of_u32_value);
            goto failed;
        }
        device_data->sync_offset = (int)of_u32_value;
    }
    /*
     * sync-size property
     */
    if (of_property_read_u32(pdev->dev.of_node, "sync-size", &of_u32_value) == 0) {
        if (device_data->sync_offset + of_u32_value > device_data->size) {
            dev_err(&pdev->dev, "invalid sync-size property value=%d\n", of_u32_value);
            goto failed;
        }
        device_data->sync_size = (size_t)of_u32_value;
    } else {
        device_data->sync_size = device_data->size;
    }
    /*
     * udmabuf_device_setup()
     */
    retval = udmabuf_device_setup(device_data);
    if (retval) {
        dev_err(&pdev->dev, "driver setup failed. return=%d\n", retval);
        goto failed;
    }
    
    if (info_enable) {
        udmabuf_device_info(device_data);
        dev_info(&pdev->dev, "driver installed.\n");
    }
    return 0;

failed:
    udmabuf_platform_driver_cleanup(pdev, device_data);

    return retval;
}

/**
 * udmabuf_platform_driver_remove() -  Remove call for the device.
 * @pdev:	Handle to the platform device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * Unregister the device after releasing the resources.
 */
static int udmabuf_platform_driver_remove(struct platform_device *pdev)
{
    struct udmabuf_device_data* this   = dev_get_drvdata(&pdev->dev);
    int                         retval = 0;

    dev_dbg(&pdev->dev, "driver remove start.\n");

    retval = udmabuf_platform_driver_cleanup(pdev, this);

    if (info_enable) {
        dev_info(&pdev->dev, "driver removed.\n");
    }
    return retval;
}

/**
 * Open Firmware Device Identifier Matching Table
 */
static struct of_device_id udmabuf_of_match[] = {
    { .compatible = "ikwzm,udmabuf-0.10.a", },
    { /* end of table */}
};
MODULE_DEVICE_TABLE(of, udmabuf_of_match);

/**
 * Platform Driver Structure
 */
static struct platform_driver udmabuf_platform_driver = {
    .probe  = udmabuf_platform_driver_probe,
    .remove = udmabuf_platform_driver_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name  = DRIVER_NAME,
        .of_match_table = udmabuf_of_match,
    },
};

/**
 * DOC: Udmabuf Module Operations
 *
 * * udmabuf_module_cleanup()
 * * udmabuf_module_init()
 * * udmabuf_module_exit()
 */

static bool udmabuf_platform_driver_registerd = 0;

/**
 * udmabuf_module_cleanup()
 */
static void udmabuf_module_cleanup(void)
{
    udmabuf_static_device_remove_all();
    if (udmabuf_platform_driver_registerd){platform_driver_unregister(&udmabuf_platform_driver);}
    if (udmabuf_sys_class     != NULL    ){class_destroy(udmabuf_sys_class);}
    if (udmabuf_device_number != 0       ){unregister_chrdev_region(udmabuf_device_number, 0);}
    ida_destroy(&udmabuf_device_ida);
}

/**
 * udmabuf_module_init()
 */
static int __init udmabuf_module_init(void)
{
    int retval = 0;

    ida_init(&udmabuf_device_ida);
      
    retval = alloc_chrdev_region(&udmabuf_device_number, 0, 0, DRIVER_NAME);
    if (retval != 0) {
        printk(KERN_ERR "%s: couldn't allocate device major number. return=%d\n", DRIVER_NAME, retval);
        udmabuf_device_number = 0;
        goto failed;
    }

    udmabuf_sys_class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR_OR_NULL(udmabuf_sys_class)) {
        retval = PTR_ERR(udmabuf_sys_class);
        udmabuf_sys_class = NULL;
        printk(KERN_ERR "%s: couldn't create sys class. return=%d\n", DRIVER_NAME, retval);
        retval = (retval == 0) ? -ENOMEM : retval;
        goto failed;
    }

    udmabuf_sys_class_set_attributes();

    udmabuf_static_device_create_all();

    retval = platform_driver_register(&udmabuf_platform_driver);
    if (retval) {
        printk(KERN_ERR "%s: couldn't register platform driver. return=%d\n", DRIVER_NAME, retval);
        udmabuf_platform_driver_registerd = 0;
        goto failed;
    } else {
        udmabuf_platform_driver_registerd = 1;
    }

    return 0;

 failed:
    udmabuf_module_cleanup();
    return retval;
}

/**
 * udmabuf_module_exit()
 */
static void __exit udmabuf_module_exit(void)
{
    udmabuf_module_cleanup();
}

module_init(udmabuf_module_init);
module_exit(udmabuf_module_exit);
