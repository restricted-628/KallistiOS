/* KallistiOS ##version##

   kernel/mm/heap.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kos/heap.h>
#include <kos/irq.h>
#include <kos/mutex.h>

#define MM_HEAP_MAGIC          UINT32_C(0x4b484541)
#define MM_HEAP_BLOCK_MAGIC    UINT32_C(0x4b48424c)
#define MM_HEAP_BLOCK_USED     UINT32_C(0x00000001)

typedef struct mm_heap_block mm_heap_block_t;

struct __attribute__((aligned(MM_HEAP_ALIGNMENT))) mm_heap_block {
    uint32_t magic;
    uint32_t flags;
    size_t capacity;
    size_t requested;
    mm_heap_block_t *previous;
    mm_heap_block_t *next;
};

struct __attribute__((aligned(MM_HEAP_ALIGNMENT))) mm_heap {
    uint32_t magic;
    uint32_t destroyed;
    mutex_t mutex;
    uint8_t *region_begin;
    uint8_t *region_end;
    mm_heap_block_t *first;
    size_t capacity;
    size_t allocated;
    size_t reserved;
    size_t high_watermark;
    size_t live_allocations;
};

_Static_assert(sizeof(mm_heap_block_t) % MM_HEAP_ALIGNMENT == 0,
               "heap block metadata must preserve payload alignment");
_Static_assert(sizeof(mm_heap_t) % MM_HEAP_ALIGNMENT == 0,
               "heap metadata must preserve block alignment");

static bool align_up_uintptr(uintptr_t value, uintptr_t *result) {
    const uintptr_t mask = MM_HEAP_ALIGNMENT - 1u;

    if(value > UINTPTR_MAX - mask)
        return false;

    *result = (value + mask) & ~mask;
    return true;
}

static size_t align_up_size(size_t value) {
    return (value + (MM_HEAP_ALIGNMENT - 1u)) &
           ~(size_t)(MM_HEAP_ALIGNMENT - 1u);
}

static bool heap_header_valid(const mm_heap_t *heap) {
    return heap && heap->magic == MM_HEAP_MAGIC && !heap->destroyed &&
           heap->region_begin && heap->region_end && heap->first &&
           heap->region_begin < heap->region_end;
}

static int heap_lock(mm_heap_t *heap) {
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    if(!heap_header_valid(heap)) {
        errno = EINVAL;
        return -1;
    }

    return mutex_lock(&heap->mutex);
}

/* Blocks are physical neighbors as well as list neighbors. Requiring both
   relationships makes damaged size or link fields fail closed before an
   allocator mutation can turn local corruption into an arbitrary write. */
static int heap_validate_locked(const mm_heap_t *heap) {
    const mm_heap_block_t *block;
    const mm_heap_block_t *previous = NULL;
    const uint8_t *expected;
    size_t capacity = 0;
    size_t allocated = 0;
    size_t reserved = 0;
    size_t live = 0;

    if(!heap_header_valid(heap)) {
        errno = EINVAL;
        return -1;
    }

    expected = heap->region_begin;

    for(block = heap->first; block; block = block->next) {
        const uint8_t *block_address = (const uint8_t *)block;
        const uint8_t *payload = block_address + sizeof(*block);

        if(block_address != expected || block->magic != MM_HEAP_BLOCK_MAGIC ||
           block->previous != previous ||
           (block->flags & ~MM_HEAP_BLOCK_USED) != 0 ||
           block->capacity % MM_HEAP_ALIGNMENT != 0 ||
           payload > heap->region_end ||
           block->capacity > (size_t)(heap->region_end - payload)) {
            errno = EFAULT;
            return -1;
        }

        if(block->flags & MM_HEAP_BLOCK_USED) {
            if(block->requested == 0 || block->requested > block->capacity) {
                errno = EFAULT;
                return -1;
            }

            allocated += block->requested;
            reserved += block->capacity;
            ++live;
        }
        else if(block->requested != 0) {
            errno = EFAULT;
            return -1;
        }

        capacity += block->capacity;
        expected = payload + block->capacity;

        if(block->next) {
            if((const uint8_t *)block->next != expected ||
               block->next->previous != block) {
                errno = EFAULT;
                return -1;
            }

            capacity += sizeof(*block);
        }

        previous = block;
    }

    if(expected != heap->region_end || capacity != heap->capacity ||
       allocated != heap->allocated || reserved != heap->reserved ||
       live != heap->live_allocations) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

static mm_heap_block_t *find_block_locked(mm_heap_t *heap, const void *ptr) {
    mm_heap_block_t *block;

    for(block = heap->first; block; block = block->next) {
        if((const uint8_t *)block + sizeof(*block) == ptr)
            return block;
    }

    return NULL;
}

static void merge_next_locked(mm_heap_block_t *block);

static void split_block_locked(mm_heap_block_t *block, size_t capacity) {
    mm_heap_block_t *remainder;
    const size_t available = block->capacity - capacity;

    if(available < sizeof(*block) + MM_HEAP_ALIGNMENT)
        return;

    remainder = (mm_heap_block_t *)((uint8_t *)block + sizeof(*block) +
                                    capacity);
    remainder->magic = MM_HEAP_BLOCK_MAGIC;
    remainder->flags = 0;
    remainder->capacity = available - sizeof(*block);
    remainder->requested = 0;
    remainder->previous = block;
    remainder->next = block->next;

    if(remainder->next)
        remainder->next->previous = remainder;

    block->next = remainder;
    block->capacity = capacity;

    /* Shrinking an allocation can place the new remainder immediately before
       an existing free block. Coalesce here so realloc cannot manufacture
       permanent fragmentation that a later free would never encounter. */
    if(remainder->next &&
       !(remainder->next->flags & MM_HEAP_BLOCK_USED))
        merge_next_locked(remainder);
}

static void merge_next_locked(mm_heap_block_t *block) {
    mm_heap_block_t *next = block->next;

    block->capacity += sizeof(*next) + next->capacity;
    block->next = next->next;

    if(block->next)
        block->next->previous = block;
}

static void *heap_alloc_locked(mm_heap_t *heap, size_t bytes) {
    mm_heap_block_t *block;
    size_t capacity;

    if(bytes == 0) {
        errno = EINVAL;
        return NULL;
    }

    if(bytes > SIZE_MAX - (MM_HEAP_ALIGNMENT - 1u)) {
        errno = ENOMEM;
        return NULL;
    }

    capacity = align_up_size(bytes);

    for(block = heap->first; block; block = block->next) {
        if(!(block->flags & MM_HEAP_BLOCK_USED) &&
           block->capacity >= capacity) {
            split_block_locked(block, capacity);
            block->flags = MM_HEAP_BLOCK_USED;
            block->requested = bytes;
            heap->allocated += bytes;
            heap->reserved += block->capacity;
            ++heap->live_allocations;

            if(heap->allocated > heap->high_watermark)
                heap->high_watermark = heap->allocated;

            return (uint8_t *)block + sizeof(*block);
        }
    }

    errno = ENOMEM;
    return NULL;
}

static void heap_free_block_locked(mm_heap_t *heap, mm_heap_block_t *block) {
    heap->allocated -= block->requested;
    heap->reserved -= block->capacity;
    --heap->live_allocations;
    block->flags = 0;
    block->requested = 0;

    if(block->next && !(block->next->flags & MM_HEAP_BLOCK_USED))
        merge_next_locked(block);

    if(block->previous &&
       !(block->previous->flags & MM_HEAP_BLOCK_USED))
        merge_next_locked(block->previous);
}

mm_heap_t *mm_heap_create(void *buffer, size_t bytes) {
    uintptr_t raw_begin;
    uintptr_t raw_end;
    uintptr_t begin;
    uintptr_t end;
    uintptr_t first_address;
    mm_heap_t *heap;
    mm_heap_block_t *first;
    size_t capacity;

    if(!buffer || bytes == 0) {
        errno = EINVAL;
        return NULL;
    }

    raw_begin = (uintptr_t)buffer;

    if(bytes > UINTPTR_MAX - raw_begin ||
       !align_up_uintptr(raw_begin, &begin)) {
        errno = EINVAL;
        return NULL;
    }

    raw_end = raw_begin + bytes;
    end = raw_end & ~(uintptr_t)(MM_HEAP_ALIGNMENT - 1u);

    if(begin >= end || sizeof(*heap) > end - begin) {
        errno = ENOSPC;
        return NULL;
    }

    first_address = begin + sizeof(*heap);

    if(sizeof(*first) + MM_HEAP_ALIGNMENT > end - first_address) {
        errno = ENOSPC;
        return NULL;
    }

    capacity = (size_t)(end - first_address - sizeof(*first));
    capacity &= ~(size_t)(MM_HEAP_ALIGNMENT - 1u);
    end = first_address + sizeof(*first) + capacity;

    heap = (mm_heap_t *)begin;
    first = (mm_heap_block_t *)first_address;
    memset(heap, 0, sizeof(*heap));
    memset(first, 0, sizeof(*first));

    heap->magic = MM_HEAP_MAGIC;
    heap->region_begin = (uint8_t *)first;
    heap->region_end = (uint8_t *)end;
    heap->first = first;
    heap->capacity = capacity;

    first->magic = MM_HEAP_BLOCK_MAGIC;
    first->capacity = capacity;

    if(mutex_init(&heap->mutex, MUTEX_TYPE_NORMAL) < 0) {
        heap->magic = 0;
        return NULL;
    }

    return heap;
}

int mm_heap_destroy(mm_heap_t *heap) {
    int rv;

    if(heap_lock(heap) < 0)
        return -1;

    rv = heap_validate_locked(heap);

    if(rv == 0 && heap->live_allocations != 0) {
        errno = EBUSY;
        rv = -1;
    }

    if(rv == 0)
        heap->destroyed = 1;

    mutex_unlock(&heap->mutex);

    if(rv == 0) {
        heap->magic = 0;
        mutex_destroy(&heap->mutex);
    }

    return rv;
}

void *mm_heap_alloc(mm_heap_t *heap, size_t bytes) {
    void *result;

    if(heap_lock(heap) < 0)
        return NULL;

    if(heap_validate_locked(heap) < 0)
        result = NULL;
    else
        result = heap_alloc_locked(heap, bytes);

    mutex_unlock(&heap->mutex);
    return result;
}

void *mm_heap_calloc(mm_heap_t *heap, size_t count, size_t bytes) {
    void *result;
    size_t total;

    if(bytes != 0 && count > SIZE_MAX / bytes) {
        errno = ENOMEM;
        return NULL;
    }

    total = count * bytes;
    result = mm_heap_alloc(heap, total);

    if(result)
        memset(result, 0, total);

    return result;
}

void *mm_heap_realloc(mm_heap_t *heap, void *ptr, size_t bytes) {
    mm_heap_block_t *block;
    void *result;
    size_t capacity;
    size_t copy_bytes;

    if(!ptr)
        return mm_heap_alloc(heap, bytes);

    if(bytes == 0) {
        mm_heap_free(heap, ptr);
        return NULL;
    }

    if(bytes > SIZE_MAX - (MM_HEAP_ALIGNMENT - 1u)) {
        errno = ENOMEM;
        return NULL;
    }

    if(heap_lock(heap) < 0)
        return NULL;

    if(heap_validate_locked(heap) < 0)
        goto fail;

    block = find_block_locked(heap, ptr);

    if(!block || !(block->flags & MM_HEAP_BLOCK_USED)) {
        errno = EINVAL;
        goto fail;
    }

    capacity = align_up_size(bytes);

    if(capacity <= block->capacity) {
        const size_t old_capacity = block->capacity;

        heap->allocated -= block->requested;
        heap->reserved -= old_capacity;
        split_block_locked(block, capacity);
        block->requested = bytes;
        heap->allocated += bytes;
        heap->reserved += block->capacity;

        if(heap->allocated > heap->high_watermark)
            heap->high_watermark = heap->allocated;

        result = ptr;
        goto out;
    }

    if(block->next && !(block->next->flags & MM_HEAP_BLOCK_USED) &&
       block->capacity + sizeof(*block) + block->next->capacity >= capacity) {
        const size_t old_capacity = block->capacity;

        merge_next_locked(block);
        split_block_locked(block, capacity);
        heap->allocated -= block->requested;
        heap->reserved -= old_capacity;
        block->requested = bytes;
        heap->allocated += bytes;
        heap->reserved += block->capacity;

        if(heap->allocated > heap->high_watermark)
            heap->high_watermark = heap->allocated;

        result = ptr;
        goto out;
    }

    copy_bytes = block->requested < bytes ? block->requested : bytes;
    result = heap_alloc_locked(heap, bytes);

    if(!result)
        goto fail;

    memcpy(result, ptr, copy_bytes);
    heap_free_block_locked(heap, block);
    goto out;

fail:
    result = NULL;
out:
    mutex_unlock(&heap->mutex);
    return result;
}

int mm_heap_free(mm_heap_t *heap, void *ptr) {
    mm_heap_block_t *block;
    int rv = -1;

    if(!ptr)
        return 0;

    if(heap_lock(heap) < 0)
        return -1;

    if(heap_validate_locked(heap) < 0)
        goto out;

    block = find_block_locked(heap, ptr);

    if(!block || !(block->flags & MM_HEAP_BLOCK_USED)) {
        errno = EINVAL;
        goto out;
    }

    heap_free_block_locked(heap, block);
    rv = 0;

out:
    mutex_unlock(&heap->mutex);
    return rv;
}

int mm_heap_get_stats(mm_heap_t *heap, mm_heap_stats_t *stats) {
    mm_heap_block_t *block;
    int rv = -1;

    if(!stats) {
        errno = EINVAL;
        return -1;
    }

    memset(stats, 0, sizeof(*stats));

    if(heap_lock(heap) < 0)
        return -1;

    if(heap_validate_locked(heap) < 0)
        goto out;

    stats->backing_bytes = (size_t)(heap->region_end - (uint8_t *)heap);
    stats->capacity_bytes = heap->capacity;
    stats->allocated_bytes = heap->allocated;
    stats->reserved_bytes = heap->reserved;
    stats->high_watermark = heap->high_watermark;
    stats->live_allocations = heap->live_allocations;

    for(block = heap->first; block; block = block->next) {
        if(!(block->flags & MM_HEAP_BLOCK_USED)) {
            stats->free_bytes += block->capacity;
            ++stats->free_blocks;

            if(block->capacity > stats->largest_free_block)
                stats->largest_free_block = block->capacity;
        }
    }

    rv = 0;
out:
    mutex_unlock(&heap->mutex);
    return rv;
}

int mm_heap_validate(mm_heap_t *heap) {
    int rv;

    if(heap_lock(heap) < 0)
        return -1;

    rv = heap_validate_locked(heap);
    mutex_unlock(&heap->mutex);
    return rv;
}
