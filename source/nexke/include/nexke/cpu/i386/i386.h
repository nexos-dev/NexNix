/*
    i386.h - contains nexke i386 stuff
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

#ifndef _I386_H
#define _I386_H

#include <stdint.h>

typedef struct _nkarchccb
{
    uint64_t features;      // CPU feature flags
} NkArchCcb_t;

// Fills CCB with CPUID flags
void CpuDetectCpuid(NkArchCcb_t* ccb);

// Waits for IO completion
void CpuIoWait();

// Writes byte to I/O port
void CpuOutb (uint16_t port, uint8_t val);

// Writes word to I/O port
void CpuOutw (uint16_t port, uint16_t val);

// Writes dword to I/O port
void CpuOutl (uint16_t port, uint32_t val);

// Reads byte from I/O port
uint8_t CpuInb (uint16_t port);

// Reads word from I/O port
uint16_t CpuInw (uint16_t port);

// Reads dword from I/O port
uint32_t CpuInl (uint16_t port);

// Crashes the CPU
void __attribute__((noreturn)) CpuCrash();

// CPU page size
#define NEXKE_CPU_PAGESZ 0x1000

#endif
