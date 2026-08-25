/* KallistiOS ##version##

   pvr_texture_reservation.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr.h>

#include <errno.h>
#include <stdint.h>

static int range_valid(const void *pointer, size_t count, size_t element_size,
                       uintptr_t *start, size_t *byte_size) {
    if(count && !pointer) {
        errno = EINVAL;
        return 0;
    }
    if(count > SIZE_MAX / element_size) {
        errno = ERANGE;
        return 0;
    }

    *start = (uintptr_t)pointer;
    *byte_size = count * element_size;
    if(*byte_size > UINTPTR_MAX - *start) {
        errno = ERANGE;
        return 0;
    }
    return 1;
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs_size && rhs_size && lhs < rhs + rhs_size &&
           rhs < lhs + lhs_size;
}

static int aligned_cursor(size_t cursor, size_t alignment, size_t *result) {
    size_t padding = (size_t)(-cursor) & (alignment - 1u);

    if(cursor > SIZE_MAX - padding) {
        errno = ERANGE;
        return -1;
    }
    *result = cursor + padding;
    return 0;
}

int pvr_txr_surface_plan_reservation(const pvr_txr_surface_t *surfaces,
                                     size_t surface_count, size_t alignment,
                                     size_t *offsets, size_t *total_bytes) {
    uintptr_t surface_start;
    uintptr_t offset_start;
    uintptr_t total_start = (uintptr_t)total_bytes;
    size_t surface_bytes;
    size_t offset_bytes;
    size_t cursor = 0;
    size_t i;

    if(!total_bytes || alignment < 8u ||
       (alignment & (alignment - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(alignment > 32u) {
        errno = ERANGE;
        return -1;
    }
    if(!range_valid(surfaces, surface_count, sizeof(*surfaces),
                    &surface_start, &surface_bytes) ||
       !range_valid(offsets, surface_count, sizeof(*offsets),
                    &offset_start, &offset_bytes))
        return -1;

    /* Output aliases could corrupt later descriptors while offsets are being
       published, so reject them before the validation pass. */
    if(ranges_overlap(surface_start, surface_bytes,
                      offset_start, offset_bytes) ||
       ranges_overlap(surface_start, surface_bytes,
                      total_start, sizeof(*total_bytes)) ||
       ranges_overlap(offset_start, offset_bytes,
                      total_start, sizeof(*total_bytes))) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < surface_count; ++i) {
        pvr_txr_level_info_t level;
        size_t start;

        if(surfaces[i].vram || surfaces[i].capacity ||
           surfaces[i].owns_vram) {
            errno = EBUSY;
            return -1;
        }
        if(pvr_txr_surface_get_level(&surfaces[i], 0, &level) < 0)
            return -1;
        if(aligned_cursor(cursor, alignment, &start) < 0)
            return -1;
        if(start > PVR_RAM_SIZE ||
           surfaces[i].byte_size > PVR_RAM_SIZE - start) {
            errno = ENOSPC;
            return -1;
        }
        cursor = start + surfaces[i].byte_size;
    }

    /* The first pass makes publication failure-atomic. Caller mutation during
       the function would be a data race and is outside this API's contract. */
    cursor = 0;
    for(i = 0; i < surface_count; ++i) {
        size_t start = 0;

        (void)aligned_cursor(cursor, alignment, &start);
        offsets[i] = start;
        cursor = start + surfaces[i].byte_size;
    }
    *total_bytes = cursor;
    return 0;
}

int pvr_txr_surface_bind_reservation(
        pvr_txr_surface_t *surface,
        const pvr_mem_reservation_t *reservation, size_t offset) {
    pvr_txr_surface_t candidate;
    pvr_txr_level_info_t level;
    pvr_ptr_t slice;

    if(!surface) {
        errno = EINVAL;
        return -1;
    }
    if(surface->vram || surface->capacity || surface->owns_vram) {
        errno = EBUSY;
        return -1;
    }
    if(pvr_txr_surface_get_level(surface, 0, &level) < 0)
        return -1;
    if(pvr_mem_reservation_get(reservation, offset, surface->byte_size,
                               8u, &slice) < 0)
        return -1;

    candidate = *surface;
    candidate.vram = slice;
    candidate.capacity = surface->byte_size;
    candidate.owns_vram = false;
    *surface = candidate;
    return 0;
}
