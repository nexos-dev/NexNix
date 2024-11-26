/*
    list.h - contains list implementation
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

#ifndef _NK_LIST_H
#define _NK_LIST_H

#include <assert.h>
#include <nexke/types.h>
#include <stddef.h>
#include <stdint.h>

// List link definition
typedef struct _link
{
    struct _link* prev;
    struct _link* next;
} NkLink_t, NkList_t;

// Gets the containter for a link
#define LINK_CONTAINER(addr, type, field) \
    ((type*) ((uintptr_t) (addr) - (uintptr_t) (&((type*) 0)->field)))

// Initializes a list
static FORCEINLINE void NkListInit (NkList_t* list)
{
    list->next = list;
    list->prev = list;
}

// Adds item to front of list
static FORCEINLINE void NkListAddFront (NkList_t* list, NkLink_t* item)
{
    NkLink_t* oldHead = list->next;
    item->next = oldHead;
    item->prev = list;
    oldHead->prev = item;
    list->next = item;
}

// Adds item to back of list
static FORCEINLINE void NkListAddBack (NkList_t* list, NkLink_t* item)
{
    NkLink_t* oldTail = list->prev;
    item->next = list;
    item->prev = oldTail;
    oldTail->next = item;
    list->prev = item;
}

// Adds item after item
static FORCEINLINE void NkListAdd (NkList_t* list, NkLink_t* item, NkLink_t* newItem)
{
    item->next->prev = newItem;
    newItem->prev = item;
    newItem->next = item->next;
    item->next = newItem;
}

// Adds item before item
static FORCEINLINE void NkListAddBefore (NkList_t* list, NkLink_t* item, NkLink_t* newItem)
{
    item->prev->next = newItem;
    newItem->next = item;
    newItem->prev = item->prev;
    item->prev = newItem;
}

// Removes item from list
static FORCEINLINE void NkListRemove (NkList_t* list, NkLink_t* item)
{
    NkLink_t* oldNext = item->next;
    NkLink_t* oldPrev = item->prev;
    oldNext->prev = oldPrev;
    oldPrev->next = oldNext;
}

// Gets first item in list
static FORCEINLINE NkLink_t* NkListFront (NkList_t* list)
{
    return (list->next == list) ? NULL : list->next;
}

// Iterates to next item in list
static FORCEINLINE NkLink_t* NkListIterate (NkList_t* list, NkLink_t* link)
{
    return (link->next == list) ? NULL : link->next;
}

#endif
