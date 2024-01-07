/*
    armv8.h - contains nexke armv8 stuff
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

#ifndef _ARMV8_H
#define _ARMV8_H

#include <stdint.h>

typedef struct _nkarchccb
{
    uint64_t features;      // CPU feature flags
} NkArchCcb_t;

void __attribute__((noreturn)) CpuCrash();

// CPU page size
#define NEXKE_CPU_PAGESZ 0x1000

#endif
