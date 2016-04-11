/*********************************************************************************
 *
 *       Copyright (C) 2016 Ichiro Kawazome
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
/*
 * minor_number_allocator
 */
#ifndef _MINOR_NUMBER_ALLOCATOR_H_
#define _MINOR_NUMBER_ALLOCATOR_H_

#include <asm/bitops.h>

#define DECLARE_MINOR_NUMBER_ALLOCATOR(__name, __max_num)                                \
DECLARE_BITMAP(    __name ## _minor_number_bitmap, __max_num);                           \
struct mutex       __name ## _minor_number_bitmap_mutex;                                 \
static inline void __name ## _minor_number_allocator_initilize(void)                     \
{                                                                                        \
    mutex_init(&(__name ## _minor_number_bitmap_mutex));                                 \
    memset(&(__name ## _minor_number_bitmap), 0, sizeof(__name ## _minor_number_bitmap));\
}                                                                                        \
static inline int __name ## _minor_number_allocate(int num)                              \
{   int status;                                                                          \
    mutex_lock(&(__name ## _minor_number_bitmap_mutex));                                 \
    status = (0 == test_and_set_bit(num, __name ## _minor_number_bitmap)) ? 0 : -1;      \
    mutex_unlock(&(__name ## _minor_number_bitmap_mutex));                               \
    return status;                                                                       \
}                                                                                        \
static inline int __name ## _minor_number_new(void)                                      \
{                                                                                        \
    int num;                                                                             \
    mutex_lock(&(__name ## _minor_number_bitmap_mutex));                                 \
    num = find_first_zero_bit(__name ## _minor_number_bitmap, __max_num);                \
    if ((0 <= num) && (num < __max_num)) {                                               \
        set_bit(num, __name ## _minor_number_bitmap);                                    \
    } else {                                                                             \
        num = -1;                                                                        \
    }                                                                                    \
    mutex_unlock(&(__name ## _minor_number_bitmap_mutex));                               \
    return num;                                                                          \
}                                                                                        \
static inline void __name ## _minor_number_free(int num)                                 \
{                                                                                        \
    mutex_lock(&(__name ## _minor_number_bitmap_mutex));                                 \
    clear_bit(num, __name ## _minor_number_bitmap);                                      \
    mutex_unlock(&(__name ## _minor_number_bitmap_mutex));                               \
}                                                                                        \

#endif
