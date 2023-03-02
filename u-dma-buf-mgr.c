/*********************************************************************************
 *
 *       Copyright (C) 2015-2023 Ichiro Kawazome
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
#include <linux/uaccess.h>

/**
 * DOC: U-dma-buf Manager Constants 
 */

MODULE_DESCRIPTION("U-dma-buf(User space mappable DMA buffer device driver) Manager");
MODULE_AUTHOR("ikwzm");
MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_VERSION     "4.3.0-rc1"
#define DRIVER_NAME        "u-dma-buf-mgr"

/**
 * DOC: u-dma-buf Device In-Kernel Interface
 *
 * * u_dma_buf_device_search() - Search u-dma-buf device by name or id.
 * * u_dma_buf_device_create() - Create u-dma-buf device for in-kernel.
 * * u_dma_buf_device_remove() - Remove u-dma-buf device for in-kernel.
 */
struct device* u_dma_buf_device_search(const char* name, int id);
struct device* u_dma_buf_device_create(const char* name, int id, size_t size, u64 option, struct device* parent);
int            u_dma_buf_device_remove(struct device *dev);

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

#define UDMABUF_MGR_BUFFER_SIZE 1024

/**
 * enum   udmabuf_manager_state - udmabuf manager state enumeration.
 */
enum   udmabuf_manager_state {
    udmabuf_manager_init_state    ,
    udmabuf_manager_create_command,
    udmabuf_manager_delete_command,
    udmabuf_manager_parse_error   ,
    udmabuf_manager_size_error    ,
    udmabuf_manager_bind_error    ,
};

/**
 * struct udmabuf_manager_data - udmabuf manager data structure.
 */
struct udmabuf_manager_data {
    const char*                  device_name;
    int                          minor_number;
    size_t                       size;
    u64                          option;
    struct device*               parent;
    enum udmabuf_manager_state   state;
    unsigned int                 buffer_offset;
    char                         buffer[UDMABUF_MGR_BUFFER_SIZE];
    char*                        parse;
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
    this->parent        = NULL;
    this->state         = udmabuf_manager_init_state;
    this->buffer_offset = 0;
    this->parse         = this->buffer;
}

/**
 * udmabuf_manager_supported_bus_type_list - udmabuf manager supported bus type list.
 */
#ifdef CONFIG_ARM_AMBA
extern struct bus_type amba_bustype;
#endif
#ifdef CONFIG_PCI
extern struct bus_type pci_bus_type;
#endif
#ifdef CONFIG_PCIEPORTBUS
extern struct bus_type pcie_port_bus_type;
#endif

static struct bus_type* udmabuf_manager_supported_bus_type_list[] = {
#ifdef CONFIG_ARM_AMBA
    &amba_bustype,
#endif
#ifdef CONFIG_PCI
    &pci_bus_type,
#endif
#ifdef CONFIG_PCIEPORTBUS
    &pcie_port_bus_type,
#endif
    NULL
};

/**
 * udmabuf_manager_parse_bus_type() - udmabuf manager parse bus type
 * @ptr:        Pointer to the parse string.
 * Return:      Pointer to found bus type or NULL.
 */
static struct bus_type* udmabuf_manager_parse_bus_type(char* ptr)
{
    int i;
    for (i = 0; udmabuf_manager_supported_bus_type_list[i] != NULL; i++) {
        const char* bus_name     = udmabuf_manager_supported_bus_type_list[i]->name;
        int         bus_name_len = strlen(bus_name);
        if (strncmp(ptr, bus_name, bus_name_len) == 0)
            break;
    }
    if (udmabuf_manager_supported_bus_type_list[i] != NULL)
        return udmabuf_manager_supported_bus_type_list[i];
    else
        return NULL;
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
        char* ptr;
        while((ptr = strsep(&parse_buffer, " \t")) != 0) {
            if (*ptr) break;
        }
        if (ptr == NULL) {
            this->state = udmabuf_manager_parse_error;
            this->parse = this->buffer;
            goto failed;
        } else if (strncmp(ptr, "create", strlen("create")) == 0) {
            this->state = udmabuf_manager_create_command;
        } else if (strncmp(ptr, "delete", strlen("delete")) == 0) {
            this->state = udmabuf_manager_delete_command;
        } else {
            this->state = udmabuf_manager_parse_error;
            this->parse = ptr;
            goto failed;
        }
        while((ptr = strsep(&parse_buffer, " \t")) != 0) {
            if (*ptr) break;
        }
        if (ptr == NULL) {
            this->state = udmabuf_manager_parse_error;
            this->parse = this->buffer;
            goto failed;
        } else {
            this->device_name = ptr;
        }
        if (this->state == udmabuf_manager_create_command) {
            struct bus_type* bus_type    = NULL;
            struct device*   parent      = NULL;
            char*            parent_name = NULL;
            u64              value;
            while((ptr = strsep(&parse_buffer, " \t")) != 0) {
                if (!*ptr) continue;
                this->parse = ptr;
                if (strncmp(ptr, "size=", 5) == 0) {
                    if (kstrtoull(ptr+5, 0, &value) != 0) {
                        this->state = udmabuf_manager_parse_error;
                        goto failed;
                    }
                    this->size = value;
                    continue;
                }
                if (strncmp(ptr, "bus=", 4) == 0) {
                    bus_type = udmabuf_manager_parse_bus_type(ptr+4);
                    if (IS_ERR_OR_NULL(bus_type)) {
                        this->state = udmabuf_manager_parse_error;
                        goto failed;
                    }
                    continue;
                }
                if (strncmp(ptr, "device=", 7) == 0) {
                    parent_name = ptr+7;
                    if (!*parent_name) {
                        this->state = udmabuf_manager_parse_error;
                        goto failed;
                    }
                    continue;
                }
                if (kstrtoull(ptr, 0, &value) == 0) {
                    this->size = value;
                    continue;
                }
                this->state = udmabuf_manager_parse_error;
                goto failed;
            }
            if (this->size == 0) {
                this->state = udmabuf_manager_size_error;
                goto failed;
            }
            if (parent_name) {
                if (bus_type == NULL)
                    bus_type = &platform_bus_type;
                parent = bus_find_device_by_name(bus_type, NULL, parent_name);
                if (IS_ERR_OR_NULL(parent)) {
                    pr_err(DRIVER_NAME ": parent device(%s) not found in bus(%s)\n", parent_name, bus_type->name);
                    this->state = udmabuf_manager_bind_error;
                    goto failed;
                }
                this->parent = parent;
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
    struct device*                  dev;
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
                pr_info(DRIVER_NAME ": create %s size=%zu\n", this->device_name, this->size);
                dev = u_dma_buf_device_create(this->device_name, this->minor_number, this->size, this->option, this->parent);
                if (IS_ERR_OR_NULL(dev)) {
                    result = (IS_ERR(dev)) ? PTR_ERR(dev) : -ENODEV;
                    pr_err(DRIVER_NAME ": create error: %s result = %d\n", this->device_name, result);
                    udmabuf_manager_state_clear(this);
                    goto failed;
                }
                udmabuf_manager_state_clear(this);
                break;
            case udmabuf_manager_delete_command :
                pr_info(DRIVER_NAME ": delete %s\n", this->device_name);
                dev = u_dma_buf_device_search(this->device_name, this->minor_number);
                if (IS_ERR_OR_NULL(dev)) {
                    pr_err(DRIVER_NAME ": delete error: %s not found\n", this->device_name);
                    udmabuf_manager_state_clear(this);
                    result = -EINVAL;
                    goto failed;
                }
                result = u_dma_buf_device_remove(dev);
                if (result) {
                    pr_err(DRIVER_NAME ": delete error: %s result = %d\n", this->device_name, result);
                    goto failed;
                    udmabuf_manager_state_clear(this);
                }
                udmabuf_manager_state_clear(this);
                break;
            case udmabuf_manager_parse_error :
                pr_err(DRIVER_NAME ": parse error: ""%s""\n", this->parse);
                udmabuf_manager_state_clear(this);
                result = -EINVAL;
                goto failed;
            case udmabuf_manager_size_error :
                pr_err(DRIVER_NAME ": size error: %zu\n", this->size);
                udmabuf_manager_state_clear(this);
                result = -EINVAL;
                goto failed;
            case udmabuf_manager_bind_error :
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
    .name    = DRIVER_NAME,
    .fops    = &udmabuf_manager_file_ops,
};

static bool udmabuf_manager_device_registerd = false;

/**
 * u_dma_buf_mgr_init()
 */
static int __init u_dma_buf_mgr_init(void)
{
    int retval = 0;

    retval = misc_register(&udmabuf_manager_device);
    if (retval) {
        pr_err(DRIVER_NAME ": couldn't register. return=%d\n", retval);
        udmabuf_manager_device_registerd = false;
    } else {
        udmabuf_manager_device_registerd = true;
    }
    return retval;
}

/**
 * u_dma_buf_mgr_exit()
 */
static void __exit u_dma_buf_mgr_exit(void)
{
    if (udmabuf_manager_device_registerd)
        misc_deregister(&udmabuf_manager_device);
}

module_init(u_dma_buf_mgr_init);
module_exit(u_dma_buf_mgr_exit);
