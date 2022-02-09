/*********************************************************************************
 *
 *       Copyright (C) 2015-2021 Ichiro Kawazome
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

#define DRIVER_VERSION     "3.2.5"
#define DRIVER_NAME        "u-dma-buf"
#define DEVICE_NAME_FORMAT "udmabuf%d"
#define DEVICE_MAX_NUM      256
#define UDMABUF_DEBUG       1
#define USE_VMA_FAULT       1
#define UDMABUF_MGR_ENABLE  1
#define UDMABUF_MGR_NAME   "u-dma-buf-mgr"

#if     ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)) && (defined(CONFIG_ARM) || defined(CONFIG_ARM64)))
#if     (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#if     (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <linux/dma-map-ops.h>
#else
#include <linux/dma-noncoherent.h>
#endif
#define IS_DMA_COHERENT(dev) dev_is_dma_coherent(dev)
#else
#define IS_DMA_COHERENT(dev) is_device_dma_coherent(dev)
#endif
#endif

#if     (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
#define USE_DEV_GROUPS      1
#else
#define USE_DEV_GROUPS      0
#endif

#if     ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) && defined(CONFIG_OF))
#define USE_OF_RESERVED_MEM 1
#else
#define USE_OF_RESERVED_MEM 0
#endif

#if     ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) && defined(CONFIG_OF))
#define USE_OF_DMA_CONFIG   1
#else
#define USE_OF_DMA_CONFIG   0
#endif

#if     (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
#define USE_DEV_PROPERTY    1
#else
#define USE_DEV_PROPERTY    0
#endif

#if     (UDMABUF_DEBUG == 1)
#define UDMABUF_DEBUG_CHECK(this,debug) (this->debug)
#else
#define UDMABUF_DEBUG_CHECK(this,debug) (0)
#endif

#if     (USE_OF_RESERVED_MEM == 1)
#include <linux/of_reserved_mem.h>
#endif

#ifndef U64_MAX
#define U64_MAX ((u64)~0ULL)
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
#define           DMA_INFO_ENABLE  (info_enable & 0x02)

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
    size_t               size;
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
DEF_ATTR_SHOW(size           , "%zu\n"   , this->size                                     );
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
#if defined(IS_DMA_COHERENT)
DEF_ATTR_SHOW(dma_coherent   , "%d\n"    , IS_DMA_COHERENT(this->dma_dev)                 );
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
  __ATTR(sync_owner     , 0444, udmabuf_show_sync_owner      , NULL                       ),
  __ATTR(sync_for_cpu   , 0664, udmabuf_show_sync_for_cpu    , udmabuf_set_sync_for_cpu   ),
  __ATTR(sync_for_device, 0664, udmabuf_show_sync_for_device , udmabuf_set_sync_for_device),
#if defined(IS_DMA_COHERENT)
  __ATTR(dma_coherent   , 0444, udmabuf_show_dma_coherent    , NULL                       ),
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
 * Return:      None
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
 * Return:      None
 */
static void udmabuf_device_vma_close(struct vm_area_struct* vma)
{
    struct udmabuf_device_data* this = vma->vm_private_data;
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->dma_dev, "vma_close()\n");
}

/**
 * VM_FAULT_RETURN_TYPE - Type of udmabuf_device_vma_fault() return value.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0))
typedef vm_fault_t VM_FAULT_RETURN_TYPE;
#else
typedef int        VM_FAULT_RETURN_TYPE;
#endif

/**
 * _udmabuf_device_vma_fault() - udmabuf device vm area fault operation.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      VM_FAULT_RETURN_TYPE (Success(=0) or error status(!=0)).
 */
static inline VM_FAULT_RETURN_TYPE _udmabuf_device_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
{
    struct udmabuf_device_data* this = vma->vm_private_data;
    unsigned long offset             = vmf->pgoff << PAGE_SHIFT;
    unsigned long phys_addr          = this->phys_addr + offset;
    unsigned long page_frame_num     = phys_addr  >> PAGE_SHIFT;
    unsigned long request_size       = 1          << PAGE_SHIFT;
    unsigned long available_size     = this->alloc_size -offset;
    unsigned long virt_addr;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0))
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
/**
 * udmabuf_device_vma_fault() - udmabuf device vm area fault operation.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      VM_FAULT_RETURN_TYPE (Success(=0) or error status(!=0)).
 */
static VM_FAULT_RETURN_TYPE udmabuf_device_vma_fault(struct vm_fault* vmf)
{
    return _udmabuf_device_vma_fault(vmf->vma, vmf);
}
#else
/**
 * udmabuf_device_vma_fault() - udmabuf device vm area fault operation.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      VM_FAULT_RETURN_TYPE (Success(=0) or error status(!=0)).
 */
static VM_FAULT_RETURN_TYPE udmabuf_device_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
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
 * @inode:      Pointer to the inode structure of this device.
 * @file:       to the file structure.
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
 * @inode:      Pointer to the inode structure of this device.
 * @file:       Pointer to the file structure.
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
#define _PGPROT_NONCACHED(vm_page_prot)    pgprot_noncached(vm_page_prot)
#define _PGPROT_WRITECOMBINE(vm_page_prot) pgprot_writecombine(vm_page_prot)
#define _PGPROT_DMACOHERENT(vm_page_prot)  pgprot_writecombine(vm_page_prot)
#else
#define _PGPROT_NONCACHED(vm_page_prot)    pgprot_noncached(vm_page_prot)
#define _PGPROT_WRITECOMBINE(vm_page_prot) pgprot_writecombine(vm_page_prot)
#define _PGPROT_DMACOHERENT(vm_page_prot)  pgprot_writecombine(vm_page_prot)
#endif

/**
 * udmabuf_device_file_mmap() - udmabuf device file memory map operation.
 * @file:       Pointer to the file structure.
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
 * @file:       Pointer to the file structure.
 * @buff:       Pointer to the user buffer.
 * @count:      The number of bytes to be read.
 * @ppos:       Pointer to the offset value.
 * Return:      Transferd size.
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
 * @file:       Pointer to the file structure.
 * @buff:       Pointer to the user buffer.
 * @count:      The number of bytes to be written.
 * @ppos:       Pointer to the offset value
 * Return:      Transferd size.
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
 * @file:       Pointer to the file structure.
 * @offset:     File offset to seek.
 * @whence:     Type of seek.
 * Return:      The new position.
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
 * @minor:      minor_number  or -1 or -2.
 * Return:      Pointer to the udmabuf device data or NULL.
 */
static struct udmabuf_device_data* udmabuf_device_create(const char* name, struct device* parent, int minor)
{
    struct udmabuf_device_data* this     = NULL;
    unsigned int                done     = 0;
    const unsigned int          DONE_ALLOC_MINOR   = (1 << 0);
    const unsigned int          DONE_CHRDEV_ADD    = (1 << 1);
    const unsigned int          DONE_DEVICE_CREATE = (1 << 3);
    const unsigned int          DONE_SET_DMA_DEV   = (1 << 4);
    /*
     * allocate device minor number
     */
    {
        if ((0 <= minor) && (minor < DEVICE_MAX_NUM)) {
            if (ida_simple_get(&udmabuf_device_ida, minor, minor+1, GFP_KERNEL) < 0) {
                printk(KERN_ERR "couldn't allocate minor number(=%d).\n", minor);
                goto failed;
            }
        } else if(minor < 0) {
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
    {
        if (parent != NULL)
            this->dma_dev = get_device(parent);
        else
            this->dma_dev = get_device(this->sys_dev);
        /*
         * set this->dma_dev->dma_mask
         */
        if (this->dma_dev->dma_mask == NULL) {
            this->dma_dev->dma_mask = &this->dma_dev->coherent_dma_mask;
        }
        /*
         * set *this->dma_dev->dma_mask and this->dma_dev->coherent_dma_mask
         * Executing dma_set_mask_and_coherent() before of_dma_configure() may fail.
         * Because dma_set_mask_and_coherent() will fail unless dev->dma_ops is set.
         * When dma_set_mask_and_coherent() fails, it is forcefuly setting the dma-mask value.
         */
        if (*this->dma_dev->dma_mask == 0) {
            int retval = dma_set_mask_and_coherent(this->dma_dev, DMA_BIT_MASK(dma_mask_bit));
            if (retval != 0) {
                printk(KERN_WARNING "dma_set_mask_and_coherent(DMA_BIT_MASK(%d)) failed. return=(%d)\n", dma_mask_bit, retval);
                *this->dma_dev->dma_mask         = DMA_BIT_MASK(dma_mask_bit);
                this->dma_dev->coherent_dma_mask = DMA_BIT_MASK(dma_mask_bit);
            }
        }
        done |= DONE_SET_DMA_DEV;
    }
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
    if (done & DONE_SET_DMA_DEV  ) { put_device(this->dma_dev);}
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
     * setup buffer size and allocation size
     */
    this->alloc_size = ((this->size + ((1 << PAGE_SHIFT) - 1)) >> PAGE_SHIFT) << PAGE_SHIFT;
    /*
     * dma buffer allocation 
     */
    this->virt_addr  = dma_alloc_coherent(this->dma_dev, this->alloc_size, &this->phys_addr, GFP_KERNEL);
    if (IS_ERR_OR_NULL(this->virt_addr)) {
        int retval = PTR_ERR(this->virt_addr);
        printk(KERN_ERR "dma_alloc_coherent(size=%zu) failed. return(%d)\n", this->alloc_size, retval);
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
    if (DMA_INFO_ENABLE) {
        dev_info(this->sys_dev, "dma device     = %s\n"       , dev_name(this->dma_dev));
#if defined(IS_DMA_COHERENT)
        dev_info(this->sys_dev, "dma coherent   = %d\n"       , IS_DMA_COHERENT(this->dma_dev));
#endif
        dev_info(this->sys_dev, "dma mask       = 0x%016llx\n", dma_get_mask(this->dma_dev));
    }
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
    put_device(this->dma_dev);
    cdev_del(&this->cdev);
    device_destroy(udmabuf_sys_class, this->device_number);
    ida_simple_remove(&udmabuf_device_ida, MINOR(this->device_number));
    kfree(this);
    return 0;
}

/**
 * DOC: Udmabuf Platform Device.
 *
 * This section defines the udmabuf platform device list.
 *
 * * struct udmabuf_platform_device       - udmabuf platform device structure.
 * * udmabuf_platform_device_list         - list of udmabuf platform device structure.
 * * udmabuf_platform_device_sem          - semaphore of udmabuf platform device list.
 * * udmabuf_platform_device_create()     - Create udmabuf platform device and add to list.
 * * udmabuf_platform_device_remove()     - Remove udmabuf platform device and delete from list.
 * * udmabuf_platform_device_remove_all() - Remove all udmabuf platform devices and clear list.
 * * udmabuf_platform_device_search()     - Search udmabuf platform device from list by name or number.
 * * udmabuf_get_device_name_property()   - Get "device-name"  property from udmabuf device.
 * * udmabuf_get_size_property()          - Get "buffer-size"  property from udmabuf device.
 * * udmabuf_get_minor_number_property()  - Get "minor-number" property from udmabuf device.
 */

#if (USE_DEV_PROPERTY != 0)
#include <linux/property.h>
#endif

/**
 * struct udmabuf_platform_device - udmabuf platform device structure.
 */
struct udmabuf_platform_device {
    struct device*       dev;
#if (USE_DEV_PROPERTY == 0)
    const char*          device_name;
    u32                  minor_number;
    u64                  buffer_size;
#endif
    struct list_head     list;
};

/**
 * udmabuf_platform_device_list   - list of udmabuf static device structure.
 * udmabuf_platform_device_sem    - semaphore of udmabuf platform device list.
 */
static struct list_head udmabuf_platform_device_list;
static struct mutex     udmabuf_platform_device_sem;

/**
 * udmabuf_get_device_name_property()  - Get "device-name"  property from udmabuf device.
 * @dev:        handle to the device structure.
 * @name:       address of device name.
 * @lock:       use mutex_lock()/mutex_unlock()
 * Return:      Success(=0) or error status(<0).
 */
static int  udmabuf_get_device_name_property(struct device *dev, const char** name, bool lock)
{
#if (USE_DEV_PROPERTY == 0)
    int                             status = -1;
    struct udmabuf_platform_device* plat;

    if (lock)
        mutex_lock(&udmabuf_platform_device_sem);
    list_for_each_entry(plat, &udmabuf_platform_device_list, list) {
        if (plat->dev == dev) {
            if (plat->device_name == NULL) {
                status = -1;
            } else {
                *name  = plat->device_name;
                status = 0;
            }
            break;
        }
    }
    if (lock)
        mutex_unlock(&udmabuf_platform_device_sem);
    return status;
#else
    return device_property_read_string(dev, "device-name", name);
#endif
}

/**
 * udmabuf_get_size_property()         - Get "buffer-size"  property from udmabuf device.
 * @dev:        handle to the device structure.
 * @value:      address of buffer size value.
 * @lock:       use mutex_lock()/mutex_unlock()
 * Return:      Success(=0) or error status(<0).
 */
static int  udmabuf_get_size_property(struct device *dev, u64* value, bool lock)
{
#if (USE_DEV_PROPERTY == 0)
    int                             status = -1;
    struct udmabuf_platform_device* plat;

    if (lock)
        mutex_lock(&udmabuf_platform_device_sem);
    list_for_each_entry(plat, &udmabuf_platform_device_list, list) {
        if (plat->dev == dev) {
            *value = plat->buffer_size;
            status = 0;
            break;
        }
    }
    if (lock)
        mutex_unlock(&udmabuf_platform_device_sem);
    return status;
#else
    return device_property_read_u64(dev, "size", value);
#endif
}

/**
 * udmabuf_get_minor_number_property() - Get "minor-number" property from udmabuf device.
 * @dev:        handle to the device structure.
 * @value:      address of minor number value.
 * @lock:       use mutex_lock()/mutex_unlock()
 * Return:      Success(=0) or error status(<0).
 */

static int  udmabuf_get_minor_number_property(struct device *dev, u32* value, bool lock)
{
#if (USE_DEV_PROPERTY == 0)
    int                             status = -1;
    struct udmabuf_platform_device* plat;

    if (lock)
        mutex_lock(&udmabuf_platform_device_sem);
    list_for_each_entry(plat, &udmabuf_platform_device_list, list) {
        if (plat->dev == dev) {
            *value = plat->minor_number;
            status = 0;
            break;
        }
    }
    if (lock) 
        mutex_unlock(&udmabuf_platform_device_sem);
    return status;
#else
    return device_property_read_u32(dev, "minor-number", value);
#endif
}

/**
 * udmabuf_platform_device_search()    - Search udmabuf platform device from list by name or number.
 * @name:       device name or NULL.
 * @id:         device id.
 * Return:      Pointer to the udmabuf_platform_device or NULL.
 */
static struct udmabuf_platform_device* udmabuf_platform_device_search(const char* name, int id)
{
    struct udmabuf_platform_device* plat;
    struct udmabuf_platform_device* found_plat = NULL;
    mutex_lock(&udmabuf_platform_device_sem);
    list_for_each_entry(plat, &udmabuf_platform_device_list, list) {
        bool found_by_name = true;
        bool found_by_id   = true;
        if (name != NULL) {
            const char* device_name;
            found_by_name = false;
            if (udmabuf_get_device_name_property(plat->dev, &device_name, false) == 0) 
                if (strcmp(name, device_name) == 0)
                    found_by_name = true;
        }
        if (id >= 0) {
            u32 minor_number;
            found_by_id = false;
            if (udmabuf_get_minor_number_property(plat->dev, &minor_number, false) == 0) 
                if (id == minor_number)
                    found_by_id = true;
        }
        if ((found_by_name == true) && (found_by_id == true))
            found_plat = plat;
    }
    mutex_unlock(&udmabuf_platform_device_sem);
    return found_plat;
}

/**
 * udmabuf_platform_device_create() - Create udmabuf platform device and add to list.
 * @name:       device name or NULL.
 * @id:         device id.
 * @size:       buffer size.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_platform_device_create(const char* name, int id, unsigned int size)
{
    struct platform_device*         pdev       = NULL;
    struct udmabuf_platform_device* plat       = NULL;
    int                             retval     = 0;
    bool                            list_added = false;

    if (size == 0)
        return -EINVAL;

    pdev = platform_device_alloc(DRIVER_NAME, id);
    if (IS_ERR_OR_NULL(pdev)) {
        retval = PTR_ERR(pdev);
        pdev   = NULL;
        printk(KERN_ERR "platform_device_alloc(%s,%d) failed. return=%d\n", DRIVER_NAME, id, retval);
        goto failed;
    }

    if (!pdev->dev.dma_mask)
        pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

    pdev->dev.coherent_dma_mask = DMA_BIT_MASK(dma_mask_bit);
    *pdev->dev.dma_mask         = DMA_BIT_MASK(dma_mask_bit);

    plat = kzalloc(sizeof(*plat), GFP_KERNEL);
    if (IS_ERR_OR_NULL(plat)) {
        retval = PTR_ERR(plat);
        plat   = NULL;
        dev_err(&pdev->dev, "kzalloc() failed. return=%d\n", retval);
        goto failed;
    }

#if (USE_DEV_PROPERTY == 0)
    {
        plat->device_name  = (name != NULL) ? kstrdup(name, GFP_KERNEL) : NULL;
        plat->minor_number = id;
        plat->buffer_size  = size;
    }
#else
    {
        struct property_entry   props_list[] = {
            PROPERTY_ENTRY_STRING("device-name" , name),
            PROPERTY_ENTRY_U64(   "size"        , size),
            PROPERTY_ENTRY_U32(   "minor-number", id  ),
            {},
        };
        struct property_entry* props = (name != NULL) ? &props_list[0] : &props_list[1];
#if     (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0))
        {
            retval = device_add_properties(&pdev->dev, props);
            if (retval != 0) {
                dev_err(&pdev->dev, "device_add_properties failed. return=%d\n", retval);
                goto failed;
            }
        }
#else
        {
            const struct property_set pset = {
                .properties = props,
            };
            retval = device_add_property_set(&pdev->dev, &pset);
            if (retval != 0) {
                dev_err(&pdev->dev, "device_add_propertiy_set failed. return=%d\n", retval);
                goto failed;
            }
        }
#endif
    }
#endif

    plat->dev  = &pdev->dev;
    mutex_lock(&udmabuf_platform_device_sem);
    list_add_tail(&plat->list, &udmabuf_platform_device_list);
    list_added = true;
    mutex_unlock(&udmabuf_platform_device_sem);
    
    retval = platform_device_add(pdev);
    if (retval != 0) {
        dev_err(&pdev->dev, "platform_device_add failed. return=%d\n", retval);
        goto failed;
    }

    return 0;

 failed:
    if (list_added == true) {
        mutex_lock(&udmabuf_platform_device_sem);
        list_del(&plat->list);
        mutex_unlock(&udmabuf_platform_device_sem);
    }
    if (pdev != NULL) {
        platform_device_put(pdev);
    }
    if (plat != NULL) {
#if (USE_DEV_PROPERTY == 0)
        if (plat->device_name != NULL)
            kfree(plat->device_name);
#endif
        kfree(plat);
    }
    return retval;
}

/**
 * udmabuf_platform_device_remove() - Remove udmabuf platform device and delete from list.
 * @plat:       udmabuf_platform_device*
 */
static void udmabuf_platform_device_remove(struct udmabuf_platform_device* plat)
{
    struct device*           dev  = plat->dev;
    struct platform_device*  pdev = to_platform_device(dev);
    platform_device_del(pdev);
    platform_device_put(pdev);
    mutex_lock(&udmabuf_platform_device_sem);
    list_del(&plat->list);
    mutex_unlock(&udmabuf_platform_device_sem);
#if (USE_DEV_PROPERTY == 0)
    if (plat->device_name != NULL)
        kfree(plat->device_name);
#endif
    kfree(plat);
}

/**
 * udmabuf_platform_device_remove_all() - Remove all udmabuf platform devices and clear list.
 */
static void udmabuf_platform_device_remove_all(void)
{
    while(!list_empty(&udmabuf_platform_device_list)) {
        struct udmabuf_platform_device* plat = list_first_entry(&udmabuf_platform_device_list, typeof(*(plat)), list);
        udmabuf_platform_device_remove(plat);
    }
}

/**
 * DOC: Udmabuf Static Devices.
 *
 * This section defines the udmabuf device to be created with arguments when loaded
 * into ther kernel with insmod.
 *
 */
#define DEFINE_UDMABUF_STATIC_DEVICE_PARAM(__num)                        \
    static int       udmabuf ## __num = 0;                               \
    module_param(    udmabuf ## __num, int, S_IRUGO);                    \
    MODULE_PARM_DESC(udmabuf ## __num, DRIVER_NAME #__num " buffer size");

#define CALL_UDMABUF_STATIC_DEVICE_CREATE(__num)                         \
    if (udmabuf ## __num != 0) {                                         \
        ida_simple_remove(&udmabuf_device_ida, __num);                   \
        udmabuf_platform_device_create(NULL, __num, udmabuf ## __num);   \
    }

#define CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(__num)           \
    if (udmabuf ## __num != 0) {                                         \
        ida_simple_get(&udmabuf_device_ida, __num, __num+1, GFP_KERNEL); \
    }

DEFINE_UDMABUF_STATIC_DEVICE_PARAM(0);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(1);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(2);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(3);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(4);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(5);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(6);
DEFINE_UDMABUF_STATIC_DEVICE_PARAM(7);

/**
 * udmabuf_static_device_reserve_minor_number_all() - Reserve udmabuf static device's minor-number.
 */
static void udmabuf_static_device_reserve_minor_number_all(void)
{
    CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(0);
    CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(1);
    CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(2);
    CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(3);
    CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(4);
    CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(5);
    CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(6);
    CALL_UDMABUF_STATIC_DEVICE_RESERVE_MINOR_NUMBER(7);
}

/**
 * udmabuf_static_device_create_all() - Create udmabuf static devices.
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
 * DOC: Udmabuf Device Driver probe/remove section.
 *
 * This section defines the udmabuf device driver.
 *
 * * udmabuf_device_probe()                      - Probe  call for the device driver.
 * * udmabuf_device_remove()                     - Remove call for the device driver.
 */

/**
 * udmabuf_device_remove()   - Remove udmabuf device driver.
 * @dev:        handle to the device structure.
 * @devdata     Pointer to the udmabuf device data structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_device_remove(struct device *dev, struct udmabuf_device_data *devdata)
{
    int retval = 0;

    if (devdata != NULL) {
#if (USE_OF_RESERVED_MEM == 1)
        bool of_reserved_mem = devdata->of_reserved_mem;
#endif
        retval = udmabuf_device_destroy(devdata);
        dev_set_drvdata(dev, NULL);
#if (USE_OF_RESERVED_MEM == 1)
        if (of_reserved_mem) {
            of_reserved_mem_device_release(dev);
        }
#endif
    } else {
        retval = -ENODEV;
    }
    return retval;
}

/**
 * of_property_read_ulong() -  Find and read a unsigned long intger from a property.
 * @node:       device node which the property value is to be read.
 * @propname:   name of property to be searched.
 * @out_value:  pointer to return value, modified only if return value is 0.
 * Return:      Success(=0) or error status(<0).
 */
static int of_property_read_ulong(const struct device_node* node, const char* propname, u64* out_value)
{
    u32    u32_value;
    u64    u64_value;
    int    retval;

    if ((retval = of_property_read_u64(node, propname, &u64_value)) == 0) {
        *out_value = u64_value;
        return 0;
    }
      
    if ((retval = of_property_read_u32(node, propname, &u32_value)) == 0) {
        *out_value = (u64)u32_value;
        return 0;
    }
      
    return retval;
}

/**
 * udmabuf_device_probe() -  Probe call for the device.
 * @dev:        handle to the device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * It does all the memory allocation and registration for the device.
 */
static int udmabuf_device_probe(struct device *dev)
{
    int                         retval       = 0;
    int                         prop_status  = 0;
    u32                         u32_value    = 0;
    u64                         u64_value    = 0;
    size_t                      size         = 0;
    int                         minor_number = -1;
    struct udmabuf_device_data* device_data  = NULL;
    const char*                 device_name  = NULL;

    /*
     * size property
     */
    if        ((prop_status = udmabuf_get_size_property(dev, &u64_value, true)) == 0) {
        size = u64_value;
    } else if ((prop_status = of_property_read_ulong(dev->of_node, "size", &u64_value)) == 0) {
        size = u64_value;
    } else {
        dev_err(dev, "invalid property size. status=%d\n", prop_status);
        retval = -ENODEV;
        goto failed;
    }
    if (size <= 0) {
        dev_err(dev, "invalid size, size=%zu\n", size);
        retval = -ENODEV;
        goto failed;
    }
    /*
     * minor-number property
     */
    if        (udmabuf_get_minor_number_property(dev, &u32_value, true) == 0) {
        minor_number = u32_value;
    } else if (of_property_read_u32(dev->of_node, "minor-number", &u32_value) == 0) {
        minor_number = u32_value;
    } else {
        minor_number = -1;
    }
    /*
     * device-name property
     */
    if (udmabuf_get_device_name_property(dev, &device_name, true) != 0)
        device_name = of_get_property(dev->of_node, "device-name", NULL);
    if (IS_ERR_OR_NULL(device_name)) {
        if (minor_number < 0)
            device_name = dev_name(dev);
        else
            device_name = NULL;
    }
    /*
     * udmabuf_device_create()
     */
    device_data = udmabuf_device_create(device_name, dev, minor_number);
    if (IS_ERR_OR_NULL(device_data)) {
        retval = PTR_ERR(device_data);
        dev_err(dev, "driver create failed. return=%d\n", retval);
        device_data = NULL;
        retval = (retval == 0) ? -EINVAL : retval;
        goto failed;
    }
    dev_set_drvdata(dev, device_data);
    /*
     * set size
     */
    device_data->size = size;
    /*
     * dma-mask property
     * If you want to set dma-mask, do it before of_dma_configure().
     * Because of_dma_configure() needs the value of dev->coherent_dma_mask.
     * However, executing dma_set_mask_and_coherent() before of_dma_configure() may fail.
     * Because dma_set_mask_and_coherent() will fail unless dev->dma_ops is set.
     * When dma_set_mask_and_coherent() fails, it is forcefuly setting the dma-mask value.
     */
    if (of_property_read_u32(dev->of_node, "dma-mask", &u32_value) == 0) {
        if ((u32_value > 64) || (u32_value < 12)) {
            dev_err(dev, "invalid dma-mask property value=%d\n", u32_value);
            goto failed;
        }
        retval = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(u32_value));
        if (retval != 0) {
            dev_info(dev, "dma_set_mask_and_coherent(DMA_BIT_MASK(%d)) failed. return=%d\n", u32_value, retval);
            retval = 0;
            *dev->dma_mask         = DMA_BIT_MASK(u32_value);
            dev->coherent_dma_mask = DMA_BIT_MASK(u32_value);
        }
    }
    /*
     * of_reserved_mem_device_init()
     */
#if (USE_OF_RESERVED_MEM == 1)
    if (dev->of_node != NULL) {
        retval = of_reserved_mem_device_init(dev);
        if (retval == 0) {
            device_data->of_reserved_mem = 1;
        } else if (retval != -ENODEV) {
            dev_err(dev, "of_reserved_mem_device_init failed. return=%d\n", retval);
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
#if ((USE_OF_RESERVED_MEM == 1) && (LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)))
    /* 
     * Under less than Linux Kernel 5.1, if "memory-region" property is specified, 
     * of_dma_configure() will not be executed.
     * Because in that case, it is already executed in of_reserved_mem_device_init().
     */
    if (device_data->of_reserved_mem == 0)
#endif
    {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0))
        retval = of_dma_configure(dev, dev->of_node, true);
#else
        retval = of_dma_configure(dev, dev->of_node);
#endif
        if (retval != 0) {
            dev_err(dev, "of_dma_configure failed. return=%d\n", retval);
            goto failed;
        }
#else
        of_dma_configure(dev, dev->of_node);
#endif
    }
#endif
    /*
     * sync-mode property
     */
    if (of_property_read_u32(dev->of_node, "sync-mode", &u32_value) == 0) {
        if ((u32_value < SYNC_MODE_MIN) || (u32_value > SYNC_MODE_MAX)) {
            dev_err(dev, "invalid sync-mode property value=%d\n", u32_value);
            goto failed;
        }
        device_data->sync_mode &= ~SYNC_MODE_MASK;
        device_data->sync_mode |= (int)u32_value;
    }
    /*
     * sync-always property
     */
    if (of_property_read_bool(dev->of_node, "sync-always")) {
        device_data->sync_mode |= SYNC_ALWAYS;
    }
    /*
     * sync-direction property
     */
    if (of_property_read_u32(dev->of_node, "sync-direction", &u32_value) == 0) {
        if (u32_value > 2) {
            dev_err(dev, "invalid sync-direction property value=%d\n", u32_value);
            goto failed;
        }
        device_data->sync_direction = (int)u32_value;
    }
    /*
     * sync-offset property
     */
    if (of_property_read_ulong(dev->of_node, "sync-offset", &u64_value) == 0) {
        if (u64_value >= device_data->size) {
            dev_err(dev, "invalid sync-offset property value=%llu\n", u64_value);
            goto failed;
        }
        device_data->sync_offset = (int)u64_value;
    }
    /*
     * sync-size property
     */
    if (of_property_read_ulong(dev->of_node, "sync-size", &u64_value) == 0) {
        if (device_data->sync_offset + u64_value > device_data->size) {
            dev_err(dev, "invalid sync-size property value=%llu\n", u64_value);
            goto failed;
        }
        device_data->sync_size = (size_t)u64_value;
    } else {
        device_data->sync_size = device_data->size;
    }
    /*
     * udmabuf_device_setup()
     */
    retval = udmabuf_device_setup(device_data);
    if (retval) {
        dev_err(dev, "driver setup failed. return=%d\n", retval);
        goto failed;
    }

    if (info_enable) {
        udmabuf_device_info(device_data);
    }

    return 0;

failed:
    udmabuf_device_remove(dev, device_data);

    return retval;
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
 * udmabuf_platform_driver_probe() -  Probe call for the device.
 * @pdev:       Handle to the platform device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * It does all the memory allocation and registration for the device.
 */
static int udmabuf_platform_driver_probe(struct platform_device *pdev)
{
    int retval = 0;

    dev_dbg(&pdev->dev, "driver probe start.\n");

    retval = udmabuf_device_probe(&pdev->dev);
    
    if (info_enable) {
        dev_info(&pdev->dev, "driver installed.\n");
    }
    return retval;
}
/**
 * udmabuf_platform_driver_remove() -  Remove call for the device.
 * @pdev:       Handle to the platform device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * Unregister the device after releasing the resources.
 */
static int udmabuf_platform_driver_remove(struct platform_device *pdev)
{
    struct udmabuf_device_data* this   = dev_get_drvdata(&pdev->dev);
    int                         retval = 0;

    dev_dbg(&pdev->dev, "driver remove start.\n");

    retval = udmabuf_device_remove(&pdev->dev, this);

    if (info_enable) {
        dev_info(&pdev->dev, "driver removed.\n");
    }
    return retval;
}

/**
 * Open Firmware Device Identifier Matching Table
 */
static struct of_device_id udmabuf_of_match[] = {
    { .compatible = "ikwzm,u-dma-buf", },
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
 * DOC: udmabuf manager device
 *
 * * enum udmabuf_manager_state        - udmabuf manager state enumeration.
 * * struct udmabuf_manager_data       - udmabuf manager data structure.
 * * udmabuf_manager_state_clear()     - udmabuf manager state clear.
 * * udmabuf_manager_buffer_overflow() - udmabuf manager check buffer overflow.
 * * udmabuf_manager_parse()           - udmabuf manager parse buffer.
 * * udmabuf_manager_file_open()       - udmabuf manager file open operation.
 * * udmabuf_manager_file_release()    - udmabuf manager file release operation.
 * * udmabuf_manager_file_write()      - udmabuf manager file write operation.
 * * udmabuf_manager_file_ops          - udmabuf manager file operation table.
 * * udmabuf_manager_device            - udmabuf manager misc device structure.
 * * udmabuf_manager_device_registerd  - udmabuf manager device registerd flag.
 * * 
 */
#if (UDMABUF_MGR_ENABLE == 1)
#include <linux/miscdevice.h>

#define UDMABUF_MGR_BUFFER_SIZE 256

/**
 * enum   udmabuf_manager_state - udmabuf manager state enumeration.
 */
enum   udmabuf_manager_state {
    udmabuf_manager_init_state    ,
    udmabuf_manager_create_command,
    udmabuf_manager_delete_command,
    udmabuf_manager_parse_error   ,
};

/**
 * struct udmabuf_manager_data - udmabuf manager data structure.
 */
struct udmabuf_manager_data {
    const char*                  device_name;
    int                          minor_number;
    unsigned int                 size;
    enum udmabuf_manager_state   state;
    unsigned int                 buffer_offset;
    char                         buffer[UDMABUF_MGR_BUFFER_SIZE];
};

/**
 * udmabuf_manager_buffer_overflow() - udmabuf manager check buffer overflow.
 * @this:       Pointer to the udmabuf manager data structure.
 */
static bool udmabuf_manager_buffer_overflow(struct udmabuf_manager_data *this)
{
    if (this == NULL)
        return true;
    else
        return (this->buffer_offset >= UDMABUF_MGR_BUFFER_SIZE);
}

/**
 * udmabuf_manager_state_clear() - udmabuf manager state clear.
 * @this:       Pointer to the udmabuf manager data structure.
 */
static void udmabuf_manager_state_clear(struct udmabuf_manager_data *this)
{
    this->device_name   = NULL;
    this->minor_number  = PLATFORM_DEVID_AUTO;
    this->size          = 0;
    this->state         = udmabuf_manager_init_state;
    this->buffer_offset = 0;
}

/**
 * udmabuf_manager_parse() - udmabuf manager parse buffer.
 * @this:       Pointer to the udmabuf manager data structure.
 * @buff:       Pointer to the user buffer.
 * @count:      The number of bytes to be written.
 * Return:      Size of copy from buff to this->buffer.
 */
static int udmabuf_manager_parse(struct udmabuf_manager_data *this, const char __user* buff, size_t count)
{
    bool   copy_done = false;
    size_t copy_size;
    int    parse_count;

    if (this->buffer_offset + count > UDMABUF_MGR_BUFFER_SIZE)
        copy_size = UDMABUF_MGR_BUFFER_SIZE - this->buffer_offset;
    else
        copy_size = count;

    if (copy_from_user(&(this->buffer[this->buffer_offset]), buff, copy_size) != 0) {
        return -EFAULT;
    }

    parse_count = 0;
    while(parse_count < copy_size) {
        char* ptr = &(this->buffer[this->buffer_offset+parse_count]);
        parse_count++;
        if ((*ptr == '\n') || (*ptr == '\0') || (*ptr == ';')) {
            *ptr = '\0';
            copy_done = true;
            break;
        }
    }
    this->buffer_offset += parse_count;

    if (copy_done == true) {
        char* parse_buffer = this->buffer;
        char* ptr = strsep(&parse_buffer, " ");
        if (ptr == NULL) {
            this->state = udmabuf_manager_parse_error;
            goto failed;
        } else if (strncmp(ptr, "create", strlen("create")) == 0) {
            this->state = udmabuf_manager_create_command;
        } else if (strncmp(ptr, "delete", strlen("delete")) == 0) {
            this->state = udmabuf_manager_delete_command;
        } else {
            this->state = udmabuf_manager_parse_error;
            goto failed;
        }
        ptr = strsep(&parse_buffer, " ");
        if (ptr == NULL) {
            this->state = udmabuf_manager_parse_error;
            goto failed;
        } else {
            this->device_name = ptr;
        }
        if (this->state == udmabuf_manager_create_command) {
            ptr = strsep(&parse_buffer, " ");
            if (ptr == NULL) {
                this->state = udmabuf_manager_parse_error;
                goto failed;
            } else {
                u64 value;
                if (kstrtoull(ptr, 0, &value) != 0) {
                    this->state = udmabuf_manager_parse_error;
                    goto failed;
                } else {
                    this->size = value;
                }
            }
        }
    }
failed:
    return parse_count;    
}

/**
 * udmabuf_manager_file_open() - udmabuf manager file open operation.
 * @inode:      Pointer to the inode structure of this device.
 * @file:       to the file structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_manager_file_open(struct inode *inode, struct file *file)
{
    struct udmabuf_manager_data* this;
    int status = 0;

    this = kzalloc(sizeof(*this), GFP_KERNEL);
    if (IS_ERR_OR_NULL(this)) {
        status = PTR_ERR(this);
    } else {
        status = 0;
        udmabuf_manager_state_clear(this);
        file->private_data = this;
    }
    return status;
}

/**
 * udmabuf_manager_file_release() - udmabuf manager file release operation.
 * @inode:      Pointer to the inode structure of this device.
 * @file:       Pointer to the file structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_manager_file_release(struct inode *inode, struct file *file)
{
    struct udmabuf_manager_data* this = file->private_data;
    if (this != NULL) 
        kfree(this);
    return 0;
}

/**
 * udmabuf_manager_file_write() - udmabuf manager file write operation.
 * @file:       Pointer to the file structure.
 * @buff:       Pointer to the user buffer.
 * @count:      The number of bytes to be written.
 * @ppos:       Pointer to the offset value
 * Return:      Transferd size.
 */
static ssize_t udmabuf_manager_file_write(struct file* file, const char __user* buff, size_t count, loff_t* ppos)
{
    struct udmabuf_manager_data*    this      = file->private_data;
    struct udmabuf_platform_device* plat;
    int                             result    = 0;
    size_t                          xfer_size = 0;

    if (this == NULL)
        return -EINVAL;

    if (udmabuf_manager_buffer_overflow(this))
        return -ENOSPC;
    
    while(xfer_size < count) {
        int parse_size = udmabuf_manager_parse(this, buff + xfer_size, count - xfer_size);
        if (parse_size < 0) {
            result = parse_size;
            goto failed;
        }
        switch (this->state) {
            case udmabuf_manager_create_command :
                printk(KERN_INFO "%s : create %s %d\n"  , UDMABUF_MGR_NAME, this->device_name, this->size);
                result = udmabuf_platform_device_create(this->device_name, this->minor_number, this->size);
                if (result == 0) {
                    udmabuf_manager_state_clear(this);
                } else {
                    printk(KERN_ERR "%s : create error: %s result = %d\n", UDMABUF_MGR_NAME, this->device_name, result);
                    udmabuf_manager_state_clear(this);
                    goto failed;
                }
                break;
            case udmabuf_manager_delete_command :
                printk(KERN_INFO "%s : delete %s\n"     , UDMABUF_MGR_NAME, this->device_name);
                plat = udmabuf_platform_device_search(this->device_name, this->minor_number);
                if (plat != NULL) {
                    udmabuf_platform_device_remove(plat);
                    udmabuf_manager_state_clear(this);
                } else {
                    printk(KERN_ERR "%s : delete error: %s not found\n", UDMABUF_MGR_NAME, this->device_name);
                    udmabuf_manager_state_clear(this);
                    result = -EINVAL;
                    goto failed;
                }
                break;
            case udmabuf_manager_parse_error :
                    printk(KERN_ERR "%s : parse error: ""%s""\n", UDMABUF_MGR_NAME, this->buffer);
                    udmabuf_manager_state_clear(this);
                    result = -EINVAL;
                    goto failed;
            default:
                break;
        }
        xfer_size += parse_size;
    }
    *ppos += xfer_size;
    result = xfer_size;
  failed:
    return result;
}

/**
 * udmabuf manager file operation table.
 */
static const struct file_operations udmabuf_manager_file_ops = {
    .owner   = THIS_MODULE,
    .open    = udmabuf_manager_file_open,
    .release = udmabuf_manager_file_release,
    .write   = udmabuf_manager_file_write,
};

/**
 * udmabuf manager misc device structure.
 */
static struct miscdevice udmabuf_manager_device = {
    .minor   = MISC_DYNAMIC_MINOR,
    .name    = UDMABUF_MGR_NAME,
    .fops    = &udmabuf_manager_file_ops,
};

static bool udmabuf_manager_device_registerd = false;
#endif

/**
 * DOC: u-dma-buf Kernel Module Operations
 *
 * * u_dma_buf_cleanup()
 * * u_dma_buf_init()
 * * u_dma_buf_exit()
 */

static bool udmabuf_platform_driver_registerd = false;

/**
 * u_dma_buf_cleanup()
 */
static void u_dma_buf_cleanup(void)
{
#if (UDMABUF_MGR_ENABLE == 1)
    if (udmabuf_manager_device_registerd ){misc_deregister(&udmabuf_manager_device);}
#endif
    udmabuf_platform_device_remove_all();
    if (udmabuf_platform_driver_registerd){platform_driver_unregister(&udmabuf_platform_driver);}
    if (udmabuf_sys_class     != NULL    ){class_destroy(udmabuf_sys_class);}
    if (udmabuf_device_number != 0       ){unregister_chrdev_region(udmabuf_device_number, 0);}
    ida_destroy(&udmabuf_device_ida);
}

/**
 * u_dma_buf_init()
 */
static int __init u_dma_buf_init(void)
{
    int retval = 0;

    ida_init(&udmabuf_device_ida);
    INIT_LIST_HEAD(&udmabuf_platform_device_list);
    mutex_init(&udmabuf_platform_device_sem);

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

    udmabuf_static_device_reserve_minor_number_all();

    retval = platform_driver_register(&udmabuf_platform_driver);
    if (retval) {
        printk(KERN_ERR "%s: couldn't register platform driver. return=%d\n", DRIVER_NAME, retval);
        udmabuf_platform_driver_registerd = false;
        goto failed;
    } else {
        udmabuf_platform_driver_registerd = true;
    }

    udmabuf_static_device_create_all();

#if (UDMABUF_MGR_ENABLE == 1)
    retval = misc_register(&udmabuf_manager_device);
    if (retval) {
        printk(KERN_ERR "%s: couldn't register udmabuf-mgr. return=%d\n", DRIVER_NAME, retval);
        udmabuf_manager_device_registerd = false;
        goto failed;
    } else {
        udmabuf_manager_device_registerd = true;
    }
#endif
    return 0;

 failed:
    u_dma_buf_cleanup();
    return retval;
}

/**
 * u_dma_buf_exit()
 */
static void __exit u_dma_buf_exit(void)
{
    u_dma_buf_cleanup();
}

module_init(u_dma_buf_init);
module_exit(u_dma_buf_exit);
