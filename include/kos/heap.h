/* KallistiOS ##version##

   kos/heap.h
   Copyright (C) 2026 Joseph Black

*/

/** \file    kos/heap.h
    \brief   Caller-backed independent heaps.
    \ingroup mm

    This interface creates an isolated allocator inside memory supplied by the
    caller. It does not reserve memory, create threads, or change the process
    allocator. Every returned allocation is aligned to MM_HEAP_ALIGNMENT.

    Operations on a live heap are serialized and may block, so they must not be
    called from interrupt context. Before destroying a heap, the caller must
    prevent new operations and wait for all existing users to finish.

    \author Joseph Black
*/

#ifndef __KOS_HEAP_H
#define __KOS_HEAP_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

/** \addtogroup mm
    @{
*/

/** \brief Alignment guaranteed by an independent heap. */
#define MM_HEAP_ALIGNMENT 32u

/** \brief Opaque independent-heap handle. */
typedef struct mm_heap mm_heap_t;

/** \brief Snapshot of an independent heap's current state. */
typedef struct mm_heap_stats {
    size_t backing_bytes;       /**< Bytes retained from the supplied region. */
    size_t capacity_bytes;      /**< Maximum payload after full coalescing. */
    size_t allocated_bytes;     /**< Bytes requested by live allocations. */
    size_t reserved_bytes;      /**< Aligned payload bytes held by allocations. */
    size_t free_bytes;          /**< Payload bytes currently available. */
    size_t largest_free_block;  /**< Largest single allocation currently possible. */
    size_t high_watermark;      /**< Greatest observed allocated_bytes value. */
    size_t live_allocations;    /**< Number of live allocations. */
    size_t free_blocks;         /**< Number of physically separate free blocks. */
} mm_heap_stats_t;

/** \brief Create an independent heap in caller-owned memory.

    The beginning and end of the usable region are aligned inward. Allocator
    metadata is stored within the region. The caller must keep the backing
    memory alive and must not modify it until mm_heap_destroy() succeeds.

    \param buffer       Backing memory.
    \param bytes        Size of backing memory.

    \return             New heap handle on success, or NULL on failure.

    \par Error Conditions:
    \em EINVAL - buffer is NULL, bytes is zero, or address arithmetic overflows.\n
    \em ENOSPC - the aligned region cannot hold heap metadata and one payload.
*/
mm_heap_t *mm_heap_create(void *buffer, size_t bytes);

/** \brief Destroy an empty independent heap.

    Destruction only invalidates allocator metadata; the backing memory remains
    owned by the caller. A heap with live allocations is left intact. This is a
    lifecycle operation: it must not run concurrently with any other operation
    on the same heap.

    \retval 0           On success.
    \retval -1          On failure with errno set.

    \par Error Conditions:
    \em EINVAL - heap metadata is invalid.\n
    \em EBUSY - allocations remain live.
*/
int mm_heap_destroy(mm_heap_t *heap);

/** \brief Allocate from an independent heap.

    \param heap         Heap handle.
    \param bytes        Requested byte count; must be nonzero.
    \return             Aligned storage on success, or NULL on failure.
*/
void *mm_heap_alloc(mm_heap_t *heap, size_t bytes);

/** \brief Allocate zero-filled storage from an independent heap.

    \param heap         Heap handle.
    \param count        Element count.
    \param bytes        Bytes per element.
    \return             Aligned storage on success, or NULL on failure.
*/
void *mm_heap_calloc(mm_heap_t *heap, size_t count, size_t bytes);

/** \brief Resize an independent-heap allocation.

    A NULL pointer behaves like mm_heap_alloc(). A zero size frees the original
    allocation and returns NULL. Existing bytes are preserved up to the smaller
    of the old and new requested sizes.

    \param heap         Heap handle.
    \param ptr          Existing allocation or NULL.
    \param bytes        New requested size.
    \return             Resized storage on success, or NULL on failure.
*/
void *mm_heap_realloc(mm_heap_t *heap, void *ptr, size_t bytes);

/** \brief Free an independent-heap allocation.

    Passing NULL is a no-op. Pointers must belong to the supplied heap and must
    identify the beginning of a live allocation.

    \retval 0           On success.
    \retval -1          On failure with errno set.
*/
int mm_heap_free(mm_heap_t *heap, void *ptr);

/** \brief Retrieve a coherent heap-statistics snapshot.

    \retval 0           On success.
    \retval -1          If an argument or heap structure is invalid.
*/
int mm_heap_get_stats(mm_heap_t *heap, mm_heap_stats_t *stats);

/** \brief Validate all independent-heap metadata and physical links.

    This operation is intended for diagnostics and destructive-operation
    preflight. It scans the entire heap while excluding concurrent mutations.

    \retval 0           If the heap is structurally valid.
    \retval -1          On corruption or an invalid handle, with errno set.
*/
int mm_heap_validate(mm_heap_t *heap);

/** @} */
__END_DECLS
#endif /* __KOS_HEAP_H */
