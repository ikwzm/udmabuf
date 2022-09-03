/*********************************************************************************
 *
 *       Copyright (C) 2015-2022 Ichiro Kawazome
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
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>

/**
 * DOC: U-dma-buf Manager Constants 
 */

MODULE_DESCRIPTION("U-dma-buf(User space mappable DMA buffer device driver) In Kernel Test Module");
MODULE_AUTHOR("ikwzm");
MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_VERSION     "4.0.0"
#define DRIVER_NAME        "u-dma-buf-kmod-test"
#define TEST_MODULE_NAME   "u-dma-buf-kmod-test"

static int       buffer_size = 4096;
module_param(    buffer_size, int, S_IRUGO);
MODULE_PARM_DESC(buffer_size , "udmabuf install/uninstall infomation enable");


/**
 * DOC: u-dma-buf Device In-Kernel Interface
 *
 * * u_dma_buf_device_search() - Search u-dma-buf device by name or id.
 * * u_dma_buf_device_create() - Create u-dma-buf device for in-kernel.
 * * u_dma_buf_device_remove() - Remove u-dma-buf device for in-kernel.
 * * u_dma_buf_device_getmap() - Get mapping information from u-dma-buf device for in-kernel.
 * * u_dma_buf_device_sync()   - Sync for CPU/Device u-dma-buf device for in-kernel.
 */
struct device* u_dma_buf_device_search(const char* name, int id);
struct device* u_dma_buf_device_create(const char* name, int id, unsigned int size);
int u_dma_buf_device_remove(struct device *dev);
int u_dma_buf_device_getmap(struct device *dev, size_t* size, void** virt_addr, dma_addr_t* phys_addr);
int u_dma_buf_device_sync(struct device *dev, int command, int direction, u64 offset, ssize_t size);

/**
 * u_dma_buf_kmod_test_init()
 */
static int __init u_dma_buf_kmod_test_init(void)
{
    int retval = 0;
    struct device*      test_dev;
    const  char*        name = TEST_MODULE_NAME;
    const  int          id   = PLATFORM_DEVID_AUTO;
    const  bool         do_test_search   = true;
    const  bool         do_test_getmap_1 = true;
    const  bool         do_test_getmap_2 = true;
    const  bool         do_test_getmap_3 = true;
    const  bool         do_test_memcpy   = true;

    if (1) {
        struct device* dev;
        printk(KERN_INFO "%s: u_dma_buf_device_create(%s,%d,%d) start", DRIVER_NAME, name, id, buffer_size);
        dev = u_dma_buf_device_create(name, id, buffer_size);
        if (IS_ERR(dev))
            printk(KERN_ERR  "%s: u_dma_buf_device_create(%s,%d,%d) return=%d"  , DRIVER_NAME, name, id, buffer_size, (int)PTR_ERR(dev));
        else if (dev == NULL)
            printk(KERN_INFO "%s: u_dma_buf_device_create(%s,%d,%d) return=NULL", DRIVER_NAME, name, id, buffer_size);
        else 
            printk(KERN_INFO "%s: u_dma_buf_device_create(%s,%d,%d) done"       , DRIVER_NAME, name, id, buffer_size);
        if (IS_ERR_OR_NULL(dev)) {
            retval   = (IS_ERR(dev))? PTR_ERR(dev) : -ENODEV;
            test_dev = NULL;
            goto fail;
        } else {
            test_dev = dev;
        }
    }

    if (do_test_search) {
        struct device* dev;
        printk(KERN_INFO "%s: u_dma_buf_device_search(%s,%d) start", DRIVER_NAME, name, id);
        dev = u_dma_buf_device_search(name, id);
        if (IS_ERR(dev))
            printk(KERN_ERR  "%s: u_dma_buf_device_search(%s,%d) return=%d\n"  , DRIVER_NAME, name, id, (int)PTR_ERR(dev));
        else if (dev == NULL)
            printk(KERN_ERR "%s: u_dma_buf_device_search(%s,%d) return=NULL\n", DRIVER_NAME, name, id);
        else if (dev != test_dev)
            printk(KERN_ERR  "%s: u_dma_buf_device_search(%s,%d) return=%pad but not %pad\n", DRIVER_NAME, name, id, &dev, &test_dev);
        else
            printk(KERN_INFO "%s: u_dma_buf_device_search(%s,%d) done\n", DRIVER_NAME, name, id);
    }

    if (do_test_getmap_1) {
        int        status;
        size_t     size;
        void*      virt_addr;
        dma_addr_t phys_addr;
        printk(KERN_INFO "%s: u_dma_buf_device_getmap() test(1) start", DRIVER_NAME);
        status = u_dma_buf_device_getmap(test_dev, &size, &virt_addr, &phys_addr);
        if (status) {
            printk(KERN_ERR  "%s: u_dma_buf_device_getmap() return=%d\n", DRIVER_NAME, status);
        } else {
            printk(KERN_INFO "%s: u_dma_buf_device_getmap() return=%d\n", DRIVER_NAME, status);
            printk(KERN_INFO "%s:     size      = %zu"  , DRIVER_NAME, size);
            printk(KERN_INFO "%s:     virt_addr = %px"  , DRIVER_NAME, &virt_addr); 
            printk(KERN_INFO "%s:     phys_addr = %pad" , DRIVER_NAME, &phys_addr); 
        }
    }

    if (do_test_getmap_2) {
        int        status;
        size_t     size;
        dma_addr_t phys_addr;
        printk(KERN_INFO "%s: u_dma_buf_device_getmap() test(2) start", DRIVER_NAME);
        status = u_dma_buf_device_getmap(test_dev, &size, NULL, &phys_addr);
        if (status) {
            printk(KERN_ERR  "%s: u_dma_buf_device_getmap() return=%d\n", DRIVER_NAME, status);
        } else {
            printk(KERN_INFO "%s: u_dma_buf_device_getmap() return=%d\n", DRIVER_NAME, status);
            printk(KERN_INFO "%s:     size      = %zu"  , DRIVER_NAME, size);
            printk(KERN_INFO "%s:     phys_addr = %pad" , DRIVER_NAME, &phys_addr); 
        }
    }

    if (do_test_getmap_3) {
        int        status;
        dma_addr_t phys_addr;
        printk(KERN_INFO "%s: u_dma_buf_device_getmap() test(3) start", DRIVER_NAME);
        status = u_dma_buf_device_getmap(test_dev, NULL, NULL, &phys_addr);
        if (status) {
            printk(KERN_ERR  "%s: u_dma_buf_device_getmap() return=%d\n", DRIVER_NAME, status);
        } else {
            printk(KERN_INFO "%s: u_dma_buf_device_getmap() return=%d\n", DRIVER_NAME, status);
            printk(KERN_INFO "%s:     phys_addr = %pad" , DRIVER_NAME, &phys_addr); 
        }
    }

    if (do_test_memcpy) {
        int        status;
        size_t     size;
        void*      buffer_addr;
        void*      load_addr;
        int        i;
        int        err  = 0;
        const u8   data = 0xCC;
        
        load_addr = kzalloc(size, GFP_KERNEL);
        if (IS_ERR_OR_NULL(load_addr)) {
            printk(KERN_ERR  "%s: can not alocate load buffer\n", DRIVER_NAME);
            retval = (IS_ERR(load_addr))? PTR_ERR(load_addr) : -ENOMEM;
            goto fail;
        }
        
        printk(KERN_INFO "%s: u_dma_buf_device_getmap() test(4) start", DRIVER_NAME);
        status = u_dma_buf_device_getmap(test_dev, &size, &buffer_addr, NULL);
        if (status) {
            printk(KERN_ERR  "%s: u_dma_buf_device_getmap() return=%d\n", DRIVER_NAME, status);
        } else {
            printk(KERN_INFO "%s: u_dma_buf_device_getmap() return=%d\n", DRIVER_NAME, status);
            printk(KERN_INFO "%s:     size      = %zu"  , DRIVER_NAME, size);
            printk(KERN_INFO "%s:     virt_addr = %px"  , DRIVER_NAME, &buffer_addr); 
        }

        printk(KERN_INFO "%s: u_dma_buf_device_sync() start", DRIVER_NAME);
        status = u_dma_buf_device_sync(test_dev, 1, 0, 0, size);
        if (status) 
            printk(KERN_ERR  "%s: u_dma_buf_device_sync() return=%d", DRIVER_NAME, status);
        else 
            printk(KERN_INFO "%s: u_dma_buf_device_sync() return=%d", DRIVER_NAME, status);

        printk(KERN_INFO "%s: memset(%px, 0x%02X, %zu) start", DRIVER_NAME, &buffer_addr, data, size);
        memset(buffer_addr, data, size);
        printk(KERN_INFO "%s: memset(%px, 0x%02X, %zu) done ", DRIVER_NAME, &buffer_addr, data, size);

        printk(KERN_INFO "%s: u_dma_buf_device_sync() start", DRIVER_NAME);
        status = u_dma_buf_device_sync(test_dev, 2, 0, 0, size);
        if (status) 
            printk(KERN_ERR  "%s: u_dma_buf_device_sync() return=%d", DRIVER_NAME, status);
        else 
            printk(KERN_INFO "%s: u_dma_buf_device_sync() return=%d", DRIVER_NAME, status);

        printk(KERN_INFO "%s: u_dma_buf_device_sync() start", DRIVER_NAME);
        status = u_dma_buf_device_sync(test_dev, 1, -1, 0, size);
        if (status) 
            printk(KERN_ERR  "%s: u_dma_buf_device_sync() return=%d", DRIVER_NAME, status);
        else 
            printk(KERN_INFO "%s: u_dma_buf_device_sync() return=%d", DRIVER_NAME, status);

        printk(KERN_INFO "%s: memcpy(%px, %px, %zu) start", DRIVER_NAME, &load_addr, &buffer_addr, size);
        memcpy(load_addr, buffer_addr, size);
        printk(KERN_INFO "%s: memcpy(%px, %px, %zu) done ", DRIVER_NAME, &load_addr, &buffer_addr, size);
        err = 0;
        for (i = 0; i < size; i++) {
            if (((u8*)load_addr)[i] != data)
                err++;
        }
        if (err > 0) 
            printk(KERN_ERR  "%s: mem check error %d\n", DRIVER_NAME, err);
        else
            printk(KERN_INFO "%s: mem check success\n" , DRIVER_NAME);

        printk(KERN_INFO "%s: u_dma_buf_device_sync() start", DRIVER_NAME);
        status = u_dma_buf_device_sync(test_dev, 2, -1, 0, size);
        if (status) 
            printk(KERN_ERR  "%s: u_dma_buf_device_sync() return=%d", DRIVER_NAME, status);
        else 
            printk(KERN_INFO "%s: u_dma_buf_device_sync() return=%d", DRIVER_NAME, status);
        printk(KERN_INFO "%s: memcpy(%px, %px, %zu) start", DRIVER_NAME, &load_addr, &buffer_addr, size);

        kfree(load_addr);
    }

    if (test_dev) {
        int        status;
        printk(KERN_INFO "%s: u_dma_buf_device_remove() start", DRIVER_NAME);
        status = u_dma_buf_device_remove(test_dev);
        if (status) {
            printk(KERN_ERR  "%s: u_dma_buf_device_remove() return=%d\n", DRIVER_NAME, status);
        } else {
            printk(KERN_INFO "%s: u_dma_buf_device_remove() return=%d\n", DRIVER_NAME, status);
        }
    }
    return 0;
    
 fail:
    if (test_dev) {
        u_dma_buf_device_remove(test_dev);
    }
    return retval;
}

/**
 * u_dma_buf_kmod_test_exit()
 */
static void __exit u_dma_buf_kmod_test_exit(void)
{
}

module_init(u_dma_buf_kmod_test_init);
module_exit(u_dma_buf_kmod_test_exit);
