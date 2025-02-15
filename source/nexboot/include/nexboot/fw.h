/*
    fw.h - contains firmware related abstractions
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

#ifndef _FW_H
#define _FW_H

#include <nexboot/detect.h>
#include <nexboot/object.h>
#include <stdbool.h>
#include <stddef.h>

// Include CPU header
#ifdef NEXNIX_ARCH_I386
#include <nexboot/cpu/i386/cpu.h>
#elif defined NEXNIX_ARCH_X86_64
#include <nexboot/cpu/x86_64/cpu.h>
#elif defined NEXNIX_ARCH_ARMV8
#include <nexboot/cpu/armv8/cpu.h>
#elif defined NEXNIX_ARCH_RISCV64
#include <nexboot/cpu/riscv64/cpu.h>
#elif defined NEXNIX_ARCH_RISCV32
#include <nexboot/cpu/riscv32/cpu.h>
#endif

// Memory types
#define NEXBOOT_MEM_FREE         1
#define NEXBOOT_MEM_RESVD        2
#define NEXBOOT_MEM_ACPI_RECLAIM 3
#define NEXBOOT_MEM_ACPI_NVS     4
#define NEXBOOT_MEM_MMIO         5
#define NEXBOOT_MEM_FW_RECLAIM   6
#define NEXBOOT_MEM_BOOT_RECLAIM 7

#define NEXBOOT_MEM_FLAG_NON_VOLATILE (1 << 0)

// Memory map abstractions
typedef struct _mementry
{
    uint64_t base;         // Base of region
    uint64_t sz;           // Size of region
    unsigned int type;     // Memory type
    unsigned int flags;    // Memory flags
} NbMemEntry_t;

/// Initializes memory map
void NbFwMemDetect();

/// Reads memory map
NbMemEntry_t* NbGetMemMap (int* size);

/// Allocates a page for nexboot
uintptr_t NbFwAllocPage();

// Allocate multiple pages
uintptr_t NbFwAllocPages (int count);

/// Detects system hardware for bootloader
bool NbFwDetectHw (NbloadDetect_t* nbDetect);

// Get disk system booted from
NbObject_t* NbFwGetBootDisk();

// Exits FW layer
void NbFwExit();

/// Data structure of /Devices/Sysinfo object
typedef struct _cpuInfo
{
    unsigned char family;    // Family of architecture (e.g., x86)
    unsigned char arch;      // Architecture of system
    int version;             // Version of CPU (e.g., on i386+, would 386, 486, etc.)
    unsigned short flags;    // Flags of this CPU.
                             // On x86, bit 0 = FPU exists
} NbCpuInfo_t;

typedef struct _hwresults
{
    char sysType[64];          /// String to describe system
    int sysFwType;             /// System firmware types
    NbCpuInfo_t cpuInfo;       /// Cpu info
    uint32_t detectedComps;    /// Detected architecture components
    uintptr_t comps[32];       /// Component table pointers
                               /// NOTE: some have no table and only BIOS ints
    uint8_t bootDrive;         /// Bios drive number
} NbSysInfo_t;

// Firmware types
#define NB_FW_TYPE_BIOS 1
#define NB_FW_TYPE_EFI  2

// CPU families
#define NB_CPU_FAMILY_X86   1
#define NB_CPU_FAMILY_ARM   2
#define NB_CPU_FAMILY_RISCV 3

// CPU archutectures
#define NB_CPU_ARCH_I386    1
#define NB_CPU_ARCH_X86_64  2
#define NB_CPU_ARCH_ARMV8   3
#define NB_CPU_ARCH_RISCV64 4

// CPU versions
#define NB_CPU_VERSION_386   1
#define NB_CPU_VERSION_486   2
#define NB_CPU_VERSION_CPUID 3    // Use CPUID to detect

// CPU flags
#define NB_CPU_FLAG_FPU_EXISTS (1 << 0)

// Generic device structure
typedef struct _hwdevice
{
    int devSubType;    // Sub type of device
    int devId;         // Identifies device
    size_t sz;         // Size of this device structure
} NbHwDevice_t;

#ifdef NEXNIX_FW_BIOS
#include <nexboot/bios/bios.h>
#elif defined NEXNIX_FW_EFI
#include <nexboot/efi/efi.h>
#endif

// FIXME: Shouldn't be in fw.h

/// Initializes address space manager
void NbCpuAsInit();

/// Maps address into space
bool NbCpuAsMap (uintptr_t virt, paddr_t phys, uint32_t flags);

/// Unmaps address from address space
void NbCpuAsUnmap (uintptr_t virt);

/// Maps firmware-dictated regions into address sace
void NbFwMapRegions (NbMemEntry_t* memMap, size_t mapSz);

// Enables paging
void NbCpuEnablePaging();

// Allocates a page that will persist after bootloader
uintptr_t NbFwAllocPersistentPage();

// Allocates a page that will persist after bootloader
uintptr_t NbFwAllocPersistentPages (int count);

// Flags for AS
#define NB_CPU_AS_RW     (1 << 1)
#define NB_CPU_AS_GLOBAL (1 << 2)
#define NB_CPU_AS_NX     (1 << 3)
#define NB_CPU_AS_WT     (1 << 4)

// System detection macros
#define NB_ARCH_COMP_ACPI    0
#define NB_ARCH_COMP_MPS     1
#define NB_ARCH_COMP_PNP     2
#define NB_ARCH_COMP_APM     3
#define NB_ARCH_COMP_SMBIOS  4
#define NB_ARCH_COMP_SMBIOS3 5
#define NB_ARCH_COMP_PCI     6
#define NB_ARCH_COMP_VESA    7
#define NB_ARCH_COMP_BIOS32  8
#define NB_ARCH_COMP_TCG_TPM 10

#endif
