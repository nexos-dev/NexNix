/*
    nexnix.c - contains NexNix boot type booting function
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

#include <assert.h>
#include <nexboot/drivers/display.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/nexnix.h>
#include <nexboot/os.h>
#include <nexboot/shell.h>
#include <string.h>

// Reads in a file component
void* osReadFile (NbObject_t* fs, bool persists, const char* name)
{
    NbShellWrite ("Loading %s...\n", name);
    // Read in file
    NbFile_t* file = NbShellOpenFile (fs, name);
    if (!file)
    {
        NbShellWrite ("nexboot: unable to open file \"%s\"\n", name);
        return NULL;
    }
    // Allocate memory for file
    int numPages = (file->size + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
    void* fileBase = NULL;
    if (persists)
        fileBase = (void*) NbFwAllocPersistentPages (numPages);
    else
        fileBase = (void*) NbFwAllocPages (numPages);
    if (!fileBase)
    {
        NbShellWrite ("nexboot: out of memory");
        NbCrash();
    }
    // Read in file
    for (int i = 0; i < numPages; ++i)
    {
        if (!NbVfsReadFile (fs,
                            file,
                            fileBase + (i * NEXBOOT_CPU_PAGE_SIZE),
                            NEXBOOT_CPU_PAGE_SIZE))
        {
            NbVfsCloseFile (fs, file);
            NbShellWrite ("nexboot: unable to read file \"%s\"", name);
            return NULL;
        }
    }
    NbVfsCloseFile (fs, file);
    return fileBase;
}

bool NbOsBootNexNix (NbOsInfo_t* info)
{
    // Sanitize input
    if (!info->payload)
    {
        NbShellWrite ("nexboot: error: payload not specified\n");
        return false;
    }
    NbObject_t* fs = NbShellGetRootFs();
    if (!fs)
    {
        NbShellWrite ("nexboot: error: no root filesystem\n");
        return false;
    }
    char rootFsBuf[128];
    NbLogMessage ("nexboot: Booting from %s%s using method NexNix\n",
                  NEXBOOT_LOGLEVEL_DEBUG,
                  NbObjGetPath (fs, rootFsBuf, 128),
                  StrRefGet (info->payload));
    // Read in kernel file
    void* keFileBase = osReadFile (fs, false, StrRefGet (info->payload));
    if (!keFileBase)
        return false;
    // Initialize boot info struct
    NexNixBoot_t* bootInfo = malloc (sizeof (NexNixBoot_t));
    memset (bootInfo, 0, sizeof (NexNixBoot_t));
    // Initialize sysinfo
    NbSysInfo_t* sysInf = NbObjGetData (NbObjFind ("/Devices/Sysinfo"));
    bootInfo->fw = sysInf->sysFwType;
    bootInfo->detectedComps = sysInf->detectedComps;
    memcpy (bootInfo->comps, sysInf->comps, 32 * sizeof (uintptr_t));
    strcpy (bootInfo->sysName, sysInf->sysType);
    // Set log base
    bootInfo->logBase = NbLogGetBase();
    // Allocate early memory pool
    void* memPool = (void*) NbFwAllocPersistentPages (
        (NEXBOOT_MEMPOOL_SZ + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE);
    if (!memPool)
    {
        NbShellWrite ("nexboot: out of memory");
        NbCrash();
    }
    // Map it
    int numPages = (NEXBOOT_MEMPOOL_SZ + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
    for (int i = 0; i < numPages; ++i)
    {
        NbCpuAsMap (NEXBOOT_MEMPOOL_BASE + (i * NEXBOOT_CPU_PAGE_SIZE),
                    (paddr_t) memPool + (i * NEXBOOT_CPU_PAGE_SIZE),
                    NB_CPU_AS_RW | NB_CPU_AS_NX);
    }
    bootInfo->memPool = (void*) NEXBOOT_MEMPOOL_BASE;
    bootInfo->memPoolSize = NEXBOOT_MEMPOOL_SZ;
    // Load modules
    if (info->mods)
    {
        ArrayIter_t iterSt = {0};
        ArrayIter_t* iter = ArrayIterate (info->mods, &iterSt);
        while (iter)
        {
            // Grab the module path
            StringRef_t** ref = iter->ptr;
            const char* mod = StrRefGet (*ref);
            // Read the file
            bootInfo->mods[bootInfo->numMods] = osReadFile (fs, true, mod);
            if (!bootInfo->mods[bootInfo->numMods])
            {
                // Error occured
                return false;
            }
            iter = ArrayIterate (info->mods, iter);
        }
    }
    // Copy arguments
    strcpy (bootInfo->args, StrRefGet (info->args));
    // We have now reached that point in loading.
    // It's time to launch the kernel. First, however, we must map the kernel
    // into the address space.
    // Load up the kernel into memory
    uintptr_t entry = NbElfLoadFile (keFileBase);
    // Allocate a boot stack
    int stackPages = NEXBOOT_STACK_SIZE / NEXBOOT_CPU_PAGE_SIZE;
    uintptr_t stack = NbFwAllocPersistentPages (stackPages);
    memset ((void*) stack, 0, NEXBOOT_STACK_SIZE);
    uintptr_t stackVirt = NB_KE_STACK_BASE - NEXBOOT_STACK_SIZE;
    for (int i = 0; i < stackPages; ++i)
    {
        NbCpuAsMap (stackVirt + (i * NEXBOOT_CPU_PAGE_SIZE),
                    stack + (i * NEXBOOT_CPU_PAGE_SIZE),
                    NB_CPU_AS_RW | NB_CPU_AS_NX);
    }
    // Get memory map
    bootInfo->memMap = NbGetMemMap (&bootInfo->mapSize);
    // Allocate a buffer for the memory mao
    // We have to do this now as we can't get the memory map to be passed until we
    // are finished allocating memory. However we need to allocate memory for the map in a tricky
    // spot (at least on EFI) Here we go by the current map size but add on an additional page to
    // account for additional map entries that may be created
    void* memMapBuf = (void*) NbFwAllocPages (
        ((bootInfo->mapSize * sizeof (NbMemEntry_t) + (NEXBOOT_CPU_PAGE_SIZE - 1)) /
         NEXBOOT_CPU_PAGE_SIZE) +
        1);
    // Map in firmware-dictated memory regions
    NbFwMapRegions (bootInfo->memMap, bootInfo->mapSize);
    // Re-make memory map in case above function changed it
    void* memMap = NbGetMemMap (&bootInfo->mapSize);
    memcpy (memMapBuf, memMap, (bootInfo->mapSize * sizeof (NbMemEntry_t)));
    bootInfo->memMap = memMapBuf;
    // Find primary display
    NbObject_t* displayIter = NULL;
    bool foundDisplay = false;
    NbObject_t* devDir = NbObjFind ("/Devices");
    while ((displayIter = NbObjEnumDir (devDir, displayIter)))
    {
        if (displayIter->type == OBJ_TYPE_DEVICE && displayIter->interface == OBJ_INTERFACE_DISPLAY)
        {
            foundDisplay = true;
            break;
        }
    }
    if (foundDisplay)
    {
        char buf[128] = {0};
        NbLogMessage ("nexboot: passing display %s to kernel\n",
                      NEXBOOT_LOGLEVEL_DEBUG,
                      NbObjGetPath (displayIter, buf, 128));
        NbDisplayDev_t* display = NbObjGetData (displayIter);
        // Copy fields
        bootInfo->displayDefault = false;
        bootInfo->display.width = display->width;
        bootInfo->display.height = display->height;
        bootInfo->display.bytesPerLine = display->bytesPerLine;
        bootInfo->display.bytesPerPx = display->bytesPerPx;
        bootInfo->display.bpp = display->bpp;
        bootInfo->display.lfbSize = display->lfbSize;
        bootInfo->display.frameBuffer = display->frontBuffer;
        bootInfo->display.backBuffer = display->backBuffer;
        memcpy (&bootInfo->display.redMask, &display->redMask, sizeof (NbPixelMask_t));
        memcpy (&bootInfo->display.greenMask, &display->greenMask, sizeof (NbPixelMask_t));
        memcpy (&bootInfo->display.blueMask, &display->blueMask, sizeof (NbPixelMask_t));
        memcpy (&bootInfo->display.resvdMask, &display->resvdMask, sizeof (NbPixelMask_t));
    }
    else
        bootInfo->displayDefault = true;
    NbLogMessage ("nexboot: Starting kernel\n", NEXBOOT_LOGLEVEL_DEBUG);
    // Exit from clutches of FW
    NbFwExit();
    // Enable paging
    NbCpuEnablePaging();
    // Launch the kernel
    NbCpuLaunchKernel (entry, (uintptr_t) bootInfo);
    return false;
}
