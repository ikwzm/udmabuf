/*********************************************************************************
 *
 *       Copyright (C) 2015-2025 Ichiro Kawazome
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
 * DOC: Udmabuf Constants.
 */

MODULE_DESCRIPTION("User space mappable DMA buffer device driver");
MODULE_AUTHOR("ikwzm");
MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_VERSION     "5.2.0"
#define DRIVER_NAME        "u-dma-buf"
#define DEVICE_NAME_FORMAT "udmabuf%d"
#define DEVICE_MAX_NUM      256

#if     defined(U_DMA_BUF_CONFIG) && (U_DMA_BUF_CONFIG != 0)
#define UDMABUF_CONFIG      1
#elif   defined(U_DMA_BUF_CONFIG) && (U_DMA_BUF_CONFIG == 0)
#define UDMABUF_CONFIG      0
#elif   defined(CONFIG_U_DMA_BUF_CONFIG)
#define UDMABUF_CONFIG      1
#else   
#define UDMABUF_CONFIG      0
#endif

#if     (UDMABUF_CONFIG == 0)
#define UDMABUF_DEBUG       1
#elif   defined(U_DMA_BUF_DEBUG) && (U_DMA_BUF_DEBUG != 0)
#define UDMABUF_DEBUG       1
#elif   defined(U_DMA_BUF_DEBUG) && (U_DMA_BUF_DEBUG == 0)
#define UDMABUF_DEBUG       0
#elif   defined(CONFIG_U_DMA_BUF_DEBUG)
#define UDMABUF_DEBUG       1
#else
#define UDMABUF_DEBUG       0
#endif

#if     (UDMABUF_CONFIG == 0)
#define USE_QUIRK_MMAP      1
#elif   defined(U_DMA_BUF_QUIRK_MMAP) && (U_DMA_BUF_QUIRK_MMAP != 0)
#define USE_QUIRK_MMAP      1
#elif   defined(U_DMA_BUF_QUIRK_MMAP) && (U_DMA_BUF_QUIRK_MMAP == 0)
#define USE_QUIRK_MMAP      0
#elif   defined(CONFIG_U_DMA_BUF_QUIRK_MMAP)
#define USE_QUIRK_MMAP      1
#else
#define USE_QUIRK_MMAP      0
#endif

#if     (UDMABUF_CONFIG == 0)
#define IN_KERNEL_FUNCTIONS 1
#elif   defined(U_DMA_BUF_IN_KERNEL_FUNCTIONS) && (U_DMA_BUF_IN_KERNEL_FUNCTIONS != 0)
#define IN_KERNEL_FUNCTIONS 1
#elif   defined(U_DMA_BUF_IN_KERNEL_FUNCTIONS) && (U_DMA_BUF_IN_KERNEL_FUNCTIONS == 0)
#define IN_KERNEL_FUNCTIONS 0
#elif   defined(CONFIG_U_DMA_BUF_IN_KERNEL_FUNCTIONS)
#define IN_KERNEL_FUNCTIONS 1
#else
#define IN_KERNEL_FUNCTIONS 0
#endif

#if     (UDMABUF_CONFIG == 0)
#define IOCTL_VERSION       2
#elif   defined(U_DMA_BUF_IOCTL) && (U_DMA_BUF_IOCTL >= 2)
#define IOCTL_VERSION       2
#elif   defined(U_DMA_BUF_IOCTL) && (U_DMA_BUF_IOCTL == 1)
#define IOCTL_VERSION       1
#elif   defined(U_DMA_BUF_IOCTL) && (U_DMA_BUF_IOCTL == 0)
#define IOCTL_VERSION       0
#elif   defined(CONFIG_U_DMA_BUF_IOCTL)
#define IOCTL_VERSION       2
#else
#define IOCTL_VERSION       0
#endif

#if     (UDMABUF_CONFIG == 0)
#define USE_DMA_BUF_EXPORT  1
#elif   defined(U_DMA_BUF_EXPORT) && (U_DMA_BUF_EXPORT != 0)
#define USE_DMA_BUF_EXPORT  1
#elif   defined(U_DMA_BUF_EXPORT) && (U_DMA_BUF_EXPORT == 0)
#define USE_DMA_BUF_EXPORT  0
#elif   defined(CONFIG_U_DMA_BUF_EXPORT)
#define USE_DMA_BUF_EXPORT  1
#else
#define USE_DMA_BUF_EXPORT  0
#endif

#if     (USE_DMA_BUF_EXPORT == 1)
#ifndef CONFIG_DMA_SHARED_BUFFER
#pragma message("Warning: NO USE DMA-BUF EXPORT because CONFIG_DMA_SHARED_BUFFER is not set")
#undef  USE_DMA_BUF_EXPORT
#define USE_DMA_BUF_EXPORT  0
#endif
#if     (IOCTL_VERSION < 2)
#if     (defined(U_DMA_BUF_IOCTL))
#pragma message("Warning: NO USE DMA-BUF EXPORT because U_DMA_BUF_IOCTL is less than 2")
#else
#pragma message("Warning: NO USE DMA-BUF EXPORT because CONFIG_U_DMA_BUF_IOCTL is not set")
#endif
#undef  USE_DMA_BUF_EXPORT
#define USE_DMA_BUF_EXPORT  0
#endif
#endif

#if     (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <linux/dma-map-ops.h>
#define IS_DMA_COHERENT(dev) dev_is_dma_coherent(dev)
#elif   (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0))
#include <linux/dma-noncoherent.h>
#define IS_DMA_COHERENT(dev) dev_is_dma_coherent(dev)
#elif   ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)) && (defined(CONFIG_ARM) || defined(CONFIG_ARM64)))
#define IS_DMA_COHERENT(dev) is_device_dma_coherent(dev)
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

#if     ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)) && (USE_QUIRK_MMAP == 1))
#define USE_QUIRK_MMAP_PAGE 1
#include <linux/dma-direct.h>
#else
#define USE_QUIRK_MMAP_PAGE 0
#endif

#if     (USE_OF_RESERVED_MEM == 1)
#include <linux/of_reserved_mem.h>
#endif

#if     (USE_DMA_BUF_EXPORT == 1)
#include <linux/dma-buf.h>
#if     (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13 ,0))
MODULE_IMPORT_NS("DMA_BUF");
#elif   (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16 ,0))
MODULE_IMPORT_NS(DMA_BUF);
#endif
#endif

#ifndef U64_MAX
#define U64_MAX ((u64)~0ULL)
#endif

/**
 * DOC: Udmabuf Static Variables.
 *
 * * udmabuf_sys_class - udmabuf system class
 * * init_enable       - udmabuf install/uninstall infomation enable
 * * dma_mask_bit      - udmabuf dma mask bit
 * * bind              - udmabuf bind device name
 * * quirk_mmap_mode   - udmabuf default quirk mmap mode 
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
#define           DMA_INFO_ENABLE     (info_enable & 0x02)
#define           CONFIG_INFO_ENABLE  (info_enable & 0x04)

/**
 * dma_mask_bit module parameter
 */
static int        dma_mask_bit = 32;
module_param(     dma_mask_bit, int, S_IRUGO);
MODULE_PARM_DESC( dma_mask_bit, "udmabuf dma mask bit(default=32)");

/**
 * bind module parameter
 */
static char*      bind = NULL;
module_param(     bind, charp, S_IRUGO);
MODULE_PARM_DESC( bind, "bind device name. exp pci/0000:00:20:0");

#if (USE_QUIRK_MMAP == 1)
/**
 * quirk_mmap_mode       - udmabuf default quirk mmap mode 
 */
#define  QUIRK_MMAP_MODE_UNDEFINED   0
#define  QUIRK_MMAP_MODE_ALWAYS_OFF  1
#define  QUIRK_MMAP_MODE_ALWAYS_ON   2
#define  QUIRK_MMAP_MODE_AUTO        3
#define  QUIRK_MMAP_MODE_PAGE        4
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
static int        quirk_mmap_mode = QUIRK_MMAP_MODE_ALWAYS_ON;
#define           QUIRK_MMAP_MODE_PARM_DESC_DEFAULT "(default=2)"
#else
static int        quirk_mmap_mode = QUIRK_MMAP_MODE_AUTO;
#define           QUIRK_MMAP_MODE_PARM_DESC_DEFAULT "(default=3)"
#endif
#if (USE_QUIRK_MMAP_PAGE == 1)
#define           QUIRK_MMAP_MODE_PARM_DESC_USAGE "(1:off,2:on,3:auto,4:page)"
#else
#define           QUIRK_MMAP_MODE_PARM_DESC_USAGE "(1:off,2:on,3:auto)"
#endif
module_param(     quirk_mmap_mode, int, S_IRUGO);
MODULE_PARM_DESC( quirk_mmap_mode, "udmabuf default quirk mmap mode" QUIRK_MMAP_MODE_PARM_DESC_USAGE QUIRK_MMAP_MODE_PARM_DESC_DEFAULT);
#endif /* #if (USE_QUIRK_MMAP == 1) */

/**
 * DOC: Udmabuf Object Data Structure.
 *
 * This section defines the structure of udmabuf device.
 *
 */

/**
 * struct udmabuf_object - udmabuf object structure.
 */
struct udmabuf_object {
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
#if (USE_QUIRK_MMAP == 1)
    int                  quirk_mmap_mode;
#if (USE_QUIRK_MMAP_PAGE == 1)
    pgoff_t              pagecount;
    struct page**        pages;
#endif    
#endif
#if (USE_DMA_BUF_EXPORT == 1)
    struct list_head     export_dma_buf_list;
    struct mutex         export_dma_buf_list_sem;
#endif
#if (USE_OF_RESERVED_MEM == 1)
    bool                 of_reserved_mem;
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_QUIRK_MMAP == 1))
    int                  debug_vma;
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_DMA_BUF_EXPORT == 1))
    bool                 debug_export;
#endif
};

#if ((UDMABUF_DEBUG == 1) && (USE_QUIRK_MMAP == 1))
#define UDMABUF_VMA_DEBUG(this,bit) ((this->debug_vma & (1<<bit)) != 0)
#else
#define UDMABUF_VMA_DEBUG(this,bit) (0)
#endif

#if ((UDMABUF_DEBUG == 1) && (USE_DMA_BUF_EXPORT == 1))
#define UDMABUF_EXPORT_DEBUG(this) (this->debug_export)
#else
#define UDMABUF_EXPORT_DEBUG(this) (0)
#endif

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
 * DOC: Udmabuf System Class Device File Description.
 *
 * This section define the device file created in system class when udmabuf is 
 * loaded into the kernel.
 *
 * The device file created in system class is as follows.
 *
 * * /sys/class/u-dma-buf/<device-name>/driver_version
 * * /sys/class/u-dma-buf/<device-name>/phys_addr
 * * /sys/class/u-dma-buf/<device-name>/size
 * * /sys/class/u-dma-buf/<device-name>/sync_mode
 * * /sys/class/u-dma-buf/<device-name>/sync_offset
 * * /sys/class/u-dma-buf/<device-name>/sync_size
 * * /sys/class/u-dma-buf/<device-name>/sync_direction
 * * /sys/class/u-dma-buf/<device-name>/sync_owner
 * * /sys/class/u-dma-buf/<device-name>/sync_for_cpu
 * * /sys/class/u-dma-buf/<device-name>/sync_for_device
 * * /sys/class/u-dma-buf/<device-name>/dma_coherent
 * * /sys/class/u-dma-buf/<device-name>/quirk_mmap_mode
 * * /sys/class/u-dma-buf/<device-name>/ioctl_version
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
 * @this:       Pointer to the udmabuf object.
 * @command     sync command (this->sync_for_cpu or this->sync_for_device)
 * @phys_addr   Pointer to the phys_addr for dma_sync_single_for_...()
 * @size        Pointer to the size for dma_sync_single_for_...()
 * @direction   Pointer to the direction for dma_sync_single_for_...()
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_sync_command_argments(
    struct udmabuf_object      *this     ,
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
 * @this:       Pointer to the udmabuf object.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_sync_for_cpu(struct udmabuf_object* this)
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
 * @this:       Pointer to the udmabuf object.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_sync_for_device(struct udmabuf_object* this)
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
    struct udmabuf_object* this = dev_get_drvdata(dev);      \
    if (mutex_lock_interruptible(&this->sem) != 0)           \
        return -ERESTARTSYS;                                 \
    status = sprintf(buf, __format, (__value));              \
    mutex_unlock(&this->sem);                                \
    return status;                                           \
}

static inline int NO_ACTION(struct udmabuf_object* this){return 0;}

#define DEF_ATTR_SET(__attr_name, __min, __max, __pre_action, __post_action) \
static ssize_t udmabuf_set_ ## __attr_name(struct device *dev, struct device_attribute *attr, const char *buf, size_t size) \
{ \
    ssize_t       status; \
    u64           value;  \
    struct udmabuf_object* this = dev_get_drvdata(dev);                      \
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
#if (USE_QUIRK_MMAP == 1)
DEF_ATTR_SHOW(quirk_mmap_mode, "%d\n"    , this->quirk_mmap_mode                          );
#endif
#if defined(IS_DMA_COHERENT)
DEF_ATTR_SHOW(dma_coherent   , "%d\n"    , IS_DMA_COHERENT(this->dma_dev)                 );
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_QUIRK_MMAP == 1))
DEF_ATTR_SHOW(debug_vma      , "%d\n"    , this->debug_vma                                );
DEF_ATTR_SET( debug_vma                  , 0, 3,        NO_ACTION, NO_ACTION              );
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_DMA_BUF_EXPORT == 1))
DEF_ATTR_SHOW(debug_export   , "%d\n"    , this->debug_export                             );
DEF_ATTR_SET( debug_export               , 0, 1,        NO_ACTION, NO_ACTION              );
#endif

#if (IOCTL_VERSION > 0)
DEF_ATTR_SHOW(ioctl_version  , "%d\n"    , (int)(IOCTL_VERSION)                           );
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
#if (USE_QUIRK_MMAP == 1)
  __ATTR(quirk_mmap_mode, 0444, udmabuf_show_quirk_mmap_mode , NULL                       ),
#endif
#if defined(IS_DMA_COHERENT)
  __ATTR(dma_coherent   , 0444, udmabuf_show_dma_coherent    , NULL                       ),
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_QUIRK_MMAP == 1))
  __ATTR(debug_vma      , 0664, udmabuf_show_debug_vma       , udmabuf_set_debug_vma      ),
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_DMA_BUF_EXPORT == 1))
  __ATTR(debug_export   , 0664, udmabuf_show_debug_export    , udmabuf_set_debug_export   ),
#endif
#if (IOCTL_VERSION > 0)
  __ATTR(ioctl_version  , 0444, udmabuf_show_ioctl_version   , NULL                       ),
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

#if (USE_QUIRK_MMAP == 1)
/**
 * DOC: Udmabuf Object VM Area Operations for quirk-mmap.
 *
 * This section defines the operation of vm when mmap-ed the udmabuf object.
 *
 * * udmabuf_mmap_vma_open()       - udmabuf object quirk-mmap vm area open operation.
 * * udmabuf_mmap_vma_close()      - udmabuf object quirk-mmap vm area close operation.
 * * udmabuf_mmap_vma_fault()      - udmabuf object quirk-mmap vm area fault operation.
 * * udmabuf_mmap_vm_ops           - udmabuf object quirk-mmap vm operation table.
 * * udmabuf_set_quirk_mmap_mode() - set quirk-mmap in udmabuf object.
 * * udmabuf_quirk_mmap_enable()   - check if udmabuf object can use quirk-mmap.
 */
/**
 * udmabuf_mmap_vma_open() - udmabuf device file mmap vm area open operation.
 * @vma:        Pointer to the vm area structure.
 * Return:      None
 */
static void udmabuf_mmap_vma_open(struct vm_area_struct* vma)
{
    struct udmabuf_object* this = vma->vm_private_data;
    if (UDMABUF_VMA_DEBUG(this,0))
        dev_info(this->dma_dev, "%s(virt_addr=0x%lx, offset=0x%lx, flags=0x%lx)\n", __func__, vma->vm_start, vma->vm_pgoff<<PAGE_SHIFT, vma->vm_flags);
}

/**
 * udmabuf_mmap_vma_close() - udmabuf device file mmap vm area close operation.
 * @vma:        Pointer to the vm area structure.
 * Return:      None
 */
static void udmabuf_mmap_vma_close(struct vm_area_struct* vma)
{
    struct udmabuf_object* this = vma->vm_private_data;
    if (UDMABUF_VMA_DEBUG(this,0))
        dev_info(this->dma_dev, "%s()\n", __func__);
}

/**
 * VM_FAULT_RETURN_TYPE - Type of udmabuf_mmap_vma_fault() return value.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0))
typedef vm_fault_t VM_FAULT_RETURN_TYPE;
#else
typedef int        VM_FAULT_RETURN_TYPE;
#endif

/**
 * _udmabuf_mmap_vma_fault() - udmabuf device file mmap vm area fault operation.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      VM_FAULT_RETURN_TYPE (Success(=0) or error status(!=0)).
 */
static inline VM_FAULT_RETURN_TYPE _udmabuf_mmap_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
{
    struct udmabuf_object* this  = vma->vm_private_data;
    unsigned long offset         = vmf->pgoff << PAGE_SHIFT;
    unsigned long phys_addr      = this->phys_addr + offset;
    unsigned long page_frame_num = phys_addr  >> PAGE_SHIFT;
    unsigned long request_size   = 1UL        << PAGE_SHIFT;
    unsigned long available_size = this->alloc_size -offset;
    unsigned long virt_addr;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
    virt_addr = vmf->address;
#else
    virt_addr = (unsigned long)vmf->virtual_address;
#endif

    if (UDMABUF_VMA_DEBUG(this,1))
        dev_info(this->dma_dev,
                 "vma_fault(virt_addr=%pad, phys_addr=%pad)\n", &virt_addr, &phys_addr
        );

    if (request_size > available_size)
        return VM_FAULT_SIGBUS;

    if (!pfn_valid(page_frame_num))
        return VM_FAULT_SIGBUS;

#if (USE_QUIRK_MMAP_PAGE == 1)
    if (this->pages != NULL) {
        if (vmf->pgoff >= this->pagecount)
            return VM_FAULT_SIGBUS;
        return vmf_insert_page(vma, virt_addr, this->pages[vmf->pgoff]);
    }
#endif
    
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
 * udmabuf_mmap_vma_fault() - udmabuf device file mmap vm area fault operation.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      VM_FAULT_RETURN_TYPE (Success(=0) or error status(!=0)).
 */
static VM_FAULT_RETURN_TYPE udmabuf_mmap_vma_fault(struct vm_fault* vmf)
{
    return _udmabuf_mmap_vma_fault(vmf->vma, vmf);
}
#else
/**
 * udmabuf_mmap_vma_fault() - udmabuf device file mmap vm area fault operation.
 * @vma:        Pointer to the vm area structure.
 * @vfm:        Pointer to the vm fault structure.
 * Return:      VM_FAULT_RETURN_TYPE (Success(=0) or error status(!=0)).
 */
static VM_FAULT_RETURN_TYPE udmabuf_mmap_vma_fault(struct vm_area_struct* vma, struct vm_fault* vmf)
{
    return _udmabuf_mmap_vma_fault(vma, vmf);
}
#endif

/**
 * udmabuf device file mmap vm operation table.
 */
static const struct vm_operations_struct udmabuf_mmap_vm_ops = {
    .open    = udmabuf_mmap_vma_open ,
    .close   = udmabuf_mmap_vma_close,
    .fault   = udmabuf_mmap_vma_fault,
};

/**
 * udmabuf_check_quirk_mmap_mode() - check quirk-mmap mode.
 * @value:      quirk-mmap mode.
 * Return:      Valid(true) or NotValid(false).
 */
static inline bool udmabuf_check_quirk_mmap_mode(int value)
{
    bool is_valid = false;
    is_valid |= (value == QUIRK_MMAP_MODE_ALWAYS_OFF);
    is_valid |= (value == QUIRK_MMAP_MODE_ALWAYS_ON );
    is_valid |= (value == QUIRK_MMAP_MODE_AUTO      );
#if (USE_QUIRK_MMAP_PAGE == 1)
    is_valid |= (value == QUIRK_MMAP_MODE_PAGE      );
#endif
    return is_valid;
}

/**
 * udmabuf_set_quirk_mmap_mode() - set quirk-mmap in udmabuf object.
 * @this:       Pointer to the udmabuf object.
 * @value:      quirk-mmap mode.
 * Return:      Success(=0) or error status(<0).
 */
static inline int udmabuf_set_quirk_mmap_mode(struct udmabuf_object* this, int value)
{
    if (!this)
        return -ENODEV;

    if (udmabuf_check_quirk_mmap_mode(value) == false)
        return -EINVAL;

    this->quirk_mmap_mode = value;
    return 0;
}

/**
 * udmabuf_quirk_mmap_enable() - check if udmabuf object can use quirk-mmap.
 * @this:       Pointer to the udmabuf object.
 * Return:      Enable(=True) or Disable(=False).
 */
static bool udmabuf_quirk_mmap_enable(struct udmabuf_object* this)
{
    if (!this)
        return true;

#if (USE_QUIRK_MMAP_PAGE == 1)
    if (this->quirk_mmap_mode == QUIRK_MMAP_MODE_PAGE      )
        return true;
#endif
    if (this->quirk_mmap_mode == QUIRK_MMAP_MODE_ALWAYS_OFF)
        return false;
    if (this->quirk_mmap_mode == QUIRK_MMAP_MODE_ALWAYS_ON )
        return true;
#if defined(IS_DMA_COHERENT)
    if (this->quirk_mmap_mode == QUIRK_MMAP_MODE_AUTO      )
        return !IS_DMA_COHERENT(this->dma_dev);
#endif
    return true;
}
#endif /* #if (USE_QUIRK_MMAP == 1) */

/**
 * DOC: Udmabuf Object Memory Map Operation.
 */
/**
 * _PGPROT_NONCACHED     - vm_page_prot value when sync_mode is SYNC_MODE_NONCACHED
 * _PGPROT_WRITECOMBINE  - vm_page_prot value when sync_mode is SYNC_MODE_WRITECOMBINE
 * _PGPROT_DMACOHERENT   - vm_page_prot value when sync_mode is SYNC_MODE_DMACOHERENT
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

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0))
static inline void vm_flags_set(struct vm_area_struct* vma, vm_flags_t flags)
{
    vma->vm_flags |=  (flags);
}
static inline void vm_flags_mod(struct vm_area_struct* vma, vm_flags_t set, vm_flags_t clear)
{
    vma->vm_flags |=  (set);
    vma->vm_flags &= ~(clear);
}
#endif

/**
 * udmabuf_object_mmap() - udmabuf object memory map operation.
 * @this:       Pointer to the udmabuf object.
 * @vma:        Pointer to the vm area structure.
 * @force_sync  Force sync flag.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_object_mmap(struct udmabuf_object* this, struct vm_area_struct* vma, bool force_sync)
{
    if (vma->vm_pgoff + vma_pages(vma) > (this->alloc_size >> PAGE_SHIFT))
        return -EINVAL;

    if ((force_sync == true) | (this->sync_mode & SYNC_ALWAYS)) {
        switch (this->sync_mode & SYNC_MODE_MASK) {
            case SYNC_MODE_NONCACHED :
                vm_flags_set(vma, VM_IO);
                vma->vm_page_prot = _PGPROT_NONCACHED(vma->vm_page_prot);
                break;
            case SYNC_MODE_WRITECOMBINE :
                vm_flags_set(vma, VM_IO);
                vma->vm_page_prot = _PGPROT_WRITECOMBINE(vma->vm_page_prot);
                break;
            case SYNC_MODE_DMACOHERENT :
                vm_flags_set(vma, VM_IO);
                vma->vm_page_prot = _PGPROT_DMACOHERENT(vma->vm_page_prot);
                break;
            default :
                break;
        }
    }

#if (USE_QUIRK_MMAP == 1)
    if (udmabuf_quirk_mmap_enable(this))
    {
        unsigned long page_frame_num = (this->phys_addr >> PAGE_SHIFT) + vma->vm_pgoff;
#if (USE_QUIRK_MMAP_PAGE == 1)
        if (this->pages != NULL) {
            vm_flags_mod(vma, VM_MIXEDMAP, VM_PFNMAP);
            vma->vm_ops          = &udmabuf_mmap_vm_ops;
            vma->vm_private_data = this;
            udmabuf_mmap_vma_open(vma);
            return 0;
        }
#endif        
        if (pfn_valid(page_frame_num)) {
            vm_flags_mod(vma, VM_PFNMAP, VM_MIXEDMAP);
            vma->vm_ops          = &udmabuf_mmap_vm_ops;
            vma->vm_private_data = this;
            udmabuf_mmap_vma_open(vma);
            return 0;
        }
    }
#endif

    return dma_mmap_coherent(this->dma_dev, vma, this->virt_addr, this->phys_addr, this->alloc_size);
}

/**
 * DOC: Udmabuf Export DMA-BUF Operations.
 *
 * struct udmabuf_export_entry    - udmabuf export dma-buf entry structure.
 * udmabuf_export_dma_buf_map()   - udmabuf export dma-buf map operation.
 * udmabuf_export_dma_buf_unmap() - udmabuf export dma-buf unmap operation.
 * udmabuf_export_release()       - udmabuf export dma-buf release operation.
 * udmabuf_export_mmap()          - udmabuf export dma-buf memory map operation.
 * udmabuf_export_begin_cpu()     - udmabuf export dma-buf begin_cpu operation.
 * udmabuf_export_end_cpu()       - udmabuf export dma-buf end_cpu operation.
 * udmabuf_export_ops             - udmabuf export dma-buf operation table.
 * udmabuf_export_create_entry()  - Create udmabuf export dma-buf entry and add list.
 */
#if (USE_DMA_BUF_EXPORT == 1)
struct udmabuf_export_entry {
    struct udmabuf_object* object;
    struct udmabuf_object  object_data;
    struct dma_buf*        dma_buf;
    int                    fd;
    bool                   force_sync;  
    u64                    offset;
    size_t                 size;
    struct list_head       list;
};

#if ((LINUX_VERSION_CODE < KERNEL_VERSION(5, 8 ,0)) && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5 ,0)) || (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4 ,233))
static inline int dma_map_sgtable(struct device* dev, struct sg_table* sgt, enum dma_data_direction direction, unsigned long attrs)
{
    int nents;
    nents = dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, direction, attrs);
    if (nents <= 0)
        return -EINVAL;
    sgt->nents = nents;
    return 0;
}
static inline void dma_unmap_sgtable(struct device* dev, struct sg_table* sgt, enum dma_data_direction direction, unsigned long attrs)
{
    dma_unmap_sg_attrs(dev, sgt->sgl, sgt->orig_nents, direction, attrs);
}
#endif

/**
 * udmabuf_export_dma_buf_map() -  udmabuf export dma-buf map operation.
 * @attachment: Pointer to dma-buf attachment structure.
 * @direction:  length of range for cpu access.
 * Return:      handle to scatter gather table(>=0) or error status(<0).
 */
static struct sg_table *udmabuf_export_dma_buf_map(struct dma_buf_attachment* attachment,
                                           enum   dma_data_direction  direction)
{
    struct dma_buf*              dma_buf = attachment->dmabuf;
    struct udmabuf_export_entry* entry;
    struct udmabuf_object*       this;
    unsigned int                 done    = 0;
    const unsigned int           DONE_ALLOC_SG_TABLE = (1 << 0);
    const unsigned int           DONE_GET_SG_TABLE   = (1 << 1);
    const unsigned int           DONE_MAP_SG_TABLE   = (1 << 2);
    struct sg_table*             sg_table;
    int                          retval;

    if (dma_buf == NULL)
        return ERR_PTR(-ENODEV);

    if ((entry = dma_buf->priv) == NULL)
        return ERR_PTR(-ENODEV);

    this = &entry->object_data;

    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) start.\n", __func__, entry->fd);

    sg_table = kzalloc(sizeof(*sg_table), GFP_KERNEL);
    if (IS_ERR_OR_NULL(sg_table)) {
        retval   = PTR_ERR(sg_table);
        dev_err( this->sys_dev, "%s(fd=%d): kzalloc() failed. return=%d\n", __func__, entry->fd, retval);
        goto failed;
    }
    done |= DONE_ALLOC_SG_TABLE;

    retval = dma_get_sgtable(this->dma_dev, sg_table, this->virt_addr, this->phys_addr, this->alloc_size);
    if (retval) {
        dev_err( this->sys_dev, "%s(fd=%d): dma_get_sgtable() failed. return=%d\n", __func__, entry->fd, retval);
        goto failed;
    }
    done |= DONE_GET_SG_TABLE;

    retval = dma_map_sgtable(attachment->dev, sg_table, direction, 0);
    if (retval) {
        dev_err( this->sys_dev, "%s(fd=%d): dma_map_sgtable() failed. return=%d\n", __func__, entry->fd, retval);
        goto failed;
    }
    done |= DONE_MAP_SG_TABLE;

    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) done.\n", __func__, entry->fd);

    return sg_table;

 failed:
    if (done & DONE_MAP_SG_TABLE  ) {dma_unmap_sgtable(attachment->dev, sg_table, direction, 0);}
    if (done & DONE_GET_SG_TABLE  ) {sg_free_table(sg_table);};
    if (done & DONE_ALLOC_SG_TABLE) {kfree(sg_table);}
    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) failed. return=%d\n", __func__, entry->fd, retval);
    return ERR_PTR(retval);
}

/**
 * udmabuf_export_dma_buf_unmap() - udmabuf export dma-buf unmap operation.
 * @attachment: Pointer to dma-buf attachment structure.
 * @sg_table:   scatter gather table.
 * @direction:  length of range for cpu access.
 */
static void udmabuf_export_dma_buf_unmap(struct dma_buf_attachment* attachment,
			         struct sg_table*           sg_table,
			         enum   dma_data_direction  direction)
{
    struct dma_buf*              dma_buf = attachment->dmabuf;
    struct udmabuf_export_entry* entry;
    struct udmabuf_object*       this;

    if (dma_buf == NULL)
        return;

    if ((entry = dma_buf->priv) == NULL)
        return;

    this = &entry->object_data;

    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) start.\n", __func__, entry->fd);

    if (sg_table == NULL)
        goto done;
    
    dma_unmap_sgtable(attachment->dev, sg_table, direction, 0);
    sg_free_table(sg_table);
    kfree(sg_table);
 done:
    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) done.\n", __func__, entry->fd);
}

/**
 * udmabuf_export_release() - udmabuf export dma-buf release operation.
 * @dma_buf:	Pointer to dma-buf structure.
 */
static void udmabuf_export_release(struct dma_buf *dma_buf)
{
    struct udmabuf_export_entry* entry = dma_buf->priv; 
    struct udmabuf_object*       this;

    if (entry == NULL)
        return;

    if ((this = entry->object) == NULL)
        return;
    
    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) start.\n", __func__, entry->fd);

    mutex_lock(&this->export_dma_buf_list_sem);
    list_del(&entry->list);
    mutex_unlock(&this->export_dma_buf_list_sem);
    kfree(entry);

    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) done.\n", __func__, entry->fd);
}

/**
 * udmabuf_export_mmap() - udmabuf export dma-buf memory map operation.
 * @dma_buf:	Pointer to dma-buf structure.
 * @vma:        Pointer to the vm area structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_export_mmap(struct dma_buf* dma_buf, struct vm_area_struct* vma)
{
    struct udmabuf_export_entry* entry = dma_buf->priv; 
    struct udmabuf_object*       this;
    int                          retval = 0;

    if (entry == NULL)
        return -ENODEV;

    this = &entry->object_data;
    
    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) start.\n", __func__, entry->fd);

    retval = udmabuf_object_mmap(this, vma, entry->force_sync);
    if (retval) {
        dev_err( this->sys_dev, "%s(fd=%d): udmabuf_object_mmap() failed return=%d\n", __func__, entry->fd, retval);
        goto failed;
    }

    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) done.\n", __func__, entry->fd);
    return 0;

 failed:
    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) failed. return=%d\n", __func__, entry->fd, retval);
    return retval;
}

/**
 * udmabuf_export_begin_cpu() - udmabuf export dma-buf begin_cpu operation.
 *                              This is called from dma_buf_begin_cpu_access().
 * @dma_buf:	Pointer to dma-buf structure.
 * @direction:  length of range for cpu access.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_export_begin_cpu(struct dma_buf* dma_buf, enum dma_data_direction direction)
{
    struct udmabuf_export_entry* entry = dma_buf->priv; 
    struct udmabuf_object*       this;

    if (entry == NULL)
        return -ENODEV;

    this = &entry->object_data;
    
    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) start.\n", __func__, entry->fd);

    this->sync_for_cpu    = 1;
    this->sync_offset     = 0;
    this->sync_size       = this->alloc_size;
    this->sync_direction  = direction;
    udmabuf_sync_for_cpu(this);

    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) done.\n", __func__, entry->fd);

    return 0;
}

/**
 * udmabuf_export_end_cpu() - udmabuf export dma-buf end_cpu operation.
 *                            This is called from dma_buf_end_cpu_access()
 * @dma_buf:	Pointer to dma-buf structure.
 * @direction:  length of range for cpu access.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_export_end_cpu(struct dma_buf* dma_buf, enum dma_data_direction direction)
{
    struct udmabuf_export_entry* entry = dma_buf->priv; 
    struct udmabuf_object*       this;

    if (entry == NULL)
        return -ENODEV;

    this = &entry->object_data;
    
    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) start.\n", __func__, entry->fd);

    this->sync_for_device = 1;
    this->sync_offset     = 0;
    this->sync_size       = this->alloc_size;
    this->sync_direction  = direction;
    udmabuf_sync_for_device(this);

    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s(fd=%d) done.\n", __func__, entry->fd);

    return 0;
}

/**
 * udmabuf_export_kmap() - udmabuf export dma-buf end_cpu operation.
 *                         This is dummy for linux kernel 4.19 and earlier.
 * Return:      NULL
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0))
static void* udmabuf_export_kmap(struct dma_buf* dma_buf, unsigned long page)
{
    return NULL;
}
#endif

/**
 * udmabuf export dma-buf operation table.
 */
static const struct dma_buf_ops udmabuf_export_ops = {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
    .cache_sgt_mapping = true,
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0))
    .map               = udmabuf_export_kmap,
#endif
    .map_dma_buf       = udmabuf_export_dma_buf_map,
    .unmap_dma_buf     = udmabuf_export_dma_buf_unmap,
    .release           = udmabuf_export_release,
    .mmap              = udmabuf_export_mmap,
    .begin_cpu_access  = udmabuf_export_begin_cpu,
    .end_cpu_access    = udmabuf_export_end_cpu,
};

/**
 * udmabuf_export_create_entry() - Create udmabuf export dma-buf entry and add list.
 * @this:       Pointer to the udmabuf object.
 * Return:      Pointer to the udmabuf export_dma_buf(>0) or error status(<=0).
 */
static struct udmabuf_export_entry* udmabuf_export_create_entry(
     struct udmabuf_object*  this     ,
     u64                     offset   ,
     size_t                  size     ,
     unsigned long           fd_flags )
{
    DEFINE_DMA_BUF_EXPORT_INFO(export_info);
    struct udmabuf_export_entry* entry  = NULL;
    int                          retval = 0;
    bool                         force_sync;
    unsigned long                export_fd_flags;
    unsigned long                dmabuf_fd_flags;

    if (UDMABUF_EXPORT_DEBUG(this)) {
        dev_info(this->sys_dev, "%s() start.\n", __func__);
        dev_info(this->sys_dev, "offset         = 0x%llx\n" , offset);
        dev_info(this->sys_dev, "size           = %zu\n"    , size);
        dev_info(this->sys_dev, "fd_flags       = 0x%08lx\n", fd_flags);
    }

    if ((offset & (PAGE_SIZE-1)) != 0) {
        dev_err(this->sys_dev, "%s() offset is not page allignment\n", __func__);
        retval = -EINVAL;
        goto failed;
    }

    if ((size & (PAGE_SIZE-1)) != 0) {
        dev_err(this->sys_dev, "%s() size is not page allignment\n", __func__);
        retval = -EINVAL;
        goto failed;
    }

    if ((offset + size) > this->size) {
        dev_err(this->sys_dev, "%s() offset + size is over buffer size\n", __func__);
        retval = -EINVAL;
        goto failed;
    }

    if ((fd_flags & ~(O_CLOEXEC | O_SYNC | O_ACCMODE)) != 0) {
        dev_err(this->sys_dev, "%s() invalid fd_flags\n", __func__);
        retval = -EINVAL;
        goto failed;
    }
    force_sync      = ((fd_flags &  O_SYNC) != 0);
    export_fd_flags = ((fd_flags & ~O_SYNC));
    dmabuf_fd_flags = ((fd_flags & ~O_SYNC));

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (IS_ERR_OR_NULL(entry)) {
        retval = PTR_ERR(entry);
        entry  = NULL;
        dev_err(this->sys_dev, "%s() kzalloc() failed. return=%d\n", __func__, retval);
        goto failed;
    }
    entry->object = this;
    entry->offset = offset;
    entry->size   = size;

    entry->object_data.sys_dev         = this->sys_dev;
    entry->object_data.dma_dev         = this->dma_dev;
    entry->object_data.phys_addr       = this->phys_addr + offset;
    entry->object_data.virt_addr       = this->virt_addr + offset;
    entry->object_data.size            = size;
    entry->object_data.alloc_size      = size;
    entry->object_data.sync_mode       = this->sync_mode;
    entry->object_data.sync_offset     = 0;
    entry->object_data.sync_size       = size;
    entry->object_data.sync_direction  = 0;
    entry->force_sync                  = force_sync;
#if (USE_QUIRK_MMAP == 1)
    entry->object_data.quirk_mmap_mode = this->quirk_mmap_mode;
#if (USE_QUIRK_MMAP_PAGE == 1)
    if (this->pages != NULL) {
        entry->object_data.pagecount   = size >> PAGE_SHIFT;
        entry->object_data.pages       = &this->pages[offset>>PAGE_SHIFT];
    }
#endif    
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_QUIRK_MMAP == 1))
    entry->object_data.debug_vma       = this->debug_vma;
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_DMA_BUF_EXPORT == 1))
    entry->object_data.debug_export    = this->debug_export;
#endif

    export_info.ops      = &udmabuf_export_ops;
    export_info.size     = size;
    export_info.priv     = (void*)entry;
    export_info.flags    = export_fd_flags;
    export_info.exp_name = dev_name(this->sys_dev);

    entry->dma_buf = dma_buf_export(&export_info);
    if (IS_ERR(entry->dma_buf)) {
        retval = PTR_ERR(entry->dma_buf);
        entry->dma_buf = NULL;
        dev_err(this->sys_dev, "%s(): dma_buf_export() failed. return=%d\n", __func__, retval);
        goto failed;
    }

    entry->fd = dma_buf_fd(entry->dma_buf, dmabuf_fd_flags);
    if (entry->fd < 0) {
        retval = entry->fd;
        entry->fd = 0;
        dev_err(this->sys_dev, "%s(): dma_buf_fd() failed. return=%d\n", __func__, retval);
        goto failed;
    }

    mutex_lock(&this->export_dma_buf_list_sem);
    list_add_tail(&entry->list, &this->export_dma_buf_list);
    mutex_unlock(&this->export_dma_buf_list_sem);

    if (UDMABUF_EXPORT_DEBUG(this)) {
        dev_info(this->sys_dev, "force_sync     = %d\n", entry->force_sync);
        dev_info(this->sys_dev, "export_fd      = %d\n", entry->fd);
        dev_info(this->sys_dev, "%s() done.\n", __func__);
    }

    return entry;

 failed:
    if (entry != NULL) {
        if (entry->dma_buf != NULL) {
            dma_buf_put(entry->dma_buf);
        }
        kfree(entry);
    }
    if (UDMABUF_EXPORT_DEBUG(this))
        dev_info(this->sys_dev, "%s() failed. return=%d\n", __func__, retval);
    return ERR_PTR(retval);
}
#endif /* #if (USE_DMA_BUF_EXPORT == 1) */

/**
 * DOC: Udmabuf Device File Operations.
 *
 * This section defines the operation of the udmabuf device file.
 *
 * * udmabuf_device_file_open()    - udmabuf device file open operation.
 * * udmabuf_device_file_release() - udmabuf device file release operation.
 * * udmabuf_device_file_mmap()    - udmabuf device file memory map operation.
 * * udmabuf_device_file_read()    - udmabuf device file read operation.
 * * udmabuf_device_file_write()   - udmabuf device file write operation.
 * * udmabuf_device_file_llseek()  - udmabuf device file llseek operation.
 * * udmabuf_device_file_ioctl()   - udmabuf device file ioctl operation.
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
    struct udmabuf_object* this;
    int status = 0;

    this = container_of(inode->i_cdev, struct udmabuf_object, cdev);
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
    struct udmabuf_object* this = file->private_data;

    this->is_open = 0;

    return 0;
}

/**
 * udmabuf_device_file_mmap() - udmabuf device file memory map operation.
 * @file:       Pointer to the file structure.
 * @vma:        Pointer to the vm area structure.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_device_file_mmap(struct file *file, struct vm_area_struct* vma)
{
    struct udmabuf_object* this = file->private_data;
    bool force_sync = ((file->f_flags & O_SYNC) != 0);

    return udmabuf_object_mmap(this, vma, force_sync);
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
    struct udmabuf_object* this      = file->private_data;
    int                    result    = 0;
    size_t                 xfer_size;
    size_t                 remain_size;
    dma_addr_t             phys_addr;
    void*                  virt_addr;

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
    struct udmabuf_object* this      = file->private_data;
    int                    result    = 0;
    size_t                 xfer_size;
    size_t                 remain_size;
    dma_addr_t             phys_addr;
    void*                  virt_addr;

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
    struct udmabuf_object* this = file->private_data;
    loff_t                 new_pos;

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
 * u-dma-buf-ioctl.h - u-dma-buf ioctl header file
 *
 * This source code(u-dma-buf.c) has built-in header file(u-dma-buf-ioctl.h) 
 * so that it can be built with only one source code.
 * To generate a header file (u-dma-buf-ioctl.h) from this source code (u-dma-buf.c), 
 * do the following
 * 
 * sed -n '/^\/\*\*\*\*\*\*\*\*\*\*\**$/,/\**\*\*\*\*\*\*\*\*\*\*\/$/p' u-dma-buf.c >  u-dma-buf-ioctl.h
 * sed -n '/^#ifndef.*U_DMA_BUF_IOCTL_H/,/^#endif.*U_DMA_BUF_IOCTL_H/p' u-dma-buf.c >> u-dma-buf-ioctl.h
 * 
 */
#if (IOCTL_VERSION > 0)
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
#endif /* #if (IOCTL_VERSION > 0) */

/**
 * udmabuf_device_file_ioctl() - udmabuf device file ioctl operation.
 * @file:       Pointer to the file structure.
 * @cmd:        The ioctl command to be executed.
 * @arg:        Pointer to user space data associated with the ioctl command.
 * Return:      Success(=0) or error status(<0).
 */
#if (IOCTL_VERSION > 0)
static long udmabuf_device_file_ioctl(struct file* file, unsigned int cmd, unsigned long arg)
{
    struct udmabuf_object* this   = file->private_data;
    void __user*           argp   = (void __user*)arg;
    int                    result = 0;

    switch(cmd) {
        case U_DMA_BUF_IOCTL_GET_DRV_INFO: {
            u_dma_buf_ioctl_drv_info drv_info = {0};
            SET_U_DMA_BUF_IOCTL_FLAGS_IOCTL_VERSION      (&drv_info, IOCTL_VERSION);
            SET_U_DMA_BUF_IOCTL_FLAGS_IN_KERNEL_FUNCTIONS(&drv_info, IN_KERNEL_FUNCTIONS);
            SET_U_DMA_BUF_IOCTL_FLAGS_USE_OF_DMA_CONFIG  (&drv_info, USE_OF_DMA_CONFIG);
            SET_U_DMA_BUF_IOCTL_FLAGS_USE_OF_RESERVED_MEM(&drv_info, USE_OF_RESERVED_MEM);
            SET_U_DMA_BUF_IOCTL_FLAGS_USE_QUIRK_MMAP     (&drv_info, USE_QUIRK_MMAP);
            SET_U_DMA_BUF_IOCTL_FLAGS_USE_QUIRK_MMAP_PAGE(&drv_info, USE_QUIRK_MMAP_PAGE);
            if (strscpy(&drv_info.version[0], DRIVER_VERSION, sizeof(drv_info.version)) < 0)
                result = -EFAULT;
            else if (copy_to_user(argp, &drv_info, sizeof(drv_info)) != 0)
                result = -EFAULT;
            else 
                result = 0;
            break;
        }
        case U_DMA_BUF_IOCTL_GET_SIZE: {
            uint64_t size = (uint64_t)this->size;
            if (copy_to_user(argp, &size, sizeof(size)) != 0)
                result = -EFAULT;
            else 
                result = 0;
            break;
        }
        case U_DMA_BUF_IOCTL_GET_DMA_ADDR: {
            uint64_t dma_addr = (uint64_t)this->phys_addr;
            if (copy_to_user(argp, &dma_addr, sizeof(dma_addr)) != 0)
                result = -EFAULT;
            else 
                result = 0;
            break;
        }
        case U_DMA_BUF_IOCTL_GET_SYNC_OWNER: {
            uint32_t sync_owner = (uint32_t)this->sync_owner;
            if (copy_to_user(argp, &sync_owner, sizeof(sync_owner)) != 0)
                result = -EFAULT;
            else 
                result = 0;
            break;
        }
        case U_DMA_BUF_IOCTL_GET_DEV_INFO: {
            u_dma_buf_ioctl_dev_info dev_info = {0};
            u64    dma_mask = *this->dma_dev->dma_mask;
            int    dma_mask_size = 0;
            u64    dma_mask_bit  = ((u64)1UL << dma_mask_size);
            while (dma_mask_size < 64) {
                if ((dma_mask & dma_mask_bit) == 0)
                    break;
                dma_mask_size++;
                dma_mask_bit = dma_mask_bit << 1;
            }
            SET_U_DMA_BUF_IOCTL_FLAGS_DMA_MASK    (&dev_info, dma_mask_size);
#if defined(IS_DMA_COHERENT)
            SET_U_DMA_BUF_IOCTL_FLAGS_DMA_COHERENT(&dev_info, IS_DMA_COHERENT(this->dma_dev));
#endif
#if (USE_QUIRK_MMAP == 1)
            SET_U_DMA_BUF_IOCTL_FLAGS_MMAP_MODE   (&dev_info, this->quirk_mmap_mode);
#endif
            dev_info.size = (uint64_t)(this->size);
            dev_info.addr = (uint64_t)(this->phys_addr);
            if (copy_to_user(argp, &dev_info, sizeof(dev_info)) != 0)
                result = -EFAULT;
            else 
                result = 0;
            break;
        }
        case U_DMA_BUF_IOCTL_GET_SYNC: {
            u_dma_buf_ioctl_sync_args sync_args = {0};
            SET_U_DMA_BUF_IOCTL_FLAGS_SYNC_DIR  (&sync_args, this->sync_direction);
            SET_U_DMA_BUF_IOCTL_FLAGS_SYNC_MODE (&sync_args, this->sync_mode);
            SET_U_DMA_BUF_IOCTL_FLAGS_SYNC_OWNER(&sync_args, this->sync_owner);
            sync_args.size   = (uint64_t)this->sync_size;
            sync_args.offset = (uint64_t)this->sync_offset;
            if (copy_to_user(argp, &sync_args, sizeof(sync_args)) != 0)
                result = -EFAULT;
            else 
                result = 0;
            break;
        }
        case U_DMA_BUF_IOCTL_SET_SYNC: {
            u_dma_buf_ioctl_sync_args sync_args;
            if (copy_from_user(&sync_args, argp, sizeof(sync_args)) != 0)
                result = -EFAULT;
            else {
                int    sync_command   = GET_U_DMA_BUF_IOCTL_FLAGS_SYNC_CMD (&sync_args);
                int    sync_direction = GET_U_DMA_BUF_IOCTL_FLAGS_SYNC_DIR (&sync_args);
                int    sync_mode      = GET_U_DMA_BUF_IOCTL_FLAGS_SYNC_MODE(&sync_args);
                u64    sync_offset    = (u64)(sync_args.offset);
                size_t sync_size      = (size_t)(sync_args.size);
                switch(sync_direction) {
                    case 0   : this->sync_direction = 0; break;
                    case 1   : this->sync_direction = 1; break;
                    case 2   : this->sync_direction = 2; break;
                    default  : /* none */                break;
                }
                if (sync_mode   >  0) {this->sync_mode   = sync_mode  ;}
                if (sync_offset >= 0) {this->sync_offset = sync_offset;}
                if (sync_size   >  0) {this->sync_size   = sync_size  ;}
                switch(sync_command) {
                    case U_DMA_BUF_IOCTL_FLAGS_SYNC_CMD_FOR_CPU:
                        this->sync_for_cpu = 1;
                        result = udmabuf_sync_for_cpu(this);
                        break;
                    case U_DMA_BUF_IOCTL_FLAGS_SYNC_CMD_FOR_DEVICE:
                        this->sync_for_device = 1;
                        result = udmabuf_sync_for_device(this);
                        break;
                    default  :
                        result = 0;
                        break;
                }
            }
            break;
        }
        case U_DMA_BUF_IOCTL_SET_SYNC_FOR_CPU: {
            u64 sync_args;
            if (copy_from_user(&sync_args, argp, sizeof(sync_args)) != 0)
                result = -EFAULT;
            else {
                this->sync_for_cpu = sync_args;
                result = udmabuf_sync_for_cpu(this);
            }
            break;
        }
        case U_DMA_BUF_IOCTL_SET_SYNC_FOR_DEVICE: {
            u64 sync_args;
            if (copy_from_user(&sync_args, argp, sizeof(sync_args)) != 0)
                result = -EFAULT;
            else {
                this->sync_for_device = sync_args;
                result = udmabuf_sync_for_device(this);
            }
            break;
        }
#if (USE_DMA_BUF_EXPORT == 1) && (IOCTL_VERSION >= 2)
        case U_DMA_BUF_IOCTL_EXPORT: {
            u_dma_buf_ioctl_export_args  export_args;
            struct udmabuf_export_entry* export_entry = ERR_PTR(-EINVAL);
            if (copy_from_user(&export_args, argp, sizeof(export_args)) != 0) {
                result = -EFAULT;
                goto export_failed;
            } else {
                u64    offset   = (u64)(export_args.offset);
                size_t size     = (size_t)(export_args.size);
                u32    fd_flags = GET_U_DMA_BUF_IOCTL_FLAGS_EXPORT_FD_FLAGS(&export_args);
                export_entry    = udmabuf_export_create_entry(this, offset, size, fd_flags);
            }
            if (IS_ERR_OR_NULL(export_entry)) {
                result = PTR_ERR(export_entry);
                export_entry = NULL;
                goto export_failed;
            }
            export_args.fd   = export_entry->fd;
            export_args.addr = export_entry->object_data.phys_addr;
            if (copy_to_user(argp, &export_args, sizeof(export_args)) != 0)
                result = -EFAULT;
            else 
                result = 0;
          export_failed:
            break;
        }
#endif            
        default:
            result = -ENOTTY;
    }
    return (long)result;
}

#endif /* #if (IOCTL_VERSION > 0) */

/**
 * udmabuf device file operation table.
 */
static const struct file_operations udmabuf_device_file_ops = {
    .owner          = THIS_MODULE,
    .open           = udmabuf_device_file_open,
    .release        = udmabuf_device_file_release,
    .mmap           = udmabuf_device_file_mmap,
    .read           = udmabuf_device_file_read,
    .write          = udmabuf_device_file_write,
    .llseek         = udmabuf_device_file_llseek,
#if (IOCTL_VERSION > 0)
    .unlocked_ioctl = udmabuf_device_file_ioctl,
#endif
};

/**
 * DOC: Udmabuf Object Operations.
 *
 * This section defines the operation of udmabuf object.
 *
 * * udmabuf_device_ida         - Udmabuf Object Device Minor Number allocator variable.
 * * udmabuf_device_number      - Udmabuf Object Device Major Number.
 * * udmabuf_object_create()    - Create udmabuf object.
 * * udmabuf_object_setup()     - Setup the udmabuf object.
 * * udmabuf_object_info()      - Print infomation the udmabuf object.
 * * udmabuf_object_destroy()   - Destroy the udmabuf object.
 */
static DEFINE_IDA(udmabuf_device_ida);
static dev_t      udmabuf_device_number = 0;

/**
 * udmabuf_object_create() -  Create udmabuf object.
 * @name:       device name   or NULL.
 * @parent:     parent device or NULL.
 * @minor:      minor_number  or -1 or -2.
 * Return:      Pointer to the udmabuf object or NULL.
 */
static struct udmabuf_object* udmabuf_object_create(const char* name, struct device* parent, int minor)
{
    struct udmabuf_object* this     = NULL;
    unsigned int           done     = 0;
    const unsigned int     DONE_ALLOC_MINOR   = (1 << 0);
    const unsigned int     DONE_CHRDEV_ADD    = (1 << 1);
    const unsigned int     DONE_DEVICE_CREATE = (1 << 3);
    const unsigned int     DONE_SET_DMA_DEV   = (1 << 4);
    /*
     * allocate device minor number
     */
    {
        if ((0 <= minor) && (minor < DEVICE_MAX_NUM)) {
            if (ida_simple_get(&udmabuf_device_ida, minor, minor+1, GFP_KERNEL) < 0) {
                pr_err(DRIVER_NAME ": couldn't allocate minor number(=%d).\n", minor);
                goto failed;
            }
        } else if(minor < 0) {
            if ((minor = ida_simple_get(&udmabuf_device_ida, 0, DEVICE_MAX_NUM, GFP_KERNEL)) < 0) {
                pr_err(DRIVER_NAME ": couldn't allocate new minor number. return=%d.\n", minor);
                goto failed;
            }
        } else {
                pr_err(DRIVER_NAME ": invalid minor number(=%d), valid range is 0 to %d\n", minor, DEVICE_MAX_NUM-1);
                goto failed;
        }
        done |= DONE_ALLOC_MINOR;
    }
    /*
     * create (udmabuf_object*) this.
     */
    {
        this = kzalloc(sizeof(*this), GFP_KERNEL);
        if (IS_ERR_OR_NULL(this)) {
            int retval = PTR_ERR(this);
            this = NULL;
            pr_err(DRIVER_NAME ": kzalloc() failed. return=%d\n", retval);
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
     * register /sys/class/u-dma-buf/<name>
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
            pr_err(DRIVER_NAME ": device_create() failed. return=%d\n", retval);
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
            dev_err(this->sys_dev, "cdev_add() failed. return=%d\n", retval);
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
                dev_warn(this->sys_dev,"dma_set_mask_and_coherent(DMA_BIT_MASK(%d)) failed. return=(%d)\n", dma_mask_bit, retval);
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
#if (USE_QUIRK_MMAP == 1)
    {
        this->quirk_mmap_mode = quirk_mmap_mode;
#if (USE_QUIRK_MMAP_PAGE == 1)
        this->pagecount       = 0;
        this->pages           = NULL;
#endif
    }
#endif
#if (USE_DMA_BUF_EXPORT == 1)
    {
        INIT_LIST_HEAD(&this->export_dma_buf_list);
        mutex_init(&this->export_dma_buf_list_sem);
    }
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_QUIRK_MMAP == 1))
    {
        this->debug_vma       = 0;
    }
#endif
#if ((UDMABUF_DEBUG == 1) && (USE_DMA_BUF_EXPORT == 1))
    {
        this->debug_export    = 0;
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
 * udmabuf_object_setup() - Setup the udmabuf object.
 * @this:       Pointer to the udmabuf object.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_object_setup(struct udmabuf_object* this)
{
    if (!this)
        return -ENODEV;
    /*
     * setup buffer size and allocation size
     */
    this->alloc_size = ((this->size + (((size_t)1 << PAGE_SHIFT) - 1)) >> PAGE_SHIFT) << PAGE_SHIFT;
    /*
     * dma buffer allocation 
     */
    this->virt_addr  = dma_alloc_coherent(this->dma_dev, this->alloc_size, &this->phys_addr, GFP_KERNEL);
    if (IS_ERR_OR_NULL(this->virt_addr)) {
        int retval = PTR_ERR(this->virt_addr);
        dev_err(this->sys_dev, "dma_alloc_coherent(size=%zu) failed. return(%d)\n", this->alloc_size, retval);
        this->virt_addr = NULL;
        return (retval == 0) ? -ENOMEM : retval;
    }
#if ((USE_QUIRK_MMAP == 1) && USE_QUIRK_MMAP_PAGE == 1)
    if (this->quirk_mmap_mode == QUIRK_MMAP_MODE_PAGE) {
        pgoff_t       pg;
        phys_addr_t   phys_paddr     = dma_to_phys(this->dma_dev, this->phys_addr);
        unsigned long page_frame_num = phys_paddr >> PAGE_SHIFT;
        struct page*  phys_pages;

        if (!pfn_valid(page_frame_num)) {
            dev_warn(this->sys_dev, "get page(phys_addr=%pad) failed.", &this->phys_addr);
            goto quirk_mmap_page_done;
        }

        phys_pages      = pfn_to_page(page_frame_num);
        this->pagecount = this->alloc_size >> PAGE_SHIFT;
        this->pages     = kmalloc_array(this->pagecount, sizeof(struct page*), GFP_KERNEL);
        if (IS_ERR_OR_NULL(this->pages)) {
            int retval = PTR_ERR(this->pages);
            dev_warn(this->sys_dev, "allocate pages(pagecount=%lu) failed. return(%d)\n", (unsigned long)this->pagecount, retval);
            this->pagecount = 0;
            this->pages     = NULL;
            goto quirk_mmap_page_done;
        }
        for (pg = 0; pg < this->pagecount; pg++) {
            this->pages[pg] = nth_page(phys_pages, pg);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
            page_kasan_tag_reset(this->pages[pg]);
#endif
        }
      quirk_mmap_page_done:
        ;
    }
#endif
    return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 11))
/**
 * dev_bus_name() - Return a device's bus/class name, if at all possible.
 * @dev: struct device to get the bus/class name of
 *
 * Will return the name of the bus/class the device is attached to.  
 * If it is not attached to a bus/class, an empty string will be returned.
 */
static inline const char* dev_bus_name(const struct device* dev)
{
    return dev->bus ? dev->bus->name : (dev->class ? dev->class->name : "");
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0))
#include <linux/iommu.h>
static  char* get_iommu_domain_type(struct device* dev)
{
    struct iommu_domain* domain = iommu_get_domain_for_dev(dev);
    if (!domain)
        return "NONE";
    else if (domain->type == IOMMU_DOMAIN_BLOCKED)
        return "BLOCKED";
    else if (domain->type == IOMMU_DOMAIN_IDENTITY)
        return "IDENTITY";
    else if (domain->type == IOMMU_DOMAIN_UNMANAGED)
        return "UNMANAGED";
    else if (domain->type == IOMMU_DOMAIN_DMA)
        return "DMA";
    else 
        return "UNKNOWN";
}
#define GET_IOMMU_DOMAIN_TYPE(dev) get_iommu_domain_type(dev)
#endif

/**
 * udmabuf_object_info() - Print infomation the udmabuf object.
 * @this:       Pointer to the udmabuf object.
 */
static void udmabuf_object_info(struct udmabuf_object* this)
{
    dev_info(this->sys_dev, "driver version = %s\n"  , DRIVER_VERSION);
    dev_info(this->sys_dev, "major number   = %d\n"  , MAJOR(this->device_number));
    dev_info(this->sys_dev, "minor number   = %d\n"  , MINOR(this->device_number));
    dev_info(this->sys_dev, "phys address   = %pad\n", &this->phys_addr);
    dev_info(this->sys_dev, "buffer size    = %zu\n" , this->alloc_size);
    if (DMA_INFO_ENABLE) {
        dev_info(this->sys_dev, "dma device     = %s\n"       , dev_name(this->dma_dev));
        dev_info(this->sys_dev, "dma bus        = %s\n"       , dev_bus_name(this->dma_dev));
#if defined(IS_DMA_COHERENT)
        dev_info(this->sys_dev, "dma coherent   = %d\n"       , IS_DMA_COHERENT(this->dma_dev));
#endif
        dev_info(this->sys_dev, "dma mask       = 0x%016llx\n", dma_get_mask(this->dma_dev));
#if defined(GET_IOMMU_DOMAIN_TYPE)
        dev_info(this->sys_dev, "iommu domain   = %s\n"       , GET_IOMMU_DOMAIN_TYPE(this->dma_dev));
#endif
    }
#if (USE_QUIRK_MMAP == 1)
    if (DMA_INFO_ENABLE) {
        dev_info(this->sys_dev, "mmap mode      = %d\n"       , this->quirk_mmap_mode);
      if (udmabuf_quirk_mmap_enable(this) == true) {
        dev_info(this->sys_dev, "mmap           = quirk-mmap\n");
#if (USE_QUIRK_MMAP_PAGE == 1)
       if (this->pages != NULL) {
        dev_info(this->sys_dev, "mmap pages     = %pad\n"     , &this->pages);
        dev_info(this->sys_dev, "mmap pages[0]  = %pad\n"     , &this->pages[0]);
       } else {
        dev_info(this->sys_dev, "mmap pages     = NONE\n"     );
       }
#endif
      } else {
        dev_info(this->sys_dev, "mmap           = dma_mmap_coherent\n");
      }
    }
#endif
}

/**
 * udmabuf_object_destroy() -  Destroy the udmabuf object.
 * @this:       Pointer to the udmabuf object.
 * Return:      Success(=0) or error status(<0).
 *
 * Unregister the device after releasing the resources.
 */
static int udmabuf_object_destroy(struct udmabuf_object* this)
{
    if (!this)
        return -ENODEV;

#if (USE_DMA_BUF_EXPORT == 1)
    {
        bool empty;
        mutex_lock(&this->export_dma_buf_list_sem);
        empty = list_empty(&this->export_dma_buf_list);
        mutex_unlock(&this->export_dma_buf_list_sem);
        if (empty == false) {
            dev_err(this->sys_dev, "exported dma-buf is currently busy.\n");
            return -EBUSY;
        }
    }
#endif
    
#if ((USE_QUIRK_MMAP == 1) && USE_QUIRK_MMAP_PAGE == 1)
    if (this->pages != NULL) {
        kfree(this->pages);
        this->pages     = NULL;
        this->pagecount = 0;
    }
#endif
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
 * DOC: Udmabuf Device List section.
 *
 * This section defines the udmabuf device list.
 *
 * * struct udmabuf_device_entry          - udmabuf device entry structure.
 * * udmabuf_device_list                  - list of udmabuf device entry structure.
 * * udmabuf_device_list_sem              - semaphore of udmabuf device list.
 * * udmabuf_device_list_create_entry()   - Create udmabuf device entry and add to list.
 * * udmabuf_device_list_delete_entry()   - Delete udmabuf device entry from list.
 * * udmabuf_device_list_remove_entry()   - Remove udmabuf device entry from list with remove functions.
 * * udmabuf_device_list_cleanup()        - Remove all udmabuf device entry from list.
 * * udmabuf_device_list_search()         - Search udmabuf device entry from list by name or number.
 * * udmabuf_get_device_name_property()   - Get "device-name"  property from udmabuf device.
 * * udmabuf_get_size_property()          - Get "buffer-size"  property from udmabuf device.
 * * udmabuf_get_minor_number_property()  - Get "minor-number" property from udmabuf device.
 * * udmabuf_get_option_property()        - Get "option"       property from udmabuf device.
 * * udmabuf_get_quirk_mmap_property()    - Get "quirk_mmap"   property from "option" property.
 */

#if (USE_DEV_PROPERTY != 0)
#include <linux/property.h>
#endif

/**
 * struct udmabuf_device_entry - udmabuf device entry structure.
 */
struct udmabuf_device_entry {
    struct device*       dev;
    void                 (*prep_remove)(struct device* dev);
    void                 (*post_remove)(struct device* dev);
#if (USE_DEV_PROPERTY == 0)
    const char*          device_name;
    u32                  minor_number;
    u64                  buffer_size;
    u64                  option;
#endif
    struct list_head     list;
};

/**
 * udmabuf_device_list        - list of udmabuf device entry structure.
 * udmabuf_device_list_sem    - semaphore of udmabuf device list.
 */
static struct list_head udmabuf_device_list;
static struct mutex     udmabuf_device_list_sem;

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
    int                          status = -1;
    struct udmabuf_device_entry* entry;

    if (lock)
        mutex_lock(&udmabuf_device_list_sem);
    list_for_each_entry(entry, &udmabuf_device_list, list) {
        if (entry->dev == dev) {
            if (entry->device_name == NULL) {
                status = -1;
            } else {
                *name  = entry->device_name;
                status = 0;
            }
            break;
        }
    }
    if (lock)
        mutex_unlock(&udmabuf_device_list_sem);
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
    int                          status = -1;
    struct udmabuf_device_entry* entry;

    if (lock)
        mutex_lock(&udmabuf_device_list_sem);
    list_for_each_entry(entry, &udmabuf_device_list, list) {
        if (entry->dev == dev) {
            *value = entry->buffer_size;
            status = 0;
            break;
        }
    }
    if (lock)
        mutex_unlock(&udmabuf_device_list_sem);
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
    int                          status = -1;
    struct udmabuf_device_entry* entry;

    if (lock)
        mutex_lock(&udmabuf_device_list_sem);
    list_for_each_entry(entry, &udmabuf_device_list, list) {
        if (entry->dev == dev) {
            *value = entry->minor_number;
            status = 0;
            break;
        }
    }
    if (lock) 
        mutex_unlock(&udmabuf_device_list_sem);
    return status;
#else
    return device_property_read_u32(dev, "minor-number", value);
#endif
}

/**
 * udmabuf_get_option_property() - Get "option" property from udmabuf device.
 * @dev:        handle to the device structure.
 * @value:      address of option value.
 * @lock:       use mutex_lock()/mutex_unlock()
 * Return:      Success(=0) or error status(<0).
 */
#if (USE_QUIRK_MMAP == 1)
static int  udmabuf_get_option_property(struct device *dev, u64* value, bool lock)
{
#if (USE_DEV_PROPERTY == 0)
    int                          status = -1;
    struct udmabuf_device_entry* entry;

    if (lock)
        mutex_lock(&udmabuf_device_list_sem);
    list_for_each_entry(entry, &udmabuf_device_list, list) {
        if (entry->dev == dev) {
            *value = entry->option;
            status = 0;
            break;
        }
    }
    if (lock) 
        mutex_unlock(&udmabuf_device_list_sem);
    return status;
#else
    return device_property_read_u64(dev, "option", value);
#endif
}
#endif

/**
 * udmabuf_get_option_dma_mask_size()   - Get dma mask size   from option.
 * udmabuf_get_option_quirk_mmap_mode() - Get quirk-mmap mode from option.
 *
 * @option:     option. dma_mask   = option[ 7: 0]
 *                      quirk_mmap = option[12:10]
 */
#define DEFINE_UDMABUF_OPTION(name,type,lo,hi)             \
static inline type udmabuf_get_option_ ## name(u64 option) \
{                                                          \
    const u64 mask = ((1UL << ((hi)-(lo)+1))-1);           \
    return (type)((option >> (lo)) & mask);                \
}
DEFINE_UDMABUF_OPTION(dma_mask_size   ,u64, 0, 7)
DEFINE_UDMABUF_OPTION(quirk_mmap_mode ,int,10,12)

/**
 * udmabuf_get_quirk_mmap_property()    - Get "quirk_mmap" property from "option" property.
 * @dev:        handle to the device structure.
 * @value:      address of quirk_mmap value.
 * @lock:       use mutex_lock()/mutex_unlock()
 * Return:      Success(=0) or error status(<0).
 */
#if (USE_QUIRK_MMAP == 1)
static int  udmabuf_get_quirk_mmap_property(struct device *dev, int* value, bool lock)
{
    u64 option;
    int status = udmabuf_get_option_property(dev, &option, lock);
    if (status == 0) {
        int quirk_mmap_mode = udmabuf_get_option_quirk_mmap_mode(option);
        if (udmabuf_check_quirk_mmap_mode(quirk_mmap_mode) == true)
            *value = quirk_mmap_mode;
        else
            status = -EINVAL;
    }
    return status;
}
#endif

/**
 * udmabuf_device_list_search()    - Search udmabuf device entry from list by name or number.
 * @dev:        handle to the device structure or NULL.
 * @name:       device name or NULL.
 * @id:         device id or negative integer.
 * Return:      Pointer to the found udmabuf device entry or NULL.
 */
static struct udmabuf_device_entry* udmabuf_device_list_search(struct device *dev, const char* name, int id)
{
    struct udmabuf_device_entry* entry;
    struct udmabuf_device_entry* found_entry = NULL;
    mutex_lock(&udmabuf_device_list_sem);
    list_for_each_entry(entry, &udmabuf_device_list, list) {
        bool found_by_dev  = true;
        bool found_by_name = true;
        bool found_by_id   = true;
        if (dev != NULL) {
            found_by_dev = false;
            if (dev == entry->dev)
                found_by_dev = true;
        }
        if (name != NULL) {
            const char* device_name;
            found_by_name = false;
            if (udmabuf_get_device_name_property(entry->dev, &device_name, false) == 0) 
                if (strcmp(name, device_name) == 0)
                    found_by_name = true;
        }
        if (id >= 0) {
            u32 minor_number;
            found_by_id = false;
            if (udmabuf_get_minor_number_property(entry->dev, &minor_number, false) == 0) 
                if (id == minor_number)
                    found_by_id = true;
        }
        if ((found_by_dev == true) && (found_by_name == true) && (found_by_id == true))
            found_entry = entry;
    }
    mutex_unlock(&udmabuf_device_list_sem);
    return found_entry;
}

/**
 * udmabuf_device_list_create_entry() - Create udmabuf device entry and add to list.
 * @dev:        handle to the device structure.
 * @name:       device name or NULL.
 * @id:         device id or negative integer.
 * @size:       buffer size.
 * @option      option.
 * @prep_remove prepare function when remove entry from udmabuf device list or NULL.
 * @post_remove post function when remove entry from udmabuf device list or NULL.
 * Return:      pointer to the udmabuf device entry or NULL.
 */
static struct udmabuf_device_entry* udmabuf_device_list_create_entry(struct device *dev, const char* name, int id, unsigned int size, u64 option, void (*prep_remove)(struct device*), void (*post_remove)(struct device*))
{                              
    struct udmabuf_device_entry* exist_entry;
    struct udmabuf_device_entry* entry  = NULL;
    int                          retval = 0;

    exist_entry = udmabuf_device_list_search(NULL, name, id);
    if (!IS_ERR_OR_NULL(exist_entry)) {
        pr_err(DRIVER_NAME ": device name(%s) or id(%d) is already exists\n", (name)?name:"NULL", id);
        retval = -EINVAL;
        goto failed;
    }

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (IS_ERR_OR_NULL(entry)) {
        retval = PTR_ERR(entry);
        entry  = NULL;
        pr_err(DRIVER_NAME ": kzalloc() failed. return=%d\n", retval);
        goto failed;
    }

#if (USE_DEV_PROPERTY == 0)
    {
        entry->device_name  = (name != NULL) ? kstrdup(name, GFP_KERNEL) : NULL;
        entry->minor_number = id;
        entry->buffer_size  = size;
        entry->option       = option;
    }
#else
    {
        struct property_entry   props_list[] = {
            PROPERTY_ENTRY_STRING("device-name" , name  ),
            PROPERTY_ENTRY_U64(   "size"        , size  ),
            PROPERTY_ENTRY_U32(   "minor-number", id    ),
            PROPERTY_ENTRY_U64(   "option"      , option),
            {},
        };
        struct property_entry* props = (name != NULL) ? &props_list[0] : &props_list[1];
#if     (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0))
        {
            retval = device_create_managed_software_node(dev, props, NULL);
            if (retval != 0) {
                pr_err(DRIVER_NAME ": device_create_managed_software_node failed. return=%d\n", retval);
                goto failed;
            }
        }
#elif   (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0))
        {
            retval = device_add_properties(dev, props);
            if (retval != 0) {
                pr_err(DRIVER_NAME ": device_add_properties failed. return=%d\n", retval);
                goto failed;
            }
        }
#else
        {
            const struct property_set pset = {
                .properties = props,
            };
            retval = device_add_property_set(dev, &pset);
            if (retval != 0) {
                pr_err(DRIVER_NAME ": device_add_propertiy_set failed. return=%d\n", retval);
                goto failed;
            }
        }
#endif
    }
#endif

    entry->dev = dev;
    entry->prep_remove = prep_remove;
    entry->post_remove = post_remove;
    
    mutex_lock(&udmabuf_device_list_sem);
    list_add_tail(&entry->list, &udmabuf_device_list);
    mutex_unlock(&udmabuf_device_list_sem);

    return entry;

 failed:
    if (entry != NULL) {
#if (USE_DEV_PROPERTY == 0)
        if (entry->device_name != NULL)
            kfree(entry->device_name);
#endif
        kfree(entry);
    }
    return ERR_PTR(retval);
}

/**
 * udmabuf_device_list_delete_entry() - Delete udmabuf device entry from list.
 * @entry:      Pointer to the udmabuf device entry.
 */
static void udmabuf_device_list_delete_entry(struct udmabuf_device_entry* entry)
{
    mutex_lock(&udmabuf_device_list_sem);
    list_del(&entry->list);
    mutex_unlock(&udmabuf_device_list_sem);
#if (USE_DEV_PROPERTY == 0)
    if (entry->device_name != NULL)
        kfree(entry->device_name);
#endif
    kfree(entry);
}

/**
 * udmabuf_device_list_remove_entry() - Remove udmabuf device entry from list with remove functions.
 * @entry:      Pointer to the udmabuf device entry.
 */
static void udmabuf_device_list_remove_entry(struct udmabuf_device_entry* entry)
{
    struct device* dev = entry->dev;
    void (*prep_remove)(struct device* dev) = entry->prep_remove;
    void (*post_remove)(struct device* dev) = entry->post_remove;
    if (prep_remove)
        prep_remove(dev);
    udmabuf_device_list_delete_entry(entry);
    if (post_remove)
        post_remove(dev);
}

/**
 * udmabuf_device_list_cleanup() - Remove all udmabuf device entry from list.
 */
static void udmabuf_device_list_cleanup(void)
{
    struct udmabuf_device_entry* entry;
    while(!list_empty(&udmabuf_device_list)) {
        entry = list_first_entry(&udmabuf_device_list, typeof(*(entry)), list);
        udmabuf_device_list_remove_entry(entry);
    }
}

/**
 * DOC: Udmabuf Platform Device section.
 *
 * This section defines the udmabuf platform device.
 *
 * * udmabuf_platform_device_create() - Create udmabuf platform device and add to device list.
 * * udmabuf_platform_device_del()    - Delete udmabuf platform device before remove from device list.
 * * udmabuf_platform_device_put()    - Put udmabuf platform device after remove from device list.
 * * udmabuf_platform_device_probe()  - Probe  call for the platform device driver.
 * * udmabuf_platform_device_remove() - Remove call for the platform device driver.
 */

/**
 * udmabuf_platform_device_del() - Delete udmabuf platform device before remove from device list.
 * @dev:        handle to the device structure.
 */
static void udmabuf_platform_device_del(struct device* dev)
{
    /*
     * platform_device_del() calls udmabuf_platform_driver_remove()
     */
    platform_device_del(to_platform_device(dev));
}

/**
 * udmabuf_platform_device_put() - Put udmabuf platform device after remove from device list.
 * @dev:        handle to the device structure.
 */
static void udmabuf_platform_device_put(struct device* dev)
{
    platform_device_put(to_platform_device(dev));
}

/**
 * udmabuf_platform_device_create() - Create udmabuf platform device and add to list.
 * @name:       device name or NULL.
 * @id:         device id or negative integer.
 * @size:       buffer size.
 * @option:     option.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_platform_device_create(const char* name, int id, unsigned int size, u64 option)
{
    struct platform_device*      pdev   = NULL;
    struct udmabuf_device_entry* entry  = NULL;
    int                          retval = 0;
    u64                          dma_mask_size = udmabuf_get_option_dma_mask_size(option);

    if (size == 0)
        return -EINVAL;

    pdev = platform_device_alloc(DRIVER_NAME, id);
    if (IS_ERR_OR_NULL(pdev)) {
        retval = PTR_ERR(pdev);
        pdev   = NULL;
        pr_err(DRIVER_NAME ": platform_device_alloc(%s,%d) failed. return=%d\n", DRIVER_NAME, id, retval);
        goto failed;
    }

    if (!pdev->dev.dma_mask)
        pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

    if (dma_mask_size != 0) {
        pdev->dev.coherent_dma_mask = DMA_BIT_MASK(dma_mask_size);
        *pdev->dev.dma_mask         = DMA_BIT_MASK(dma_mask_size);
    } else {
        pdev->dev.coherent_dma_mask = DMA_BIT_MASK(dma_mask_bit);
        *pdev->dev.dma_mask         = DMA_BIT_MASK(dma_mask_bit);
    }

    entry = udmabuf_device_list_create_entry(&pdev->dev,
                                             name,
                                             id,
                                             size,
                                             option,
                                             udmabuf_platform_device_del,
                                             udmabuf_platform_device_put);
    if (IS_ERR_OR_NULL(entry)) {
        retval = PTR_ERR(entry);
        entry  = NULL;
        pr_err(DRIVER_NAME ": device create entry failed. return=%d\n", retval);
        goto failed;
    }

    /*
     * platform_device_add() calls udmabuf_platform_driver_probe()
     */
    retval = platform_device_add(pdev);
    if (retval != 0) {
        pr_err(DRIVER_NAME ": platform_device_add failed. return=%d\n", retval);
        goto failed;
    }

    if (dev_get_drvdata(&pdev->dev) == NULL) {
        pr_err(DRIVER_NAME ": object of %s is none.", dev_name(&pdev->dev));
        platform_device_del(pdev);
        retval = -ENODEV;
        goto failed;
    }
    
    return 0;

 failed:
    if (entry != NULL) {
        udmabuf_device_list_delete_entry(entry);
    }
    if (pdev  != NULL) {
        platform_device_put(pdev);
    }
    return retval;
}

/**
 * udmabuf_platform_device_remove() - Remove call for the platform device driver.
 * @dev:        handle to the device structure.
 * @obj:        Pointer to the udmabuf object.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_platform_device_remove(struct device *dev, struct udmabuf_object *obj)
{
    int retval = 0;

    if (obj != NULL) {
#if (USE_OF_RESERVED_MEM == 1)
        bool of_reserved_mem = obj->of_reserved_mem;
#endif
        retval = udmabuf_object_destroy(obj);
        if (retval != 0) {
            dev_set_drvdata(dev, NULL);
#if (USE_OF_RESERVED_MEM == 1)
            if (of_reserved_mem) {
                of_reserved_mem_device_release(dev);
            }
#endif
        }
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
 * udmabuf_platform_device_probe()  - Probe call for the platform device driver.
 * @dev:        handle to the device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * It does all the memory allocation and registration for the device.
 */
static int udmabuf_platform_device_probe(struct device *dev)
{
    int                    retval       = 0;
    int                    prop_status  = 0;
    u32                    u32_value    = 0;
    u64                    u64_value    = 0;
    size_t                 size         = 0;
    int                    minor_number = -1;
    struct udmabuf_object* obj          = NULL;
    const char*            device_name  = NULL;

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
     * udmabuf_object_create()
     */
    obj = udmabuf_object_create(device_name, dev, minor_number);
    if (IS_ERR_OR_NULL(obj)) {
        retval = PTR_ERR(obj);
        dev_err(dev, "object create failed. return=%d\n", retval);
        obj = NULL;
        retval = (retval == 0) ? -EINVAL : retval;
        goto failed;
    }
    /*
     * mutex_lock() then dev_set_drvdata()
     */
    mutex_lock(&obj->sem);
    dev_set_drvdata(dev, obj);
    /*
     * set size
     */
    obj->size = size;
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
            goto failed_with_unlock;
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
            obj->of_reserved_mem = 1;
        } else if (retval != -ENODEV) {
            dev_err(dev, "of_reserved_mem_device_init failed. return=%d\n", retval);
            goto failed_with_unlock;
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
    if (obj->of_reserved_mem == 0)
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
            goto failed_with_unlock;
        }
#else
        of_dma_configure(dev, dev->of_node);
#endif
    }
#endif
#if (USE_QUIRK_MMAP == 1)
    {
        int quirk_mmap_mode;
        if (udmabuf_get_quirk_mmap_property(dev, &quirk_mmap_mode, true) == 0)
            udmabuf_set_quirk_mmap_mode(obj, quirk_mmap_mode);
        /*
         * quirk-mmap-on  property
         */
        if (of_property_read_bool(dev->of_node, "quirk-mmap-on")) {
            udmabuf_set_quirk_mmap_mode(obj, QUIRK_MMAP_MODE_ALWAYS_ON);
        }
        /*
         * quirk-mmap-off property
         */
        if (of_property_read_bool(dev->of_node, "quirk-mmap-off")) {
            udmabuf_set_quirk_mmap_mode(obj, QUIRK_MMAP_MODE_ALWAYS_OFF);
        }
        /*
         * quirk-mmap-auto property
         */
        if (of_property_read_bool(dev->of_node, "quirk-mmap-auto")) {
            udmabuf_set_quirk_mmap_mode(obj, QUIRK_MMAP_MODE_AUTO);
        }
#if (USE_QUIRK_MMAP_PAGE == 1)
        /*
         * quirk-mmap-page property
         */
        if (of_property_read_bool(dev->of_node, "quirk-mmap-page")) {
            udmabuf_set_quirk_mmap_mode(obj, QUIRK_MMAP_MODE_PAGE);
        }
#endif
    }
#endif
    /*
     * sync-mode property
     */
    if (of_property_read_u32(dev->of_node, "sync-mode", &u32_value) == 0) {
        if ((u32_value < SYNC_MODE_MIN) || (u32_value > SYNC_MODE_MAX)) {
            dev_err(dev, "invalid sync-mode property value=%d\n", u32_value);
            goto failed_with_unlock;
        }
        obj->sync_mode &= ~SYNC_MODE_MASK;
        obj->sync_mode |= (int)u32_value;
    }
    /*
     * sync-always property
     */
    if (of_property_read_bool(dev->of_node, "sync-always")) {
        obj->sync_mode |= SYNC_ALWAYS;
    }
    /*
     * sync-direction property
     */
    if (of_property_read_u32(dev->of_node, "sync-direction", &u32_value) == 0) {
        if (u32_value > 2) {
            dev_err(dev, "invalid sync-direction property value=%d\n", u32_value);
            goto failed_with_unlock;
        }
        obj->sync_direction = (int)u32_value;
    }
    /*
     * sync-offset property
     */
    if (of_property_read_ulong(dev->of_node, "sync-offset", &u64_value) == 0) {
        if (u64_value >= obj->size) {
            dev_err(dev, "invalid sync-offset property value=%llu\n", u64_value);
            goto failed_with_unlock;
        }
        obj->sync_offset = (int)u64_value;
    }
    /*
     * sync-size property
     */
    if (of_property_read_ulong(dev->of_node, "sync-size", &u64_value) == 0) {
        if (obj->sync_offset + u64_value > obj->size) {
            dev_err(dev, "invalid sync-size property value=%llu\n", u64_value);
            goto failed_with_unlock;
        }
        obj->sync_size = (size_t)u64_value;
    } else {
        obj->sync_size = obj->size;
    }
    /*
     * udmabuf_object_setup()
     */
    retval = udmabuf_object_setup(obj);
    if (retval) {
        dev_err(dev, "object setup failed. return=%d\n", retval);
        goto failed_with_unlock;
    }

    mutex_unlock(&obj->sem);

    if (info_enable) {
        udmabuf_object_info(obj);
    }

    return 0;

 failed_with_unlock:
    mutex_unlock(&obj->sem);
 failed:
    if (obj != NULL) {
        udmabuf_platform_device_remove(dev, obj);
    } else {
        dev_set_drvdata(dev, NULL);
    }

    return retval;
}

/**
 * DOC: Udmabuf Child Device section.
 *
 * This section defines the udmabuf sub device.
 *
 * * udmabuf_child_device_create() - Create udmabuf child device and add to device list.
 * * udmabuf_child_device_delete() - Delete udmabuf child device after remove from device list.
 */

/**
 * udmabuf_child_device_delete()   - Delete udmabuf child device after remove from device list.
 * @dev:        handle to the device structure.
 */
static void udmabuf_child_device_delete(struct device* dev)
{
    char* device_name = kstrdup(dev_name(dev), GFP_KERNEL);

    udmabuf_object_destroy(dev_get_drvdata(dev));

    if (info_enable) {
        pr_info(DRIVER_NAME ": %s removed.\n", ((device_name) ? device_name: ""));
    }
    if (device_name)
        kfree(device_name);
}

/**
 * udmabuf_child_device_create() - Create udmabuf child device and add to list.
 * @name:       device name or NULL.
 * @id:         device id or negative integer.
 * @size:       buffer size.
 * @option      option.
 * @parent:     parent device.
 * Return:      Success(=0) or error status(<0).
 */
static int udmabuf_child_device_create(const char* name, int id, unsigned int size, u64 option, struct device* parent)
{
    const char*                  device_name = NULL;
    struct udmabuf_object*       obj         = NULL;
    struct udmabuf_device_entry* entry       = NULL;
    int                          retval      = 0;

    pr_debug(DRIVER_NAME ": child device create start.\n");

    if (size == 0)
        return -EINVAL;

    /*
     * device-name property
     */
    if ((name == NULL) && (id < 0))
        device_name = DRIVER_NAME;
    else
        device_name = name;
    /*
     * udmabuf_object_create()
     */
    obj = udmabuf_object_create(device_name, parent, id);
    if (IS_ERR_OR_NULL(obj)) {
        retval = PTR_ERR(obj);
        pr_err(DRIVER_NAME ": object create failed. return=%d\n", retval);
        obj = NULL;
        retval = (retval == 0) ? -EINVAL : retval;
        goto failed;
    }
    /*
     * mutex_lock()
     */
    mutex_lock(&obj->sem);
    /*
     * set size
     */
    obj->size = size;
#if (USE_QUIRK_MMAP == 1)
    /*
     * set quirk_mmap_mode
     */
    udmabuf_set_quirk_mmap_mode(obj, udmabuf_get_option_quirk_mmap_mode(option));
#endif
    /*
     * create entry
     */
    entry = udmabuf_device_list_create_entry(obj->sys_dev,
                                             name,
                                             id,
                                             size,
                                             option,
                                             NULL,
                                             udmabuf_child_device_delete);
    if (IS_ERR_OR_NULL(entry)) {
        retval = PTR_ERR(entry);
        entry  = NULL;
        dev_err(obj->sys_dev, "device create entry failed. return=%d\n", retval);
        goto failed_with_unlock;
    }
    /*
     * udmabuf_object_setup()
     */
    retval = udmabuf_object_setup(obj);
    if (retval) {
        dev_err(obj->sys_dev, "object setup failed. return=%d\n", retval);
        goto failed_with_unlock;
    }

    mutex_unlock(&obj->sem);

    if (info_enable) {
        udmabuf_object_info(obj);
    }

    if (info_enable) {
        pr_info(DRIVER_NAME ": %s installed.\n", dev_name(obj->sys_dev));
    }
    return 0;

 failed_with_unlock:
    mutex_unlock(&obj->sem);
 failed:
    if (entry != NULL) {
        udmabuf_device_list_delete_entry(entry);
    }
    if (obj   != NULL) {
        udmabuf_object_destroy(obj);
    }
    return retval;
}

/**
 * DOC: Udmabuf Static Devices section.
 *
 * This section defines the udmabuf device to be created with arguments when loaded
 * into ther kernel with insmod.
 *
 * * udmabuf_available_bus_type_list[] - List of bus_type available for udmabuf static device.
 * * udmabuf_find_available_bus_type() - Find available bus_type by name.
 * * udmabuf_static_parent_device      - Parent device of udmabuf static device or NULL or ERR_PTR.
 * * udmabuf_static_device_create()    - Create udmabuf static device and add to list.
 */
/**
 * * udmabuf_available_bus_type_list[] - List of bus_type available for udmabuf static device.
 */
#if defined(CONFIG_ARM_AMBA) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0))
extern struct bus_type      amba_bustype;
#define AMBA_BUS_TYPE      &amba_bustype,
#else
#define AMBA_BUS_TYPE
#endif
#if defined(CONFIG_PCI)
extern struct bus_type      pci_bus_type;
#define PCI_BUS_TYPE       &pci_bus_type,
#else
#define PCI_BUS_TYPE
#endif
#if defined(CONFIG_PCIEPORTBUS) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
extern struct bus_type      pcie_port_bus_type;
#define PCIE_PORT_BUS_TYPE &pcie_port_bus_type,
#else
#define PCIE_PORT_BUS_TYPE 
#endif

static struct bus_type* udmabuf_available_bus_type_list[] = {
    AMBA_BUS_TYPE
    PCI_BUS_TYPE
    PCIE_PORT_BUS_TYPE
    NULL
};

/**
 * udmabuf_find_available_bus_type() - Find available bus_type by name.
 * @name:       bus name string.
 * @name_len:   length of @name.
 * Return:      pointer to the bus_type or NULL.
 */
static struct bus_type* udmabuf_find_available_bus_type(char* name, int name_len)
{
    int i;
    if ((name == NULL) || (name_len == 0))
        return NULL;
    for (i = 0; udmabuf_available_bus_type_list[i] != NULL; i++) {
        const char* bus_name;
        if (udmabuf_available_bus_type_list[i] == NULL)
            break;
        bus_name = udmabuf_available_bus_type_list[i]->name;
        if (name_len != strlen(bus_name))
            continue;
        if (strncmp(name, bus_name, name_len) == 0)
            break;
    }
    return udmabuf_available_bus_type_list[i];
}

/**
 * udmabuf_static_bind_parse() - Parse bind string to get bus_type and device_name.
 * @bind:        string to parse.
 * @bus_type:    pointer to store bus_type found.
 * @device_name: pointer to store device_name found.
 * Return:       Success(=0) or error status(<0).
 */
static int udmabuf_static_parse_bind(char* bind, struct bus_type** bus_type, char** device_name)
{
    int   retval   = 0;
    char* next_ptr = strchr(bind, '/');

    if (!next_ptr) {
        *bus_type    = &platform_bus_type;
        *device_name = bind;
        retval       = 0;
    } else {
        char*            name           = bind;
        int              name_len       = next_ptr - bind;
        struct bus_type* found_bus_type = udmabuf_find_available_bus_type(name, name_len);
        if (found_bus_type == NULL) {
            retval       = -EINVAL;
        } else {
            *bus_type    = found_bus_type;
            *device_name = next_ptr+1;
            retval       = 0;
        }
    }
    return retval;
}

/**
 * udmabuf_static_parent_device - Parent device of udmabuf static device or NULL or ERR_PTR.
 */
static struct device* udmabuf_static_parent_device = NULL;

/**
 * udmabuf_static_device_create() - Create udmabuf static device and add to list.
 * @name:       device name or NULL.
 * @id:         device id or negative integer.
 * @size:       buffer size.
 * Return:      Success(=0) or error status(<0).
 */
static void udmabuf_static_device_create(const char* name, int id, unsigned int size)
{
    if ((bind != NULL) && (udmabuf_static_parent_device == NULL)) {
        struct device*   parent      = NULL;
        struct bus_type* bus_type    = NULL;
        char*            device_name = NULL;
        int              retval;
        retval = udmabuf_static_parse_bind(bind, &bus_type, &device_name);
        if (retval) {
            udmabuf_static_parent_device = ERR_PTR(-EINVAL);
            pr_err(DRIVER_NAME ": bind error: %s is not support bus\n", bind);
            return;
        }
        parent = bus_find_device_by_name(bus_type, NULL, device_name);
        if (IS_ERR_OR_NULL(parent)) {
            udmabuf_static_parent_device = (parent == NULL)? ERR_PTR(-EINVAL) : parent;
            pr_err(DRIVER_NAME ": bind error: device(%s) not found in bus(%s)\n", device_name, bus_type->name);
            return;
        } else {
            udmabuf_static_parent_device = parent;
        }
    }

    if (IS_ERR(udmabuf_static_parent_device))
        return;

    if (udmabuf_static_parent_device)
        udmabuf_child_device_create(name, id, size, 0, udmabuf_static_parent_device);
    else
        udmabuf_platform_device_create(name, id, size, 0);
}

#define DEFINE_UDMABUF_STATIC_DEVICE_PARAM(__num)                        \
    static ulong     udmabuf ## __num = 0;                               \
    module_param(    udmabuf ## __num, ulong, S_IRUGO);                  \
    MODULE_PARM_DESC(udmabuf ## __num, DRIVER_NAME #__num " buffer size");

#define CALL_UDMABUF_STATIC_DEVICE_CREATE(__num)                         \
    if (udmabuf ## __num != 0) {                                         \
        ida_simple_remove(&udmabuf_device_ida, __num);                   \
        udmabuf_static_device_create(NULL, __num, udmabuf ## __num);     \
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
static int udmabuf_static_device_create_all(void)
{
    CALL_UDMABUF_STATIC_DEVICE_CREATE(0);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(1);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(2);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(3);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(4);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(5);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(6);
    CALL_UDMABUF_STATIC_DEVICE_CREATE(7);
    return (IS_ERR(udmabuf_static_parent_device))? PTR_ERR(udmabuf_static_parent_device) : 0;
}

/**
 * DOC: Udmabuf Platform Driver section.
 *
 * This section defines the udmabuf platform driver.
 *
 * * udmabuf_platform_driver_probe()   - Probe call for platform_device_add().
 * * udmabuf_platform_driver_remove()  - Remove call for platform_device_del().
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

    retval = udmabuf_platform_device_probe(&pdev->dev);

    if (retval != 0) {
        dev_err(&pdev->dev, "driver probe failed. return=%d\n", retval);
    } else if (info_enable) {
        dev_info(&pdev->dev, "driver installed.\n");
    }
    return retval;
}
/**
 * _udmabuf_platform_driver_remove() -  Remove call for the device.
 * @pdev:       Handle to the platform device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * Unregister the device after releasing the resources.
 */
static inline int _udmabuf_platform_driver_remove(struct platform_device *pdev)
{
    struct udmabuf_object* this   = dev_get_drvdata(&pdev->dev);
    int                    retval = 0;

    dev_dbg(&pdev->dev, "driver remove start.\n");

    retval = udmabuf_platform_device_remove(&pdev->dev, this);

    if (retval != 0) {
        dev_err(&pdev->dev, "driver remove failed. return=%d\n", retval);
    } else if (info_enable) {
        dev_info(&pdev->dev, "driver removed.\n");
    }
    return retval;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
/**
 * udmabuf_platform_driver_remove() -  Remove call for the device.
 * @pdev:       Handle to the platform device structure.
 * Return:      Success(=0) or error status(<0).
 *
 * Unregister the device after releasing the resources.
 */
static int udmabuf_platform_driver_remove(struct platform_device *pdev)
{
    return _udmabuf_platform_driver_remove(pdev);
}
#else
/**
 * udmabuf_platform_driver_remove() -  Remove call for the device.
 * @pdev:       Handle to the platform device structure.
 * Return:      void
 *
 * Unregister the device after releasing the resources.
 */
static void udmabuf_platform_driver_remove(struct platform_device *pdev)
{
    _udmabuf_platform_driver_remove(pdev);
}
#endif

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
 * DOC: u-dma-buf Device In-Kernel Interface.
 *
 * * u_dma_buf_device_search()           - Search u-dma-buf device by name or id.
 * * u_dma_buf_device_create()           - Create u-dma-buf device for in-kernel.
 * * u_dma_buf_device_remove()           - Remove u-dma-buf device for in-kernel.
 * * u_dma_buf_device_getmap()           - Get mapping information from u-dma-buf device for in-kernel.
 * * u_dma_buf_device_sync()             - Sync for CPU/Device u-dma-buf device for in-kernel.
 * * u_dma_buf_find_available_bus_type() - Find available bus_type by name.
 * * u_dma_buf_available_bus_type_list[] - List of bus_type available by u-dma-buf.
 */
/**
 * u-dma-buf-funcs.h - u-dma-buf in-kernel functions header file
 *
 * This source code(u-dma-buf.c) has built-in header file(u-dma-buf-funcs.h) 
 * so that it can be built with only one source code.
 * To generate a header file (u-dma-buf-funcs.h) from this source code (u-dma-buf.c), 
 * do the following
 * 
 * sed -n '/^\/\*\*\*\*\*\*\*\*\*\*\**$/,/\**\*\*\*\*\*\*\*\*\*\*\/$/p' u-dma-buf.c >  u-dma-buf-funcs.h
 * sed -n '/^#ifndef.*U_DMA_BUF_FUNCS_H/,/^#endif.*U_DMA_BUF_FUNCS_H/p' u-dma-buf.c >> u-dma-buf-funcs.h
 * 
 */
#if (IN_KERNEL_FUNCTIONS == 1)
#ifndef  U_DMA_BUF_FUNCS_H
#define  U_DMA_BUF_FUNCS_H
struct device*   u_dma_buf_device_search(const char* name, int id);
struct device*   u_dma_buf_device_create(const char* name, int id, size_t size, u64 option, struct device* parent);
int              u_dma_buf_device_remove(struct device *dev);
int              u_dma_buf_device_getmap(struct device *dev, size_t* size, void** virt_addr, dma_addr_t* phys_addr);
int              u_dma_buf_device_sync(struct device *dev, int command, int direction, u64 offset, ssize_t size);
struct bus_type* u_dma_buf_find_available_bus_type(char* name, int name_len);
#endif /* #ifndef U_DMA_BUF_FUNCS_H */
#endif /* #if (IN_KERNEL_FUNCTIONS == 1) */
/**
 * u_dma_buf_device_search() - Search u-dma-buf device by name or id.
 * @name:       device name or NULL.
 * @id:         device id or negative integer.
 * Return:      handle to u-dma-buf device structure(>=0) or error status(<0).
 */
#if (IN_KERNEL_FUNCTIONS == 1)
struct device* u_dma_buf_device_search(const char* name, int id)
{
    struct udmabuf_device_entry* entry = udmabuf_device_list_search(NULL, name, id);

    if (entry == NULL)
        return ERR_PTR(-ENODEV);
    else
        return entry->dev;
}
EXPORT_SYMBOL(u_dma_buf_device_search);
#endif

/**
 * u_dma_buf_device_create() - Create u-dma-buf device for in-kernel.
 * @name:       device name or NULL.
 * @id:         device id or negative integer.
 * @size:       buffer size.
 * @option:     option. dma_mask=option[7:0], quirk_mmap_mode=option[12:10]
 * @parent:     parent device or NULL.
 * Return:      handle to u-dma-buf device structure(>=0) or error status(<0).
 */
#if (IN_KERNEL_FUNCTIONS == 1)
struct device* u_dma_buf_device_create(const char* name, int id, size_t size, u64 option, struct device* parent)
{
    int            result = 0;
    struct device* dev;

    if (parent) {
        result = udmabuf_child_device_create(name, id, size, option, parent);
    } else {
        result = udmabuf_platform_device_create(name, id, size, option);
    }

    if (result)
        return ERR_PTR(result);

    dev = u_dma_buf_device_search(name, id);
    return dev;
}
EXPORT_SYMBOL(u_dma_buf_device_create);
#endif

/**
 * u_dma_buf_device_remove() - Remove u-dma-buf device for in-kernel.
 * @dev:        handle to the u-dma-buf device structure.
 * Return:      Success(=0) or error status(<0).
 */
#if (IN_KERNEL_FUNCTIONS == 1)
int u_dma_buf_device_remove(struct device *dev)
{
    struct udmabuf_device_entry* entry = udmabuf_device_list_search(dev, NULL, -1);
    if (entry == NULL)
        return -EINVAL;

    udmabuf_device_list_remove_entry(entry);
    return 0;
}
EXPORT_SYMBOL(u_dma_buf_device_remove);
#endif

/**
 * u_dma_buf_device_getmap() - Get mapping information from u-dma-buf device for in-kernel.
 * @dev:        handle to the u-dma-buf device structure.
 * @size        Pointer to the buffer size for output.
 * @virt_addr   Pointer to the virtual address for output.
 * @phys_addr   Pointer to the physical address for output.
 * Return:      Success(=0) or error status(<0).
 */
#if (IN_KERNEL_FUNCTIONS == 1)
int u_dma_buf_device_getmap(struct device *dev, size_t* size, void** virt_addr, dma_addr_t* phys_addr)
{
    struct udmabuf_device_entry* entry;
    struct udmabuf_object*       this;

    entry = udmabuf_device_list_search(dev, NULL, -1);
    if (entry == NULL)
        return -EINVAL;

    this = dev_get_drvdata(entry->dev);
    if (this == NULL)
        return -ENODEV;

    if (!mutex_trylock(&this->sem))
        return -EBUSY;

    if (size      != NULL) {*size      = this->size     ;}
    if (virt_addr != NULL) {*virt_addr = this->virt_addr;}
    if (phys_addr != NULL) {*phys_addr = this->phys_addr;}

    mutex_unlock(&this->sem);
    return 0;
}
EXPORT_SYMBOL(u_dma_buf_device_getmap);
#endif

/**
 * u_dma_buf_device_sync() - Sync for CPU/Device u-dma-buf device for in-kernel.
 * @dev:        handle to the u-dma-buf device structure.
 * @command     sync command (no_op=0, sync_for_cpu=1, sync_for_device=2)
 * @direction   sync direction (0 = DMA_BIDIRECTIONAL, 1 = DMA_TO_DEVICE, 2 = DMA_FROM_DEVICE)
 * @offset      sync offset.
 * @size        sync size.
 * Return:      Success(=0) or error status(<0).
 */
#if (IN_KERNEL_FUNCTIONS == 1)
int u_dma_buf_device_sync(struct device *dev, int command, int direction, u64 offset, ssize_t size)
{
    struct udmabuf_device_entry* entry;
    struct udmabuf_object*       this;
    int                          result = 0;

    entry = udmabuf_device_list_search(dev, NULL, -1);
    if (entry == NULL)
        return -EINVAL;

    this = dev_get_drvdata(entry->dev);
    if (this == NULL)
        return -ENODEV;

    if (!mutex_trylock(&this->sem))
        return -EBUSY;

    switch(direction) {
        case 0   : this->sync_direction = 0; break;
        case 1   : this->sync_direction = 1; break;
        case 2   : this->sync_direction = 2; break;
        default  : /* none */                break;
    }
    if (offset >= 0) {this->sync_offset = offset;}
    if (size   >  0) {this->sync_size   = size  ;}
    
    switch (command) {
        case 0 :
            result = 0;
            break;
        case 1 :
            this->sync_for_cpu    = 1;
            result = udmabuf_sync_for_cpu(this);
            break;
        case 2 :
            this->sync_for_device = 1;
            result = udmabuf_sync_for_device(this);
            break;
        default:
            result = -EINVAL;
            break;
    }

    mutex_unlock(&this->sem);
    return result;
}
EXPORT_SYMBOL(u_dma_buf_device_sync);
#endif

/**
 * u_dma_buf_find_available_bus_type() - Find available bus_type by name.
 * @name:       bus name string.
 * @name_len:   length of @name.
 * Return:      pointer to the bus_type or NULL.
 */
#if (IN_KERNEL_FUNCTIONS == 1)
struct bus_type* u_dma_buf_find_available_bus_type(char* name, int name_len)
{
    return udmabuf_find_available_bus_type(name, name_len);
}
EXPORT_SYMBOL(u_dma_buf_find_available_bus_type);
#endif

/**
 * u_dma_buf_available_bus_type_list[] - List of bus_type available by u-dma-buf.
 */
#if (IN_KERNEL_FUNCTIONS == 1)
struct bus_type** u_dma_buf_available_bus_type_list = &udmabuf_available_bus_type_list[0];
EXPORT_SYMBOL(u_dma_buf_available_bus_type_list);
#endif

/**
 * DOC: u-dma-buf Kernel Module Operations.
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
    udmabuf_device_list_cleanup();
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

    if (CONFIG_INFO_ENABLE) {
        #define TO_STR(x) #x
        #define NUM_TO_STR(x) TO_STR(x)
        pr_info(DRIVER_NAME ": "
                "DEVICE_MAX_NUM="      NUM_TO_STR(DEVICE_MAX_NUM)      ","
                "UDMABUF_CONFIG="      NUM_TO_STR(UDMABUF_CONFIG)      ","
                "UDMABUF_DEBUG="       NUM_TO_STR(UDMABUF_DEBUG)       ","
                "USE_QUIRK_MMAP="      NUM_TO_STR(USE_QUIRK_MMAP)      ","
                "USE_QUIRK_MMAP_PAGE=" NUM_TO_STR(USE_QUIRK_MMAP_PAGE) ","
        #if defined(IS_DMA_COHERENT)
                "IS_DMA_COHERENT=1," 
        #endif
                "USE_DMA_BUF_EXPORT="  NUM_TO_STR(USE_DMA_BUF_EXPORT)  ","
                "USE_DEV_GROUPS="      NUM_TO_STR(USE_DEV_GROUPS)      ","
                "USE_OF_RESERVED_MEM=" NUM_TO_STR(USE_OF_RESERVED_MEM) ","
                "USE_OF_DMA_CONFIG="   NUM_TO_STR(USE_OF_DMA_CONFIG)   ","
                "USE_DEV_PROPERTY="    NUM_TO_STR(USE_DEV_PROPERTY)    ","
                "IN_KERNEL_FUNCTIONS=" NUM_TO_STR(IN_KERNEL_FUNCTIONS) ","
                "IOCTL_VERSION="       NUM_TO_STR(IOCTL_VERSION)        );
    }

    ida_init(&udmabuf_device_ida);
    INIT_LIST_HEAD(&udmabuf_device_list);
    mutex_init(&udmabuf_device_list_sem);

    retval = alloc_chrdev_region(&udmabuf_device_number, 0, 0, DRIVER_NAME);
    if (retval != 0) {
        pr_err(DRIVER_NAME ": couldn't allocate device major number. return=%d\n", retval);
        udmabuf_device_number = 0;
        goto failed;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    udmabuf_sys_class = class_create(THIS_MODULE, DRIVER_NAME);
#else
    udmabuf_sys_class = class_create(DRIVER_NAME);
#endif
    if (IS_ERR_OR_NULL(udmabuf_sys_class)) {
        retval = PTR_ERR(udmabuf_sys_class);
        udmabuf_sys_class = NULL;
        pr_err(DRIVER_NAME ": couldn't create sys class. return=%d\n", retval);
        retval = (retval == 0) ? -ENOMEM : retval;
        goto failed;
    }

    udmabuf_sys_class_set_attributes();

    udmabuf_static_device_reserve_minor_number_all();

    retval = platform_driver_register(&udmabuf_platform_driver);
    if (retval) {
        pr_err(DRIVER_NAME ": couldn't register platform driver. return=%d\n", retval);
        udmabuf_platform_driver_registerd = false;
        goto failed;
    } else {
        udmabuf_platform_driver_registerd = true;
    }

    retval = udmabuf_static_device_create_all();
    if (retval) {
        pr_err(DRIVER_NAME ": couldn't create static devices. return=%d\n", retval);
        goto failed;
    } 

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
