/*
    types.h - contains types used between headers in-kernel
    Copyright 2023 - 2024 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef _NKTYPES_H
#define _NKTYPES_H

#include <errno.h>
#include <stdint.h>

typedef struct _nkcons NkConsole_t;
typedef struct _int NkInterrupt_t;
typedef struct _hwint NkHwInterrupt_t;
typedef struct _timeevt NkTimeEvent_t;
typedef struct _hwclock PltHwClock_t;
typedef struct _mmspace MmMulSpace_t;
typedef struct _page MmPage_t;
typedef struct _memspace MmSpace_t;
typedef struct _memobject MmObject_t;
typedef struct _thread NkThread_t;
typedef struct _work NkWorkItem_t;
typedef struct _nkccb NkCcb_t;

typedef int ipl_t;
typedef int id_t;
typedef uint64_t ktime_t;
typedef volatile int spinlock_t;
typedef int errno_t;

#define FORCEINLINE inline __attribute__ ((always_inline))

#endif
