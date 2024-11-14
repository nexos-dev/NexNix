/*
    kvmm.c - contains kernel virtual memory manager
    Copyright 2024 The NexNix Project

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

#include "backends.h"
#include <assert.h>
#include <nexke/mm.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <string.h>

#define MM_KV_MAX_FREELIST 12
#define MM_KV_REFILL_VAL   8
#define MM_KV_REFILL_MIN   4
#define MM_KV_MAX_BUCKETS  5

// Kernel virtual region structure
typedef struct _kvregion
{
    uintptr_t vaddr;    // Virtual address of page
    size_t numPages;    // Number of pages in region
    bool isFree;        // Is this region free?
    spinlock_t lock;
    NkLink_t link;
} MmKvRegion_t;

// Kernel virtual region footer
typedef struct _kvfooter
{
    size_t magic;       // 0xDEADBEEF
    size_t regionSz;    // Size of this region
} MmKvFooter_t;

// Kernel memory bucket
typedef struct _kvbucket
{
    NkList_t regionList;    // List of regions in bucket
    size_t bucketNum;       // The bucket number of this bucket
    spinlock_t lock;        // Lock on this bucket
} MmKvBucket_t;

// Kernel free arena
typedef struct _kvarena
{
    MmKvBucket_t buckets[MM_KV_MAX_BUCKETS];    // Memory buckets
    size_t numPages;                            // Number of pages in arena
    size_t numFreePages;
    bool needsMap;    // Whether this arena is pre-mapped

    NkList_t freeList;      // Free list of pages
    size_t freeListSz;      // Number of pages in free list currently
    spinlock_t listLock;    // Lock on free list

    uintptr_t resvdStart;    // Start of reserved area
    size_t resvdSz;          // Size of reserved area in pages

    uintptr_t start;    // Bounds of arena
    uintptr_t end;
    spinlock_t lock;          // Lock on variables in here
    struct _kvarena* next;    // Next arena
} MmKvArena_t;

#define MM_KV_FOOTER_MAGIC 0xDEADBEEF

typedef struct _kvregion MmKvPage_t;

// Bucket sizes
#define MM_BUCKET_1TO4   0
#define MM_BUCKET_5TO8   1
#define MM_BUCKET_9TO16  2
#define MM_BUCKET_17TO32 3
#define MM_BUCKET_32PLUS 4

// Arena
static MmKvArena_t* mmArenas = NULL;

static MmSpace_t kmemSpace = {0};

// Boot pool globals
static uintptr_t bootPoolBase = 0;
static uintptr_t bootPoolMark = 0;
static uintptr_t bootPoolEnd = 0;
static size_t bootPoolSz = NEXBOOT_MEMPOOL_SZ;
static bool mmInit = false;    // Wheter normal MM is up yet

// Adds arena to list
static void mmKvAddArena (MmKvArena_t* arena)
{
    if (!mmArenas)
    {
        mmArenas = arena;
        arena->next = NULL;
    }
    else
    {
        arena->next = mmArenas;
        mmArenas = arena;
    }
}

// Gets arena from pointer
static MmKvArena_t* mmKvGetArena (void* ptr)
{
    MmKvArena_t* curArena = mmArenas;
    while (curArena)
    {
        if (ptr >= (void*) curArena->start && ptr <= (void*) curArena->end)
            return curArena;
        curArena = curArena->next;
    }
    assert (false);
}

// Gets bucket number from size
static int mmKvGetBucket (size_t sz)
{
    assert (sz);
    if (sz <= 4)
        return MM_BUCKET_1TO4;
    else if (sz <= 8)
        return MM_BUCKET_5TO8;
    else if (sz <= 16)
        return MM_BUCKET_9TO16;
    else if (sz <= 32)
        return MM_BUCKET_17TO32;
    else
        return MM_BUCKET_32PLUS;
}

// Gets region structure from address
static MmKvRegion_t* mmKvGetRegion (MmKvArena_t* arena, uintptr_t addr)
{
    // Get offset in page count
    pfn_t pageOffset =
        (addr - (arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ))) / NEXKE_CPU_PAGESZ;
    // Map into reserved area
    return (MmKvRegion_t*) arena->resvdStart + pageOffset;
}

// Gets region footer
static MmKvFooter_t* mmKvGetRegionFooter (MmKvArena_t* arena, uintptr_t base, size_t sz)
{
    // Get offset in page count
    pfn_t pageOffset =
        ((base - (arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ))) / NEXKE_CPU_PAGESZ) +
        (sz - 1);
    // Map into reserved area
    return (MmKvFooter_t*) (arena->resvdStart + (pageOffset * sizeof (MmKvRegion_t)));
}

// Initializes boot pool
void MmInitKvm1()
{
    // Get boot info
    NexNixBoot_t* bootInfo = NkGetBootArgs();
    // Initialize boot pool
    bootPoolBase = (uintptr_t) bootInfo->memPool;
    bootPoolMark = bootPoolBase;
    bootPoolEnd = bootPoolBase + bootInfo->memPoolSize;
    // Figure out how many pages we need to reserve
    MmKvArena_t* arena = (MmKvArena_t*) bootPoolBase;
    memset (arena, 0, sizeof (MmKvArena_t));
    arena->needsMap = false;
    arena->resvdStart = bootPoolBase + sizeof (MmKvArena_t);
    arena->resvdSz =
        CpuPageAlignUp ((((bootInfo->memPoolSize >> NEXKE_CPU_PAGE_SHIFT) * sizeof (MmKvRegion_t)) +
                         sizeof (MmKvArena_t))) >>
        NEXKE_CPU_PAGE_SHIFT;
    arena->start = bootPoolBase;
    arena->end = bootPoolEnd;
    arena->numPages = (bootInfo->memPoolSize / NEXKE_CPU_PAGESZ) - arena->resvdSz;
    arena->numFreePages = arena->numPages;
    // Initialize buckets
    for (int i = 0; i < MM_KV_MAX_BUCKETS; ++i)
        arena->buckets[i].bucketNum = i;
    // Create a region for the entire arena
    MmKvRegion_t* firstRegion =
        mmKvGetRegion (arena, arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ));
    memset (firstRegion, 0, sizeof (MmKvRegion_t));
    firstRegion->numPages = arena->numPages;
    firstRegion->vaddr = arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ);
    // Create footer
    MmKvFooter_t* footer = mmKvGetRegionFooter (arena,
                                                arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ),
                                                firstRegion->numPages);
    footer->magic = MM_KV_FOOTER_MAGIC;
    footer->regionSz = firstRegion->numPages;
    // Add to bucket
    NkListAddFront (&arena->buckets[MM_BUCKET_32PLUS].regionList, &firstRegion->link);
    // Add to arena list
    mmKvAddArena (arena);
}

// Second phase KVM init
void MmInitKvm2()
{
    // Allocate object
    size_t numPages = ((NEXKE_KERNEL_ADDR_END + 1) - NEXKE_KERNEL_ADDR_START) / NEXKE_CPU_PAGESZ;
    MmObject_t* object =
        MmCreateObject (numPages, MM_BACKEND_KERNEL, MUL_PAGE_R | MUL_PAGE_KE | MUL_PAGE_RW);
    NkLogDebug ("nexke: kernel page object has size %u KiB\n",
                (numPages * NEXKE_CPU_PAGESZ) / 1024);
    assert (object);
    // Create kernel space entry
    MmCreateKernelSpace (object);
    // Create new arena
    MmKvArena_t* arena = (MmKvArena_t*) kmemSpace.startAddr;
    memset (arena, 0, sizeof (MmKvArena_t));
    arena->needsMap = true;
    arena->resvdStart = kmemSpace.startAddr + sizeof (MmKvArena_t);
    arena->resvdSz =
        CpuPageAlignUp (((((kmemSpace.endAddr - kmemSpace.startAddr) >> NEXKE_CPU_PAGE_SHIFT) *
                          sizeof (MmKvRegion_t)) +
                         sizeof (MmKvArena_t))) >>
        NEXKE_CPU_PAGE_SHIFT;
    arena->start = kmemSpace.startAddr;
    arena->end = kmemSpace.endAddr;
    arena->numPages =
        ((kmemSpace.endAddr - kmemSpace.startAddr) >> NEXKE_CPU_PAGE_SHIFT) - arena->resvdSz;
    arena->numFreePages = arena->numPages;
    // Initialize buckets
    for (int i = 0; i < MM_KV_MAX_BUCKETS; ++i)
        arena->buckets[i].bucketNum = i;
    // Create a region for the entire arena
    MmKvRegion_t* firstRegion =
        mmKvGetRegion (arena, kmemSpace.startAddr + (arena->resvdSz * NEXKE_CPU_PAGESZ));
    memset (firstRegion, 0, sizeof (MmKvRegion_t));
    firstRegion->numPages = arena->numPages;
    firstRegion->vaddr = arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ);
    // Create footer
    MmKvFooter_t* footer =
        mmKvGetRegionFooter (arena,
                             kmemSpace.startAddr + (arena->resvdSz * NEXKE_CPU_PAGESZ),
                             firstRegion->numPages);
    footer->magic = MM_KV_FOOTER_MAGIC;
    footer->regionSz = firstRegion->numPages;
    // Add to bucket
    NkListAddFront (&arena->buckets[MM_BUCKET_32PLUS].regionList, &firstRegion->link);
    mmKvAddArena (arena);
}

// Prepares a found region for use
static inline void mmKvPrepareRegion (MmKvArena_t* arena,
                                      MmKvBucket_t* bucket,
                                      MmKvRegion_t* region,
                                      size_t numPages)
{
    // Decrease free size
    NkSpinLock (&arena->lock);
    arena->numFreePages -= numPages;
    NkSpinUnlock (&arena->lock);
    // Check if this is a perfect fit
    if (region->numPages == numPages)
    {
        // Remove from bucket and return
        NkListRemove (&bucket->regionList, &region->link);
        region->isFree = false;
        NkSpinUnlock (&region->lock);
        NkSpinUnlock (&bucket->lock);
    }
    else
    {
        // Get size of block we are splitting off
        size_t splitSz = region->numPages - numPages;
        region->numPages = numPages;
        region->isFree = false;
        // Update footer
        if (numPages > 1)
        {
            MmKvFooter_t* footerLeft = mmKvGetRegionFooter (arena, region->vaddr, numPages);
            footerLeft->magic = MM_KV_FOOTER_MAGIC;
            footerLeft->regionSz = numPages;
        }
        NkSpinUnlock (&region->lock);
        // Now get the region structure
        uintptr_t splitRegionBase = region->vaddr + (region->numPages * NEXKE_CPU_PAGESZ);
        MmKvRegion_t* splitRegion = mmKvGetRegion (arena, splitRegionBase);
        NkSpinLock (&splitRegion->lock);
        splitRegion->isFree = true;
        splitRegion->numPages = splitSz;
        splitRegion->vaddr = splitRegionBase;
        // Figure out which bucket it goes in
        int bucketIdx = mmKvGetBucket (splitRegion->numPages);
        MmKvBucket_t* destBucket = &arena->buckets[bucketIdx];
        // Update footers if they are needed
        if (splitSz > 1)
        {
            MmKvFooter_t* footerRight = mmKvGetRegionFooter (arena, splitRegionBase, splitSz);
            footerRight->magic = MM_KV_FOOTER_MAGIC;
            footerRight->regionSz = splitSz;
        }
        NkSpinUnlock (&splitRegion->lock);
        // Remove found region from bucket
        NkListRemove (&bucket->regionList, &region->link);
        NkSpinUnlock (&bucket->lock);
        // Add other region
        NkSpinLock (&destBucket->lock);
        NkListAddFront (&destBucket->regionList, &splitRegion->link);
        NkSpinUnlock (&destBucket->lock);
    }
}

// Allocates memory in arena
static MmKvRegion_t* mmAllocKvInArena (MmKvArena_t* arena, size_t numPages)
{
    // Figure out which bucket we should look in
    int bucketIdx = mmKvGetBucket (numPages);
    MmKvBucket_t* bucket = &arena->buckets[bucketIdx];
    MmKvRegion_t* foundRegion = NULL;
    NkLink_t* iter = NkListFront (&bucket->regionList);
    // Find a region
    while (!foundRegion)
    {
        NkSpinLock (&bucket->lock);
        while (iter)
        {
            MmKvRegion_t* curRegion = LINK_CONTAINER (iter, MmKvRegion_t, link);
            NkSpinLock (&curRegion->lock);
            if (curRegion->numPages >= numPages)
            {
                foundRegion = curRegion;
                break;
            }
            NkSpinUnlock (&curRegion->lock);
            iter = NkListIterate (iter);
        }
        // Did we find a region
        if (foundRegion)
            break;
        NkSpinUnlock (&bucket->lock);
        // Move to another bucket
        // Check if we have any more buckets to check
        if (bucketIdx == MM_BUCKET_32PLUS)
            return NULL;
        bucket = &arena->buckets[++bucketIdx];
        iter = NkListFront (&bucket->regionList);
    }
    assert (foundRegion);
    // We have now found a memory region
    // Prepare it
    mmKvPrepareRegion (arena, bucket, foundRegion, numPages);
    return foundRegion;
}

// Joins regions for a free
static inline MmKvRegion_t* mmKvJoinRegions (MmKvArena_t* arena, MmKvRegion_t* region)
{
    // Check if we have a block to the left
    MmKvFooter_t* leftFooter = (MmKvFooter_t*) (region - 1);
    if (region != (void*) arena->resvdStart && leftFooter->magic == MM_KV_FOOTER_MAGIC)
    {
        // Get region
        MmKvRegion_t* leftRegion =
            mmKvGetRegion (arena, region->vaddr - (leftFooter->regionSz * NEXKE_CPU_PAGESZ));
        NkSpinLock (&leftRegion->lock);
        if (leftRegion->isFree)
        {
            leftRegion->numPages += region->numPages;
            // Update footer
            MmKvFooter_t* newFooter =
                mmKvGetRegionFooter (arena, leftRegion->vaddr, leftRegion->numPages);
            newFooter->magic = MM_KV_FOOTER_MAGIC;
            newFooter->regionSz = leftRegion->numPages;
            // This region absorbed the other one so make it the one we work on from now on
            region = leftRegion;
        }
        NkSpinUnlock (&leftRegion->lock);
    }
    // Check if we have a free block to the right
    MmKvRegion_t* nextRegion =
        (MmKvRegion_t*) mmKvGetRegionFooter (arena, region->vaddr, region->numPages) + 1;
    NkSpinLock (&nextRegion->lock);
    // Check if its free and valid
    if (nextRegion->vaddr == region->vaddr + (region->numPages * NEXKE_CPU_PAGESZ) &&
        nextRegion->isFree)
    {
        // Remove from bucket
        MmKvBucket_t* bucket = &arena->buckets[mmKvGetBucket (region->numPages)];
        NkSpinUnlock (&nextRegion->lock);
        NkSpinLock (&bucket->lock);
        NkListRemove (&bucket->regionList, &nextRegion->link);
        NkSpinUnlock (&bucket->lock);
        region->numPages += nextRegion->numPages;
        MmKvFooter_t* newFooter = mmKvGetRegionFooter (arena, region->vaddr, region->numPages);
        newFooter->magic = MM_KV_FOOTER_MAGIC;
        newFooter->regionSz = region->numPages;
    }
    else
        NkSpinUnlock (&nextRegion->lock);
    return region;
}

// Allocates a single page region
static void* mmKvAllocFreeList (MmKvArena_t* arena)
{
    void* p = NULL;
    if (arena->freeListSz)
    {
        // Get link of first entry
        NkLink_t* link = NkListPopFront (&arena->freeList);
        MmKvRegion_t* region = LINK_CONTAINER (link, MmKvRegion_t, link);
        region->isFree = false;
        --arena->freeListSz;
        p = (void*) region->vaddr;
    }
    // Refill list
    if (arena->freeListSz <= MM_KV_REFILL_MIN)
    {
        // Refill free list
        for (int i = arena->freeListSz; i < MM_KV_REFILL_VAL; ++i)
        {
            MmKvRegion_t* newRegion = mmAllocKvInArena (arena, 1);
            if (!newRegion)
                break;    // Break on OOM
            NkListAddFront (&arena->freeList, &newRegion->link);
            ++arena->freeListSz;
        }
    }
    return p;
}

// Free a single page region
static void mmKvFreeToList (MmKvArena_t* arena, MmKvRegion_t* region)
{
    NkListAddFront (&arena->freeList, &region->link);
    ++arena->freeListSz;
    region->isFree = true;
}

// Brings memory in for region
static void mmKvGetMemory (void* p, size_t numPages)
{
    // Get base offset
    uintptr_t offset = (uintptr_t) p - kmemSpace.startAddr;
    MmObject_t* kmemObj = kmemSpace.entryList->obj;
    for (int i = 0; i < numPages; ++i)
    {
        MmPage_t* page = MmAllocPage();
        if (!page)
            NkPanicOom();
        NkSpinLock (&page->lock);
        // Fix this page in memory
        MmFixPage (page);
        MmAddPage (kmemObj, offset + (i * NEXKE_CPU_PAGESZ), page);
        MmMulMapPage (&kmemSpace,
                      (uintptr_t) p + (i * NEXKE_CPU_PAGESZ),
                      page,
                      MUL_PAGE_KE | MUL_PAGE_RW | MUL_PAGE_R);
        MmBackendPageIn (kmemObj, offset + (i * NEXKE_CPU_PAGESZ), page);
        NkSpinUnlock (&page->lock);
    }
}

// Frees memory for page
static void mmKvFreeMemory (void* p, size_t numPages)
{
    assert (kmemSpace.startAddr == kmemSpace.entryList->vaddr);
    size_t offset = (size_t) p - kmemSpace.startAddr;
    MmObject_t* kmemObj = kmemSpace.entryList->obj;
    for (int i = 0; i < numPages; ++i)
    {
        MmPage_t* page = MmLookupPage (kmemObj, offset);
        if (page)
        {
            NkSpinLock (&page->lock);
            // Free it
            MmUnfixPage (page);
            MmRemovePage (page);
            MmFreePage (page);
            NkSpinUnlock (&page->lock);
        }
        offset += NEXKE_CPU_PAGESZ;
    }
    // Unmap this region
    MmMulUnmapRange (MmGetKernelSpace(), (uintptr_t) p, numPages);
}

// Allocates a region of memory
void* MmAllocKvRegion (size_t numPages, int flags)
{
    // Find arena that has enough free pages
    MmKvArena_t* arena = mmArenas;
    while (arena)
    {
        if (arena->numFreePages >= numPages)
        {
            // Also check if compatible
            if (!(flags & MM_KV_NO_DEMAND) && !arena->needsMap)
                continue;
            // If this is a single page allocation attempt to pull from free list
            void* p = NULL;
            if (numPages == 1)
                p = mmKvAllocFreeList (arena);
            if (!p)
                p = (void*) mmAllocKvInArena (arena, numPages)->vaddr;
            if (p)
            {
                if (flags & MM_KV_NO_DEMAND && arena->needsMap)
                {
                    // Go ahead and bring in pages for this memory region
                    mmKvGetMemory (p, numPages);
                }
                return p;
            }
        }
        arena = arena->next;
    }
    return NULL;
}

// Frees a region of memory
void MmFreeKvRegion (void* mem)
{
    // Get which arena memory is in
    MmKvArena_t* arena = mmKvGetArena (mem);
    // Get region header
    MmKvRegion_t* region = mmKvGetRegion (arena, (uintptr_t) mem);
    size_t numPages = region->numPages;
    region->isFree = true;
    arena->numFreePages += region->numPages;
    if (numPages == 1 && arena->freeListSz <= MM_KV_MAX_FREELIST)
        mmKvFreeToList (arena, region);
    else
    {
        // Join joinable regions
        region = mmKvJoinRegions (arena, region);
        // Add region to appropriate bucket
        MmKvBucket_t* bucket = &arena->buckets[mmKvGetBucket (region->numPages)];
        NkSpinLock (&bucket->lock);
        NkListAddFront (&bucket->regionList, &region->link);
        NkSpinUnlock (&bucket->lock);
    }
    // Unmap and free memory
    if (arena->needsMap)
        mmKvFreeMemory (mem, numPages);
}

// Allocates a memory page for kernel
void* MmAllocKvPage()
{
    return MmAllocKvRegion (1, MM_KV_NO_DEMAND);
}

// Frees a memory page for kernel
void MmFreeKvPage (void* page)
{
    MmFreeKvRegion (page);
}

// Returns kernel address space
MmSpace_t* MmGetKernelSpace()
{
    return &kmemSpace;
}

// Returns kernel object
MmObject_t* MmGetKernelObject()
{
    return kmemSpace.entryList->obj;
}

// Maps in MMIO / FW memory
void* MmAllocKvMmio (paddr_t phys, int numPages, int perm)
{
    // Allocate number of virtual pages for this memory
    void* virt = MmAllocKvRegion (numPages, 0);
    if (!virt)
        NkPanicOom();
    uintptr_t off = (uintptr_t) virt - kmemSpace.startAddr;
    // Loop through every page and map and add it
    pfn_t curPfn = (pfn_t) (phys / NEXKE_CPU_PAGESZ);
    for (int i = 0; i < numPages; ++i)
    {
        MmPage_t* page = MmFindPagePfn (curPfn);
        assert (page);
        NkSpinLock (&page->lock);
        MmAddPage (kmemSpace.entryList->obj, off + i * (NEXKE_CPU_PAGESZ), page);
        MmMulMapPage (&kmemSpace, (uintptr_t) virt + (i * NEXKE_CPU_PAGESZ), page, perm);
        NkSpinUnlock (&page->lock);
    }
    // Get address right
    virt += (uintptr_t) phys % NEXKE_CPU_PAGESZ;
    return virt;
}

// Unmaps MMIO / FW memory
void MmFreeKvMmio (void* virt)
{
    MmFreeKvRegion ((void*) CpuPageAlignDown ((uintptr_t) virt));
}

// Kernel backend functions
bool KvmInitObj (MmObject_t* obj)
{
    obj->pageable = false;
    return true;
}

bool KvmDestroyObj (MmObject_t* obj)
{
    return true;
}

bool KvmPageIn (MmObject_t* obj, size_t offset, MmPage_t* page)
{
    // Zero this page
    MmMulZeroPage (page);
    return true;
}

bool KvmPageOut (MmObject_t* obj, size_t offset)
{
    return false;
}
