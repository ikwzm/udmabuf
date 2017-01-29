/*********************************************************************************
 *
 *       Copyright (C) 2015-2017 Ichiro Kawazome
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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
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
#include "minor_number_allocator.h"

#define DRIVER_NAME        "udmabuf"
#define DEVICE_NAME_FORMAT "udmabuf%d"
#define DEVICE_MAX_NUM      256
#define UDMABUF_DEBUG       1
#ifdef  CONFIG_ARM
#define SYNC_ENABLE         1
#else
#define SYNC_ENABLE         0
#endif

#if     (LINUX_VERSION_CODE >= 0x030B00)
#define USE_DEV_GROUPS      1
#else
#define USE_DEV_GROUPS      0
#endif

#if     (UDMABUF_DEBUG == 1)
#define UDMABUF_DEBUG_CHECK(this,debug) (this->debug)
#else
#define UDMABUF_DEBUG_CHECK(this,debug) (0)
#endif

static struct class*  udmabuf_sys_class     = NULL;
static dev_t          udmabuf_device_number = 0;
static int            dma_mask_bit          = 32;

/**
 * struct udmabuf_driver_data - Device driver structure
 */
struct udmabuf_driver_data {
    struct device*       device;
    struct cdev          cdev;
    dev_t                device_number;
    struct mutex         sem;
    bool                 is_open;
    int                  size;
    size_t               alloc_size;
    void*                virt_addr;
    dma_addr_t           phys_addr;
    u64                  dma_mask;
#if (SYNC_ENABLE == 1)
    int                  sync_mode;
    int                  sync_offset;
    size_t               sync_size;
    int                  sync_direction;
    bool                 sync_owner;
    int                  sync_for_cpu;
    int                  sync_for_device;
#endif
#if ((UDMABUF_DEBUG == 1) && (SYNC_ENABLE == 1))
    bool                 debug_vma;
#endif   
};

/**
 *
 */
#define SYNC_INVALID       (0x00)
#define SYNC_NONCACHED     (0x01)
#define SYNC_WRITECOMBINE  (0x02)
#define SYNC_DMACOHERENT   (0x03)
#define SYNC_MODE_MASK     (0x03)
#define SYNC_ALWAYS        (0x04)

#if (SYNC_ENABLE == 1)
/**
 * udmabuf_sync_for_cpu() - call dma_sync_single_for_cpu()
 * returns:	Success(0)
 */
static int udmabuf_sync_for_cpu(struct udmabuf_driver_data* this)
{
    if (this->sync_for_cpu) {
        dma_addr_t phys_addr = this->phys_addr + this->sync_offset;
        enum dma_data_direction direction;
        switch(this->sync_direction) {
            case 1 : direction = DMA_TO_DEVICE    ; break;
            case 2 : direction = DMA_FROM_DEVICE  ; break;
            default: direction = DMA_BIDIRECTIONAL; break;
        }
        dma_sync_single_for_cpu(this->device, phys_addr, this->sync_size, direction);
        this->sync_for_cpu = 0;
        this->sync_owner   = 0;
    }
    return 0;
}
#endif
#if (SYNC_ENABLE == 1)
/**
 * udmabuf_sync_for_device() - call dma_sync_single_for_device()
 * returns:	Success(0)
 */
static int udmabuf_sync_for_device(struct udmabuf_driver_data* this)
{
    if (this->sync_for_device) {
        dma_addr_t phys_addr = this->phys_addr + this->sync_offset;
        enum dma_data_direction direction;
        switch(this->sync_direction) {
            case 1 : direction = DMA_TO_DEVICE    ; break;
            case 2 : direction = DMA_FROM_DEVICE  ; break;
            default: direction = DMA_BIDIRECTIONAL; break;
        }
        dma_sync_single_for_device(this->device, phys_addr, this->sync_size, direction);
        this->sync_for_device = 0;
        this->sync_owner      = 1;
    }
    return 0;
}
#endif

#define DEF_ATTR_SHOW(__attr_name, __format, __value) \
static ssize_t udmabuf_show_ ## __attr_name(struct device *dev, struct device_attribute *attr, char *buf) \
{                                                            \
    ssize_t status;                                          \
    struct udmabuf_driver_data* this = dev_get_drvdata(dev); \
    if (mutex_lock_interruptible(&this->sem) != 0)           \
        return -ERESTARTSYS;                                 \
    status = sprintf(buf, __format, (__value));              \
    mutex_unlock(&this->sem);                                \
    return status;                                           \
}

static inline int NO_ACTION(struct udmabuf_driver_data* this){return 0;}

#define DEF_ATTR_SET(__attr_name, __min, __max, __pre_action, __post_action) \
static ssize_t udmabuf_set_ ## __attr_name(struct device *dev, struct device_attribute *attr, const char *buf, size_t size) \
{ \
    ssize_t       status; \
    unsigned long value;  \
    struct udmabuf_driver_data* this = dev_get_drvdata(dev);                 \
    if (0 != mutex_lock_interruptible(&this->sem)){return -ERESTARTSYS;}     \
    if (0 != (status = kstrtoul(buf, 10, &value))){            goto failed;} \
    if ((value < __min) || (__max < value)) {status = -EINVAL; goto failed;} \
    if (0 != (status = __pre_action(this)))       {            goto failed;} \
    this->__attr_name = value;                                               \
    if (0 != (status = __post_action(this)))      {            goto failed;} \
    status = size;                                                           \
  failed:                                                                    \
    mutex_unlock(&this->sem);                                                \
    return status;                                                           \
}

DEF_ATTR_SHOW(size           , "%d\n"   , this->size                              );
DEF_ATTR_SHOW(phys_addr      , "0x%lx\n", (long unsigned int)this->phys_addr      );
#if (SYNC_ENABLE == 1)
DEF_ATTR_SHOW(sync_mode      , "%d\n"   , this->sync_mode                         );
DEF_ATTR_SET( sync_mode                 , 0, 7, NO_ACTION, NO_ACTION              );
DEF_ATTR_SHOW(sync_offset    , "0x%lx\n", (long unsigned int)this->sync_offset    );
DEF_ATTR_SET( sync_offset               , 0, 0xFFFFFFFF, NO_ACTION, NO_ACTION     );
DEF_ATTR_SHOW(sync_size      , "%zu\n"  , this->sync_size                         );
DEF_ATTR_SET( sync_size                 , 0, 0xFFFFFFFF, NO_ACTION, NO_ACTION     );
DEF_ATTR_SHOW(sync_direction , "%d\n"   , this->sync_direction                    );
DEF_ATTR_SET( sync_direction            , 0, 3, NO_ACTION, NO_ACTION              );
DEF_ATTR_SHOW(sync_owner     , "%d\n"   , this->sync_owner                        );
DEF_ATTR_SHOW(sync_for_cpu   , "%d\n"   , this->sync_for_cpu                      );
DEF_ATTR_SET( sync_for_cpu              , 0, 1, NO_ACTION, udmabuf_sync_for_cpu   );
DEF_ATTR_SHOW(sync_for_device, "%d\n"   , this->sync_for_device                   );
DEF_ATTR_SET( sync_for_device           , 0, 1, NO_ACTION, udmabuf_sync_for_device);
#endif
#if ((UDMABUF_DEBUG == 1) && (SYNC_ENABLE == 1))
DEF_ATTR_SHOW(debug_vma      , "%d\n"   , this->debug_vma                         );
DEF_ATTR_SET( debug_vma                 , 0, 1, NO_ACTION, NO_ACTION              );
#endif

static struct device_attribute udmabuf_device_attrs[] = {
  __ATTR(size           , 0444, udmabuf_show_size            , NULL                       ),
  __ATTR(phys_addr      , 0444, udmabuf_show_phys_addr       , NULL                       ),
#if (SYNC_ENABLE == 1)
  __ATTR(sync_mode      , 0664, udmabuf_show_sync_mode       , udmabuf_set_sync_mode      ),
  __ATTR(sync_offset    , 0664, udmabuf_show_sync_offset     , udmabuf_set_sync_offset    ),
  __ATTR(sync_size      , 0664, udmabuf_show_sync_size       , udmabuf_set_sync_size      ),
  __ATTR(sync_direction , 0664, udmabuf_show_sync_direction  , udmabuf_set_sync_direction ),
  __ATTR(sync_owner     , 0664, udmabuf_show_sync_owner      , NULL                       ),
  __ATTR(sync_for_cpu   , 0664, udmabuf_show_sync_for_cpu    , udmabuf_set_sync_for_cpu   ),
  __ATTR(sync_for_device, 0664, udmabuf_show_sync_for_device , udmabuf_set_sync_for_device),
#endif
#if ((UDMABUF_DEBUG == 1) && (SYNC_ENABLE == 1))
  __ATTR(debug_vma      , 0664, udmabuf_show_debug_vma       , udmabuf_set_debug_vma      ),
#endif
  __ATTR_NULL,
};

#if (USE_DEV_GROUPS == 1)

static struct attribute *udmabuf_attrs[] = {
  &(udmabuf_device_attrs[0].attr),
  &(udmabuf_device_attrs[1].attr),
#if (SYNC_ENABLE == 1)
  &(udmabuf_device_attrs[2].attr),
  &(udmabuf_device_attrs[3].attr),
  &(udmabuf_device_attrs[4].attr),
  &(udmabuf_device_attrs[5].attr),
  &(udmabuf_device_attrs[6].attr),
  &(udmabuf_device_attrs[7].attr),
  &(udmabuf_device_attrs[8].attr),
#endif
#if ((UDMABUF_DEBUG == 1) && (SYNC_ENABLE == 1))
  &(udmabuf_device_attrs[9].attr),
#endif
  NULL
};
static struct attribute_group  udmabuf_attr_group = {
  .attrs = udmabuf_attrs
};
static const struct attribute_group* udmabuf_attr_groups[] = {
  &udmabuf_attr_group,
  NULL
};

#define SET_SYS_CLASS_ATTRIBUTES(sys_class) {(sys_class)->dev_groups = udmabuf_attr_groups; }
#else
#define SET_SYS_CLASS_ATTRIBUTES(sys_class) {(sys_class)->dev_attrs  = udmabuf_device_attrs;}
#endif

#if (SYNC_ENABLE == 1)
/**
 * udmabuf_driver_vma_open() - This is the driver open function.
 * @vma:        Pointer to the vm area structure.
 * returns:	Success or error status.
 */
static void udmabuf_driver_vma_open(struct vm_area_struct* vma)
{
    struct udmabuf_driver_data* this = vma->vm_private_data;
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->device, "vma_open(virt_addr=0x%lx, offset=0x%lx)\n", vma->vm_start, vma->vm_pgoff<<PAGE_SHIFT);
}
#endif

#if (SYNC_ENABLE == 1)
/**
 * udmabuf_driver_vma_close() - This is the driver close function.
 * @vma:        Pointer to the vm area structure.
 * returns:	Success or error status.
 */
static void udmabuf_driver_vma_close(struct vm_area_struct* vma)
{
    struct udmabuf_driver_data* this = vma->vm_private_data;
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->device, "vma_close()\n");
}
#endif

#if (SYNC_ENABLE == 1)
/**
 * udmabuf_driver_vma_fault() - This is the driver nopage function.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * returns:	Success or error status.
 */
static int udmabuf_driver_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
{
    struct udmabuf_driver_data* this = vma->vm_private_data;
    struct page*  page_ptr           = NULL;
    unsigned long offset             = vmf->pgoff << PAGE_SHIFT;
    unsigned long phys_addr          = this->phys_addr + offset;
    unsigned long page_frame_num     = phys_addr  >> PAGE_SHIFT;
    unsigned long request_size       = 1          << PAGE_SHIFT;
    unsigned long available_size     = this->alloc_size -offset;

    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->device, "vma_fault(virt_addr=0x%lx, phys_addr=0x%lx)\n", (long unsigned int)vmf->virtual_address, phys_addr);

    if (request_size > available_size) 
        return VM_FAULT_SIGBUS;

    if (!pfn_valid(page_frame_num))
        return VM_FAULT_SIGBUS;

    page_ptr = pfn_to_page(page_frame_num);
    get_page(page_ptr);
    vmf->page = page_ptr;
    return 0;
}
#endif

#if (SYNC_ENABLE == 1)
/**
 *
 */
static const struct vm_operations_struct udmabuf_driver_vm_ops = {
    .open    = udmabuf_driver_vma_open ,
    .close   = udmabuf_driver_vma_close,
    .fault   = udmabuf_driver_vma_fault,
};
#endif

/**
 * udmabuf_driver_file_open() - This is the driver open function.
 * @inode:	Pointer to the inode structure of this device.
 * @file:	Pointer to the file structure.
 * returns:	Success or error status.
 */
static int udmabuf_driver_file_open(struct inode *inode, struct file *file)
{
    struct udmabuf_driver_data* this;
    int status = 0;

    this = container_of(inode->i_cdev, struct udmabuf_driver_data, cdev);
    file->private_data = this;
    this->is_open = 1;

    return status;
}

/**
 * udmabuf_driver_file_release() - This is the driver release function.
 * @inode:	Pointer to the inode structure of this device.
 * @file:	Pointer to the file structure.
 * returns:	Success.
 */
static int udmabuf_driver_file_release(struct inode *inode, struct file *file)
{
    struct udmabuf_driver_data* this = file->private_data;

    this->is_open = 0;

    return 0;
}

/**
 * udmabuf_driver_file_mmap() - This is the driver memory map function.
 * @file:	Pointer to the file structure.
 * @vma:        Pointer to the vm area structure.
 * returns:	Success.
 */
static int udmabuf_driver_file_mmap(struct file *file, struct vm_area_struct* vma)
{
    struct udmabuf_driver_data* this = file->private_data;

#if (SYNC_ENABLE == 1)
    vma->vm_ops           = &udmabuf_driver_vm_ops;
    vma->vm_private_data  = this;
 /* vma->vm_flags        |= VM_RESERVED; */
    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS)) {
        switch (this->sync_mode & SYNC_MODE_MASK) {
            case SYNC_NONCACHED : 
                vma->vm_flags    |= VM_IO;
                vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
                break;
            case SYNC_WRITECOMBINE : 
                vma->vm_flags    |= VM_IO;
                vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
                break;
            case SYNC_DMACOHERENT : 
                vma->vm_flags    |= VM_IO;
                vma->vm_page_prot = pgprot_dmacoherent(vma->vm_page_prot);
                break;
            default :
                break;
        }
    }
    udmabuf_driver_vma_open(vma);
    return 0;
#else
    return dma_mmap_coherent(this->device, vma, this->virt_addr, this->phys_addr, this->alloc_size);
#endif
}

/**
 * udmabuf_driver_file_read() - This is the driver read function.
 * @file:	Pointer to the file structure.
 * @buff:	Pointer to the user buffer.
 * @count:	The number of bytes to be written.
 * @ppos:	Pointer to the offset value
 * returns:	Success or error status.
 */
static ssize_t udmabuf_driver_file_read(struct file* file, char __user* buff, size_t count, loff_t* ppos)
{
    struct udmabuf_driver_data* this      = file->private_data;
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

#if (SYNC_ENABLE == 1)
    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS))
        dma_sync_single_for_cpu(this->device, phys_addr, xfer_size, DMA_FROM_DEVICE);
#endif

    if ((remain_size = copy_to_user(buff, virt_addr, xfer_size)) != 0) {
        result = 0;
        goto return_unlock;
    }

#if (SYNC_ENABLE == 1)
    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS))
        dma_sync_single_for_device(this->device, phys_addr, xfer_size, DMA_FROM_DEVICE);
#endif

    *ppos += xfer_size;
    result = xfer_size;
 return_unlock:
    mutex_unlock(&this->sem);
    return result;
}

/**
 * udmabuf_driver_file_write() - This is the driver write function.
 * @file:	Pointer to the file structure.
 * @buff:	Pointer to the user buffer.
 * @count:	The number of bytes to be written.
 * @ppos:	Pointer to the offset value
 * returns:	Success or error status.
 */
static ssize_t udmabuf_driver_file_write(struct file* file, const char __user* buff, size_t count, loff_t* ppos)
{
    struct udmabuf_driver_data* this      = file->private_data;
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

#if (SYNC_ENABLE == 1)
    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS))
        dma_sync_single_for_cpu(this->device, phys_addr, xfer_size, DMA_TO_DEVICE);
#endif

    if ((remain_size = copy_from_user(virt_addr, buff, xfer_size)) != 0) {
        result = 0;
        goto return_unlock;
    }

#if (SYNC_ENABLE == 1)
    if ((file->f_flags & O_SYNC) | (this->sync_mode & SYNC_ALWAYS))
        dma_sync_single_for_device(this->device, phys_addr, xfer_size, DMA_TO_DEVICE);
#endif

    *ppos += xfer_size;
    result = xfer_size;
 return_unlock:
    mutex_unlock(&this->sem);
    return result;
}

/**
 * udmabuf_driver_file_llseek() - This is the driver llseek function.
 * @file:	Pointer to the file structure.
 * @offset:	File offset to seek.
 * @whence:	Type of seek.
 * returns:	The new position.
 */
static loff_t udmabuf_driver_file_llseek(struct file* file, loff_t offset, int whence)
{
    struct udmabuf_driver_data* this = file->private_data;
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
 *
 */
static const struct file_operations udmabuf_driver_file_ops = {
    .owner   = THIS_MODULE,
    .open    = udmabuf_driver_file_open,
    .release = udmabuf_driver_file_release,
    .mmap    = udmabuf_driver_file_mmap,
    .read    = udmabuf_driver_file_read,
    .write   = udmabuf_driver_file_write,
    .llseek  = udmabuf_driver_file_llseek,
};

/**
 * udmabuf_device_minor_number_allocator
 */
DECLARE_MINOR_NUMBER_ALLOCATOR(udmabuf_device, DEVICE_MAX_NUM);

/**
 * udmabuf_driver_create() -  Create call for the device.
 *
 * @name:       device name or NULL
 * @parent:     parent device
 * @minor:	minor_number or -1
 * @size:	buffer size
 * Returns device driver strcutre pointer
 *
 * It does all the memory allocation and registration for the device.
 */
static struct udmabuf_driver_data* udmabuf_driver_create(const char* name, struct device* parent, int minor, unsigned int size)
{
    struct udmabuf_driver_data* this     = NULL;
    unsigned int                done     = 0;
    const unsigned int          DONE_ALLOC_MINOR   = (1 << 0);
    const unsigned int          DONE_CHRDEV_ADD    = (1 << 1);
    const unsigned int          DONE_ALLOC_CMA     = (1 << 2);
    const unsigned int          DONE_DEVICE_CREATE = (1 << 3);
    /*
     * alloc device_minor_number
     */
    {
        if ((0 <= minor) && (minor < DEVICE_MAX_NUM)) {
            if (udmabuf_device_minor_number_allocate(minor) < 0) {
                printk(KERN_ERR "invalid or conflict minor number %d.\n", minor);
                goto failed;
            }
        } else {
            if ((minor = udmabuf_device_minor_number_new()) < 0) {
                printk(KERN_ERR "couldn't allocate minor number.\n");
                goto failed;
            }
        }
        done |= DONE_ALLOC_MINOR;
    }
    /*
     * create (udmabuf_driver_data*) this.
     */
    {
        this = kzalloc(sizeof(*this), GFP_KERNEL);
        if (IS_ERR_OR_NULL(this)) {
            goto failed;
        }
    }
    /*
     * make this->device_number and this->size
     */
    {
        this->device_number   = MKDEV(MAJOR(udmabuf_device_number), minor);
        this->size            = size;
        this->alloc_size      = ((size + ((1 << PAGE_SHIFT) - 1)) >> PAGE_SHIFT) << PAGE_SHIFT;
    }
#if (SYNC_ENABLE == 1)
    {
        this->sync_mode       = SYNC_NONCACHED;
        this->sync_offset     = 0;
        this->sync_size       = size;
        this->sync_direction  = 0;
        this->sync_owner      = 0;
        this->sync_for_cpu    = 0;
        this->sync_for_device = 0;
    }
#endif
#if ((UDMABUF_DEBUG == 1) && (SYNC_ENABLE == 1))
    {
        this->debug_vma       = 0;
    }
#endif
    /*
     * register /sys/class/udmabuf/udmabuf[0..n]
     */
    {
        if (name == NULL) {
            this->device = device_create(udmabuf_sys_class,
                                         parent,
                                         this->device_number,
                                         (void *)this,
                                         DEVICE_NAME_FORMAT, MINOR(this->device_number));
        } else {
            this->device = device_create(udmabuf_sys_class,
                                         parent,
                                         this->device_number,
                                         (void *)this,
                                         name);
        }
        if (IS_ERR_OR_NULL(this->device)) {
            this->device = NULL;
            goto failed;
        }
        done |= DONE_DEVICE_CREATE;
    }
    /*
     * setup dma mask 
     */
    {
        this->device->dma_mask = &this->dma_mask;
        if (dma_set_mask(this->device, DMA_BIT_MASK(dma_mask_bit)) == 0) {
            dma_set_coherent_mask(this->device, DMA_BIT_MASK(dma_mask_bit));
        } else {
            printk(KERN_WARNING "dma_set_mask(DMA_BIT_MASK(%d)) failed\n", dma_mask_bit);
            dma_set_mask(this->device, DMA_BIT_MASK(32));
            dma_set_coherent_mask(this->device, DMA_BIT_MASK(32));
        }
    }
    /*
     * dma buffer allocation 
     */
    {
        this->virt_addr = dma_alloc_coherent(this->device, this->alloc_size, &this->phys_addr, GFP_KERNEL);
        if (IS_ERR_OR_NULL(this->virt_addr)) {
            printk(KERN_ERR "dma_alloc_coherent() failed\n");
            this->virt_addr = NULL;
            goto failed;
        }
        done |= DONE_ALLOC_CMA;
    }
    /*
     * add chrdev.
     */
    {
        cdev_init(&this->cdev, &udmabuf_driver_file_ops);
        this->cdev.owner = THIS_MODULE;
        if (cdev_add(&this->cdev, this->device_number, 1) != 0) {
            printk(KERN_ERR "cdev_add() failed\n");
            goto failed;
        }
        done |= DONE_CHRDEV_ADD;
    }
    /*
     *
     */
    mutex_init(&this->sem);
    /*
     *
     */
    dev_info(this->device, "driver installed\n");
    dev_info(this->device, "major number   = %d\n"    , MAJOR(this->device_number));
    dev_info(this->device, "minor number   = %d\n"    , MINOR(this->device_number));
    dev_info(this->device, "phys address   = 0x%lx\n" , (long unsigned int)this->phys_addr);
    dev_info(this->device, "buffer size    = %zu\n"   , this->alloc_size);

    return this;

 failed:
    if (done & DONE_ALLOC_MINOR  ) { udmabuf_device_minor_number_free(minor);}
    if (done & DONE_DEVICE_CREATE) { device_destroy(udmabuf_sys_class, this->device_number);}
    if (done & DONE_ALLOC_CMA    ) { dma_free_coherent(this->device, this->alloc_size, this->virt_addr, this->phys_addr);}
    if (done & DONE_CHRDEV_ADD   ) { cdev_del(&this->cdev); }
    if (this != NULL)              { kfree(this); }
    return NULL;
}

/**
 * udmabuf_driver_destroy() -  Remove call for the device.
 *
 * @this: udmabuf driver data strcutre pointer
 * Returns 0 or error status.
 *
 * Unregister the device after releasing the resources.
 */
static int udmabuf_driver_destroy(struct udmabuf_driver_data* this)
{
    if (!this)
        return -ENODEV;

    udmabuf_device_minor_number_free(MINOR(this->device_number));
    dev_info(this->device, "driver uninstalled\n");
    dma_free_coherent(this->device, this->alloc_size, this->virt_addr, this->phys_addr);
    device_destroy(udmabuf_sys_class, this->device_number);
    cdev_del(&this->cdev);
    kfree(this);
    return 0;
}

/**
 * udmabuf_platform_driver_probe() -  Probe call for the device.
 *
 * @pdev:	handle to the platform device structure.
 * Returns 0 on success, negative error otherwise.
 *
 * It does all the memory allocation and registration for the device.
 */
static int udmabuf_platform_driver_probe(struct platform_device *pdev)
{
    int          retval = 0;
    unsigned int size   = 0;
    int          minor_number = -1;
    const char*  device_name;

    dev_info(&pdev->dev, "driver probe start.\n");
    /*
     * get buffer size
     */
    {
        int status;
        status = of_property_read_u32(pdev->dev.of_node, "size", &size);
        if (status != 0) {
            dev_err(&pdev->dev, "invalid property size.\n");
            retval = -ENODEV;
            goto failed;
        }
    }
    /*
     * get device number
     */
    {
        int status;
        unsigned int number;
        status = of_property_read_u32(pdev->dev.of_node, "minor-number", &number);
        minor_number = (status == 0) ? number : -1;
    }
    /*
     * get device name
     */
    {
        device_name = of_get_property(pdev->dev.of_node, "device-name", NULL);
        
        if (IS_ERR_OR_NULL(device_name)) {
            if (minor_number < 0)
                device_name = dev_name(&pdev->dev);
            else
                device_name = NULL;
        }
    }
    /*
     * create (udmabuf_driver_data*)this.
     */
    {
        struct udmabuf_driver_data* driver_data = udmabuf_driver_create(device_name, &pdev->dev, minor_number, size);
        if (IS_ERR_OR_NULL(driver_data)) {
            dev_err(&pdev->dev, "driver create fail.\n");
            retval = PTR_ERR(driver_data);
            goto failed;
        }
        dev_set_drvdata(&pdev->dev, driver_data);
    }
    dev_info(&pdev->dev, "driver installed.\n");
    return 0;
 failed:
    dev_info(&pdev->dev, "driver install failed.\n");
    return retval;
}

/**
 * udmabuf_platform_driver_remove() -  Remove call for the device.
 *
 * @pdev:	handle to the platform device structure.
 * Returns 0 or error status.
 *
 * Unregister the device after releasing the resources.
 */
static int udmabuf_platform_driver_remove(struct platform_device *pdev)
{
    struct udmabuf_driver_data* this   = dev_get_drvdata(&pdev->dev);
    int                         retval = 0;

    if ((retval = udmabuf_driver_destroy(this)) != 0)
        return retval;
    dev_set_drvdata(&pdev->dev, NULL);
    dev_info(&pdev->dev, "driver unloaded\n");
    return 0;
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
static bool udmabuf_platform_driver_done = 0;

/**
 * 
 */
static int        udmabuf0 = 0;
static int        udmabuf1 = 0;
static int        udmabuf2 = 0;
static int        udmabuf3 = 0;
module_param(     udmabuf0, int, S_IRUGO);
module_param(     udmabuf1, int, S_IRUGO);
module_param(     udmabuf2, int, S_IRUGO);
module_param(     udmabuf3, int, S_IRUGO);
MODULE_PARM_DESC( udmabuf0, "udmabuf0 buffer size");
MODULE_PARM_DESC( udmabuf1, "udmabuf1 buffer size");
MODULE_PARM_DESC( udmabuf2, "udmabuf2 buffer size");
MODULE_PARM_DESC( udmabuf3, "udmabuf3 buffer size");

module_param(     dma_mask_bit, int, S_IRUGO);
MODULE_PARM_DESC( dma_mask_bit, "udmabuf dma mask bit(default=32)");

struct udmabuf_driver_data* udmabuf_driver[4] = {NULL,NULL,NULL,NULL};

#define CREATE_UDMABUF_DRIVER(__num,parent)                                                  \
    if (udmabuf ## __num > 0) {                                                              \
        udmabuf_driver[__num] = udmabuf_driver_create(NULL, parent, __num, udmabuf ## __num);\
        if (IS_ERR_OR_NULL(udmabuf_driver[__num])) {                                         \
            udmabuf_driver[__num] = NULL;                                                    \
            printk(KERN_ERR "%s: couldn't create udmabuf%d driver\n", DRIVER_NAME, __num);   \
        }                                                                                    \
    }

/**
 * udmabuf_module_exit()
 */
static void __exit udmabuf_module_exit(void)
{
    if (udmabuf_driver[3]     != NULL){udmabuf_driver_destroy(udmabuf_driver[3]);}
    if (udmabuf_driver[2]     != NULL){udmabuf_driver_destroy(udmabuf_driver[2]);}
    if (udmabuf_driver[1]     != NULL){udmabuf_driver_destroy(udmabuf_driver[1]);}
    if (udmabuf_driver[0]     != NULL){udmabuf_driver_destroy(udmabuf_driver[0]);}
    if (udmabuf_platform_driver_done ){platform_driver_unregister(&udmabuf_platform_driver);}
    if (udmabuf_sys_class     != NULL){class_destroy(udmabuf_sys_class);}
    if (udmabuf_device_number != 0   ){unregister_chrdev_region(udmabuf_device_number, 0);}
}

/**
 * udmabuf_module_init()
 */
static int __init udmabuf_module_init(void)
{
    int retval = 0;

    udmabuf_device_minor_number_allocator_initilize();
      
    retval = alloc_chrdev_region(&udmabuf_device_number, 0, 0, DRIVER_NAME);
    if (retval != 0) {
        printk(KERN_ERR "%s: couldn't allocate device major number\n", DRIVER_NAME);
        udmabuf_device_number = 0;
        goto failed;
    }

    udmabuf_sys_class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR_OR_NULL(udmabuf_sys_class)) {
        printk(KERN_ERR "%s: couldn't create sys class\n", DRIVER_NAME);
        retval = PTR_ERR(udmabuf_sys_class);
        udmabuf_sys_class = NULL;
        goto failed;
    }
    SET_SYS_CLASS_ATTRIBUTES(udmabuf_sys_class);

    CREATE_UDMABUF_DRIVER(0, NULL);
    CREATE_UDMABUF_DRIVER(1, NULL);
    CREATE_UDMABUF_DRIVER(2, NULL);
    CREATE_UDMABUF_DRIVER(3, NULL);

    retval = platform_driver_register(&udmabuf_platform_driver);
    if (retval) {
        printk(KERN_ERR "%s: couldn't register platform driver\n", DRIVER_NAME);
    } else {
        udmabuf_platform_driver_done = 1;
    }
    return 0;

 failed:
    udmabuf_module_exit();
    return retval;
}

module_init(udmabuf_module_init);
module_exit(udmabuf_module_exit);

MODULE_AUTHOR("ikwzm");
MODULE_DESCRIPTION("User space mappable DMA buffer device driver");
MODULE_LICENSE("Dual BSD/GPL");

