/*********************************************************************************
 *
 *       Copyright (C) 2015-2024 Ichiro Kawazome
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
