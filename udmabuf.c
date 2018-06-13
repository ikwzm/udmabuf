/*********************************************************************************
 *
 *       Copyright (C) 2015-2018 Ichiro Kawazome
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

#define DRIVER_NAME        "udmabuf"
#define DEVICE_NAME_FORMAT "udmabuf%d"
#define DEVICE_MAX_NUM      256
#define UDMABUF_DEBUG       1
#define STATIC_DEVICE_NUM   8

#if     defined(CONFIG_ARM) || defined(CONFIG_ARM64)
#define USE_VMA_FAULT       1
#else
#define USE_VMA_FAULT       0
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
 * sync_mode(synchronous mode) value
 */
#define SYNC_MODE_INVALID       (0x00)
#define SYNC_MODE_NONCACHED     (0x01)
#define SYNC_MODE_WRITECOMBINE  (0x02)
#define SYNC_MODE_DMACOHERENT   (0x03)
#define SYNC_MODE_MASK          (0x03)
#define SYNC_ALWAYS             (0x04)

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

static struct class*  udmabuf_sys_class     = NULL;
static DEFINE_IDA(    udmabuf_device_ida );
static dev_t          udmabuf_device_number = 0;
static int            dma_mask_bit          = 32;
static int            info_enable           = 1;

/**
 * struct udmabuf_driver_data - Udmabuf driver data structure
 */
struct udmabuf_driver_data {
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
    int                  sync_offset;
    size_t               sync_size;
    int                  sync_direction;
    bool                 sync_owner;
    int                  sync_for_cpu;
    int                  sync_for_device;
#if (USE_OF_RESERVED_MEM == 1)
    bool                 of_reserved_mem;
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_VMA_FAULT == 1))
    bool                 debug_vma;
#endif   
};

/**
 * struct udmabuf_static_device - Udmabuf static device structure
 */
struct udmabuf_static_device {
    struct platform_device* pdev;
    unsigned int            size;
};
struct udmabuf_static_device udmabuf_static_device_list[STATIC_DEVICE_NUM] = {};

/**
 * udmabuf_static_device_search()
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
 * udmabuf_sync_for_cpu() - call dma_sync_single_for_cpu() when (sync_for_cpu != 0)
 * @this:       Pointer to the udmabuf driver data structure.
 * Return:	Success(0)
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
        dma_sync_single_for_cpu(this->dma_dev, phys_addr, this->sync_size, direction);
        this->sync_for_cpu = 0;
        this->sync_owner   = 0;
    }
    return 0;
}

/**
 * udmabuf_sync_for_device() - call dma_sync_single_for_device() when (sync_for_device != 0)
 * @this:       Pointer to the udmabuf driver data structure.
 * Return:	Success(0)
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
        dma_sync_single_for_device(this->dma_dev, phys_addr, this->sync_size, direction);
        this->sync_for_device = 0;
        this->sync_owner      = 1;
    }
    return 0;
}

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
DEF_ATTR_SHOW(phys_addr      , "%pad\n" , &this->phys_addr                        );
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
#if ((UDMABUF_DEBUG == 1) && (USE_VMA_FAULT == 1))
DEF_ATTR_SHOW(debug_vma      , "%d\n"   , this->debug_vma                         );
DEF_ATTR_SET( debug_vma                 , 0, 1, NO_ACTION, NO_ACTION              );
#endif

static struct device_attribute udmabuf_device_attrs[] = {
  __ATTR(size           , 0444, udmabuf_show_size            , NULL                       ),
  __ATTR(phys_addr      , 0444, udmabuf_show_phys_addr       , NULL                       ),
  __ATTR(sync_mode      , 0664, udmabuf_show_sync_mode       , udmabuf_set_sync_mode      ),
  __ATTR(sync_offset    , 0664, udmabuf_show_sync_offset     , udmabuf_set_sync_offset    ),
  __ATTR(sync_size      , 0664, udmabuf_show_sync_size       , udmabuf_set_sync_size      ),
  __ATTR(sync_direction , 0664, udmabuf_show_sync_direction  , udmabuf_set_sync_direction ),
  __ATTR(sync_owner     , 0664, udmabuf_show_sync_owner      , NULL                       ),
  __ATTR(sync_for_cpu   , 0664, udmabuf_show_sync_for_cpu    , udmabuf_set_sync_for_cpu   ),
  __ATTR(sync_for_device, 0664, udmabuf_show_sync_for_device , udmabuf_set_sync_for_device),
#if ((UDMABUF_DEBUG == 1) && (USE_VMA_FAULT == 1))
  __ATTR(debug_vma      , 0664, udmabuf_show_debug_vma       , udmabuf_set_debug_vma      ),
#endif
  __ATTR_NULL,
};

#if (USE_DEV_GROUPS == 1)

static struct attribute *udmabuf_attrs[] = {
  &(udmabuf_device_attrs[0].attr),
  &(udmabuf_device_attrs[1].attr),
  &(udmabuf_device_attrs[2].attr),
  &(udmabuf_device_attrs[3].attr),
  &(udmabuf_device_attrs[4].attr),
  &(udmabuf_device_attrs[5].attr),
  &(udmabuf_device_attrs[6].attr),
  &(udmabuf_device_attrs[7].attr),
  &(udmabuf_device_attrs[8].attr),
#if ((UDMABUF_DEBUG == 1) && (USE_VMA_FAULT == 1))
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

#if (USE_VMA_FAULT == 1)
/**
 * udmabuf_driver_vma_open() - This is the driver open function.
 * @vma:        Pointer to the vm area structure.
 * Return:	None
 */
static void udmabuf_driver_vma_open(struct vm_area_struct* vma)
{
    struct udmabuf_driver_data* this = vma->vm_private_data;
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->dma_dev, "vma_open(virt_addr=0x%lx, offset=0x%lx)\n", vma->vm_start, vma->vm_pgoff<<PAGE_SHIFT);
}
#endif

#if (USE_VMA_FAULT == 1)
/**
 * udmabuf_driver_vma_close() - This is the driver close function.
 * @vma:        Pointer to the vm area structure.
 * Return:	None
 */
static void udmabuf_driver_vma_close(struct vm_area_struct* vma)
{
    struct udmabuf_driver_data* this = vma->vm_private_data;
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->dma_dev, "vma_close()\n");
}
#endif

#if (USE_VMA_FAULT == 1)
/**
 * _udmabuf_driver_vma_fault() - This is the driver nopage inline function.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      Success(=0) or error status(<0).
 */
static inline int _udmabuf_driver_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
{
    struct udmabuf_driver_data* this = vma->vm_private_data;
    struct page*  page_ptr           = NULL;
    unsigned long offset             = vmf->pgoff << PAGE_SHIFT;
    unsigned long phys_addr          = this->phys_addr + offset;
    unsigned long page_frame_num     = phys_addr  >> PAGE_SHIFT;
    unsigned long request_size       = 1          << PAGE_SHIFT;
    unsigned long available_size     = this->alloc_size -offset;

#if (LINUX_VERSION_CODE >= 0x040A00)
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->dma_dev,
                 "vma_fault(virt_addr=0x%lx, phys_addr=%pad)\n",
                 vmf->address,
                 &phys_addr
        );
#else
    if (UDMABUF_DEBUG_CHECK(this, debug_vma))
        dev_info(this->dma_dev,
                 "vma_fault(virt_addr=0x%lx, phys_addr=%pad)\n",
                 (long unsigned int)vmf->virtual_address,
                 &phys_addr
        );
#endif
    
    if (request_size > available_size) 
        return VM_FAULT_SIGBUS;

    if (!pfn_valid(page_frame_num))
        return VM_FAULT_SIGBUS;

    page_ptr = pfn_to_page(page_frame_num);
    get_page(page_ptr);
    vmf->page = page_ptr;
    return 0;
}

#if (LINUX_VERSION_CODE >= 0x040B00)
/**
 * udmabuf_driver_vma_fault() - This is the driver nopage function.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_driver_vma_fault(struct vm_fault* vmf)
{
    return _udmabuf_driver_vma_fault(vmf->vma, vmf);
}
#else
/**
 * udmabuf_driver_vma_fault() - This is the driver nopage function.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_driver_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
{
    return _udmabuf_driver_vma_fault(vma, vmf);
}
#endif

#endif /* #if (USE_VMA_FAULT == 1) */

#if (USE_VMA_FAULT == 1)
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
 * Return:      Success(=0) or error status(<0).
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
 * Return:      Success(=0) or error status(<0).
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
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_driver_file_mmap(struct file *file, struct vm_area_struct* vma)
{
    struct udmabuf_driver_data* this = file->private_data;

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
    vma->vm_pgoff        = 0;

#if (USE_VMA_FAULT == 1)
    {
        unsigned long page_frame_num = this->phys_addr >> PAGE_SHIFT;
        if (pfn_valid(page_frame_num)) {
            vma->vm_ops      = &udmabuf_driver_vm_ops;
            udmabuf_driver_vma_open(vma);
            return 0;
        }
    }
#endif

    return dma_mmap_coherent(this->dma_dev, vma, this->virt_addr, this->phys_addr, this->alloc_size);
}

/**
 * udmabuf_driver_file_read() - This is the driver read function.
 * @file:	Pointer to the file structure.
 * @buff:	Pointer to the user buffer.
 * @count:	The number of bytes to be written.
 * @ppos:	Pointer to the offset value.
 * Return:	Transferd size.
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
 * udmabuf_driver_file_write() - This is the driver write function.
 * @file:	Pointer to the file structure.
 * @buff:	Pointer to the user buffer.
 * @count:	The number of bytes to be written.
 * @ppos:	Pointer to the offset value
 * Return:	Transferd size.
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
 * udmabuf_driver_file_llseek() - This is the driver llseek function.
 * @file:	Pointer to the file structure.
 * @offset:	File offset to seek.
 * @whence:	Type of seek.
 * Return:	The new position.
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
 * udmabuf_driver_create() -  Create udmabuf driver data structure.
 * @name:       device name   or NULL.
 * @parent:     parent device or NULL.
 * @minor:	minor_number  or -1.
 * Return:      Pointer to the udmabuf driver data structure or NULL.
 */
static struct udmabuf_driver_data* udmabuf_driver_create(const char* name, struct device* parent, int minor)
{
    struct udmabuf_driver_data* this     = NULL;
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
     * create (udmabuf_driver_data*) this.
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
        cdev_init(&this->cdev, &udmabuf_driver_file_ops);
        this->cdev.owner = THIS_MODULE;
        if ((retval = cdev_add(&this->cdev, this->device_number, 1)) != 0) {
            printk(KERN_ERR "cdev_add() failed. return=%d\n", retval);
            goto failed;
        }
        done |= DONE_CHRDEV_ADD;
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
    if (parent != NULL) 
        this->dma_dev = parent;
    else
        this->dma_dev = this->sys_dev;
    return this;

 failed:
    if (done & DONE_CHRDEV_ADD   ) { cdev_del(&this->cdev); }
    if (done & DONE_DEVICE_CREATE) { device_destroy(udmabuf_sys_class, this->device_number);}
    if (done & DONE_ALLOC_MINOR  ) { ida_simple_remove(&udmabuf_device_ida, minor);}
    if (this != NULL)              { kfree(this); }
    return NULL;
}

/**
 * udmabuf_driver_setup() -  Setup the udmabuf driver data structure.
 * @this:       Pointer to the udmabuf driver data structure.
 * @size:	buffer size.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_driver_setup(struct udmabuf_driver_data* this, unsigned int size)
{
    if (!this)
        return -ENODEV;
    /*
     * setup dma_mask and coherent_dma_mask
     */
    if (this->dma_dev->dma_mask == NULL) {
        this->dma_dev->dma_mask = &this->dma_dev->coherent_dma_mask;
    }
    if (dma_set_mask(this->dma_dev, DMA_BIT_MASK(dma_mask_bit)) == 0) {
        dma_set_coherent_mask(this->dma_dev, DMA_BIT_MASK(dma_mask_bit));
    } else {
        printk(KERN_WARNING "dma_set_mask(DMA_BIT_MASK(%d)) failed\n", dma_mask_bit);
        dma_set_mask(this->dma_dev, DMA_BIT_MASK(32));
        dma_set_coherent_mask(this->dma_dev, DMA_BIT_MASK(32));
    }
    /*
     * setup buffer size and allocation size
     */
    this->size       = size;
    this->sync_size  = size;
    this->alloc_size = ((size + ((1 << PAGE_SHIFT) - 1)) >> PAGE_SHIFT) << PAGE_SHIFT;
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
 * udmabuf_driver_info() -  Print infomation the udmabuf driver data structure.
 * @this:       Pointer to the udmabuf driver data structure.
 */
static void udmabuf_driver_info(struct udmabuf_driver_data* this)
{
    dev_info(this->sys_dev, "major number   = %d\n"  , MAJOR(this->device_number));
    dev_info(this->sys_dev, "minor number   = %d\n"  , MINOR(this->device_number));
    dev_info(this->sys_dev, "phys address   = %pad\n", &this->phys_addr);
    dev_info(this->sys_dev, "buffer size    = %zu\n" , this->alloc_size);
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
    dev_info(this->sys_dev, "dma coherent   = %d\n"  , is_device_dma_coherent(this->dma_dev));
#endif
}

/**
 * udmabuf_driver_destroy() -  Remove the udmabuf driver data structure.
 * @this:       Pointer to the udmabuf driver data structure.
 * Return:      Success(=0) or error status(<0).
 *
 * Unregister the device after releasing the resources.
 */
static int udmabuf_driver_destroy(struct udmabuf_driver_data* this)
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
 * udmabuf_platform_driver_probe() -  Probe call for the device.
 * @pdev:	handle to the platform device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * It does all the memory allocation and registration for the device.
 */
static int udmabuf_platform_driver_probe(struct platform_device *pdev)
{
    int                         retval       = 0;
    unsigned int                size         = 0;
    int                         minor_number = -1;
    struct udmabuf_driver_data* driver_data  = NULL;
    const char*                 device_name  = NULL;

    dev_dbg(&pdev->dev, "driver probe start.\n");

    if (udmabuf_static_device_search(pdev, &minor_number, &size) == 0) {
        int          status;
        unsigned int number;

        status = of_property_read_u32(pdev->dev.of_node, "size", &size);
        if (status != 0) {
            dev_err(&pdev->dev, "invalid property size. status=%d\n", status);
            retval = -ENODEV;
            goto failed;
        }

        status = of_property_read_u32(pdev->dev.of_node, "minor-number", &number);
        minor_number = (status == 0) ? number : -1;

        device_name = of_get_property(pdev->dev.of_node, "device-name", NULL);
        if (IS_ERR_OR_NULL(device_name)) {
            if (minor_number < 0)
                device_name = dev_name(&pdev->dev);
            else
                device_name = NULL;
        }
    }
    /*
     * create udmabuf_driver
     */
    driver_data = udmabuf_driver_create(device_name, &pdev->dev, minor_number);
    if (IS_ERR_OR_NULL(driver_data)) {
        retval = PTR_ERR(driver_data);
        dev_err(&pdev->dev, "driver create failed. return=%d.\n", retval);
        driver_data = NULL;
        retval = (retval == 0) ? -EINVAL : retval;
        goto failed;
    }
    dev_set_drvdata(&pdev->dev, driver_data);
    /*
     * of_reserved_mem_device_init()
     */
#if (USE_OF_RESERVED_MEM == 1)
    if (pdev->dev.of_node != NULL) {
        retval = of_reserved_mem_device_init(&pdev->dev);
        if (retval == 0) {
            driver_data->of_reserved_mem = 1;
        } else if (retval != -ENODEV) {
            dev_err(&pdev->dev, "of_reserved_mem_device_init failed. return=%d\n", retval);
            goto failed;
        }
    }
#endif
    /*
     * setup udmabuf_driver
     */
    retval = udmabuf_driver_setup(driver_data, size);
    if (retval) {
        dev_err(&pdev->dev, "driver setup failed. return=%d\n", retval);
        goto failed;
    }
    
    if (info_enable) {
        udmabuf_driver_info(driver_data);
        dev_info(&pdev->dev, "driver installed.\n");
    }
    return 0;

failed:
    if (driver_data != NULL) {
        int status;
        status = udmabuf_driver_destroy(driver_data);
        if (status) {
            dev_err(&pdev->dev, "driver destroy failed. return=%d.\n", status);
        }
#if (USE_OF_RESERVED_MEM == 1)
        if (driver_data->of_reserved_mem == 1) {
            of_reserved_mem_device_release(&pdev->dev);
        }
#endif
    }
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
    struct udmabuf_driver_data* this   = dev_get_drvdata(&pdev->dev);
    int                         retval = 0;

    dev_dbg(&pdev->dev, "driver remove start.\n");

    if (this != NULL) {
        if ((retval = udmabuf_driver_destroy(this)) != 0)
            return retval;
        dev_set_drvdata(&pdev->dev, NULL);
#if (USE_OF_RESERVED_MEM == 1)
        if (this->of_reserved_mem) {
            of_reserved_mem_device_release(&pdev->dev);
        }
#endif
    }
    if (info_enable) {
        dev_info(&pdev->dev, "driver removed.\n");
    }
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

/**
 * udmabuf_static_device_create() - Create static udmabuf device.
 * @id:	        device id.
 * @size:	buffer size.
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

#if (USE_OF_DMA_CONFIG == 1)
#if (LINUX_VERSION_CODE >= 0x040C00)
    retval = of_dma_configure(&pdev->dev, NULL);
    if (retval != 0) {
        dev_err(&pdev->dev, "of_dma_configure failed. return=%d\n", retval);
    }
#else
    of_dma_configure(&pdev->dev, NULL);
#endif
#endif

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
 * static udmabuf device description and functions.
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

static void udmabuf_static_device_destory_all(void)
{
    int id;
    for (id = 0; id < STATIC_DEVICE_NUM; id++) {
        if (udmabuf_static_device_list[id].pdev != NULL) {
            platform_device_del(udmabuf_static_device_list[id].pdev);
            platform_device_put(udmabuf_static_device_list[id].pdev);
            udmabuf_static_device_list[id].pdev = NULL;
            udmabuf_static_device_list[id].size = 0;
        }
    }
}

/**
 * other module parameters
 */
module_param(     info_enable , int, S_IRUGO);
MODULE_PARM_DESC( info_enable , "udmabuf install/uninstall infomation enable");

module_param(     dma_mask_bit, int, S_IRUGO);
MODULE_PARM_DESC( dma_mask_bit, "udmabuf dma mask bit(default=32)");

static bool udmabuf_platform_driver_done = 0;

/**
 * udmabuf_module_cleanup()
 */
static void udmabuf_module_cleanup(void)
{
    udmabuf_static_device_destory_all();
    if (udmabuf_platform_driver_done ){platform_driver_unregister(&udmabuf_platform_driver);}
    if (udmabuf_sys_class     != NULL){class_destroy(udmabuf_sys_class);}
    if (udmabuf_device_number != 0   ){unregister_chrdev_region(udmabuf_device_number, 0);}
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
    SET_SYS_CLASS_ATTRIBUTES(udmabuf_sys_class);

    udmabuf_static_device_create_all();

    retval = platform_driver_register(&udmabuf_platform_driver);
    if (retval) {
        printk(KERN_ERR "%s: couldn't register platform driver. return=%d\n", DRIVER_NAME, retval);
    } else {
        udmabuf_platform_driver_done = 1;
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

MODULE_AUTHOR("ikwzm");
MODULE_DESCRIPTION("User space mappable DMA buffer device driver");
MODULE_LICENSE("Dual BSD/GPL");
