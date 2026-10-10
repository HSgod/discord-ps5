/*
 * Accord - process-lifetime heap for the OpenGL runtime.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The kit and the Mesa runtime it links allocate for the whole life of the
 * title, and some of those allocations are released by late C++ destructors
 * after the driver has torn itself down. A heap the system can unmap
 * underneath them turns that into a crash on the way out, so every
 * allocation the process makes goes into one fixed 128 MiB arena created
 * from sceLibcMspace and never given back. Modelled on ps5-opengl's
 * native-app heap, which ps5-homebrew-ui carries as src/runtime/app_heap.c.
 *
 * The harness links this file only when it is told to --wrap the malloc
 * family (APP_WRAP_SYMBOLS in the Makefile), which is what routes the
 * process's allocations through __wrap_* below. Anything allocated before
 * the arena exists, or by code that bypasses the wrappers, still lands on
 * the real heap and is handled by the __real_* fallbacks.
 */

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "app_heap.hpp"

static const size_t kArenaSize = 128u * 1024u * 1024u;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *address, size_t size);
void __real_free(void *address);
int __real_posix_memalign(void **address, size_t alignment, size_t size);
size_t __real_malloc_usable_size(const void *address);

void *sceLibcMspaceCreate(const char *name, void *base, size_t size, unsigned int flags);
void *sceLibcMspaceMalloc(void *mspace, size_t size);
void *sceLibcMspaceCalloc(void *mspace, size_t count, size_t size);
void *sceLibcMspaceRealloc(void *mspace, void *address, size_t size);
void sceLibcMspaceFree(void *mspace, void *address);
int sceLibcMspacePosixMemalign(void *mspace, void **address, size_t alignment, size_t size);
size_t sceLibcMspaceMallocUsableSize(const void *address);

/* 0 before the first allocation, 1 while the arena is being built, 2 ready,
 * -1 failed: a failed arena means every call falls back to the real heap. */
static atomic_int heap_state;
static void *heap_base;
static void *heap_mspace;
static atomic_size_t heap_live_bytes;
static atomic_size_t heap_peak_bytes;
static atomic_size_t heap_blocks;
static atomic_size_t heap_failures;

/* The first thread to arrive builds the arena; the others wait for its answer
 * rather than allocate from two arenas. */
static int heap_ready(void)
{
    int state = atomic_load_explicit(&heap_state, memory_order_acquire);
    if (state == 2)
        return 1;
    if (state != 0)
        return 0;

    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&heap_state, &expected, 1,
                                                 memory_order_acq_rel, memory_order_acquire))
        return expected == 2;

    void *base = mmap(NULL, kArenaSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED)
    {
        atomic_store_explicit(&heap_state, -1, memory_order_release);
        return 0;
    }
    heap_base = base;
    heap_mspace = sceLibcMspaceCreate("accord", base, kArenaSize, 0);
    if (heap_mspace == NULL)
    {
        heap_base = NULL;
        munmap(base, kArenaSize);
        atomic_store_explicit(&heap_state, -1, memory_order_release);
        return 0;
    }
    atomic_store_explicit(&heap_state, 2, memory_order_release);
    return 1;
}

static void note_size_change(size_t before, size_t after)
{
    size_t live = after >= before
                      ? atomic_fetch_add_explicit(&heap_live_bytes, after - before,
                                                  memory_order_relaxed) + after - before
                      : atomic_fetch_sub_explicit(&heap_live_bytes, before - after,
                                                  memory_order_relaxed) - (before - after);
    size_t peak = atomic_load_explicit(&heap_peak_bytes, memory_order_relaxed);
    while (live > peak &&
           !atomic_compare_exchange_weak_explicit(&heap_peak_bytes, &peak, live,
                                                  memory_order_relaxed, memory_order_relaxed))
    {
    }
}

static void *note_allocation(void *address, int counted)
{
    if (address != NULL)
    {
        note_size_change(0, sceLibcMspaceMallocUsableSize(address));
        atomic_fetch_add_explicit(&heap_blocks, 1, memory_order_relaxed);
    }
    else if (counted)
    {
        atomic_fetch_add_explicit(&heap_failures, 1, memory_order_relaxed);
    }
    return address;
}

static int heap_owns(const void *address)
{
    /* The state is the publication point for heap_base, so read it first. */
    if (atomic_load_explicit(&heap_state, memory_order_acquire) != 2)
        return 0;
    const uintptr_t value = (uintptr_t)address;
    const uintptr_t base = (uintptr_t)heap_base;
    return value >= base && value - base < kArenaSize;
}

void *__wrap_malloc(size_t size)
{
    return heap_ready() ? note_allocation(sceLibcMspaceMalloc(heap_mspace, size), size != 0)
                        : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    return heap_ready()
               ? note_allocation(sceLibcMspaceCalloc(heap_mspace, count, size),
                                 count != 0 && size != 0)
               : __real_calloc(count, size);
}

void *__wrap_realloc(void *address, size_t size)
{
    if (address == NULL)
        return __wrap_malloc(size);
    if (!heap_owns(address))
        return __real_realloc(address, size);

    const size_t before = sceLibcMspaceMallocUsableSize(address);
    void *result = sceLibcMspaceRealloc(heap_mspace, address, size);
    if (result != NULL)
        note_size_change(before, sceLibcMspaceMallocUsableSize(result));
    else if (size != 0)
        atomic_fetch_add_explicit(&heap_failures, 1, memory_order_relaxed);
    return result;
}

void __wrap_free(void *address)
{
    if (heap_owns(address))
    {
        note_size_change(sceLibcMspaceMallocUsableSize(address), 0);
        atomic_fetch_sub_explicit(&heap_blocks, 1, memory_order_relaxed);
        sceLibcMspaceFree(heap_mspace, address);
    }
    else
    {
        __real_free(address);
    }
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size)
{
    if (!heap_ready())
        return __real_posix_memalign(address, alignment, size);

    const int result = sceLibcMspacePosixMemalign(heap_mspace, address, alignment, size);
    if (result == 0)
        note_allocation(*address, size != 0);
    else if (result == ENOMEM)
        atomic_fetch_add_explicit(&heap_failures, 1, memory_order_relaxed);
    return result;
}

size_t __wrap_malloc_usable_size(const void *address)
{
    return heap_owns(address) ? sceLibcMspaceMallocUsableSize(address)
                              : __real_malloc_usable_size(address);
}

/* Counters for the title's log: what the arena holds, and whether it ever
 * refused an allocation. */
void accord_heap_stats(size_t *live_bytes, size_t *peak_bytes, size_t *blocks, size_t *failures)
{
    if (live_bytes != NULL)
        *live_bytes = atomic_load_explicit(&heap_live_bytes, memory_order_relaxed);
    if (peak_bytes != NULL)
        *peak_bytes = atomic_load_explicit(&heap_peak_bytes, memory_order_relaxed);
    if (blocks != NULL)
        *blocks = atomic_load_explicit(&heap_blocks, memory_order_relaxed);
    if (failures != NULL)
        *failures = atomic_load_explicit(&heap_failures, memory_order_relaxed);
}
