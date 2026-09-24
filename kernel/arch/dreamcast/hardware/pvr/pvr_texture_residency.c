/* KallistiOS ##version##

   pvr_texture_residency.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#define PVR_TXR_RESIDENCY_MAGIC UINT32_C(0x50565243)

static int checked_range(const void *pointer, size_t count,
                         size_t element_size, uintptr_t *start,
                         size_t *byte_size) {
    if(!pointer || !count || count > SIZE_MAX / element_size) {
        errno = !pointer || !count ? EINVAL : ERANGE;
        return -1;
    }

    *start = (uintptr_t)pointer;
    *byte_size = count * element_size;
    if(*byte_size > UINTPTR_MAX - *start) {
        errno = ERANGE;
        return -1;
    }
    return 0;
}

static int ranges_overlap(uintptr_t lhs, size_t lhs_size,
                          uintptr_t rhs, size_t rhs_size) {
    return lhs < rhs + rhs_size && rhs < lhs + lhs_size;
}

static int cache_valid(const pvr_txr_residency_t *cache) {
    uintptr_t base;
    size_t i;

    if(!cache || cache->_magic != PVR_TXR_RESIDENCY_MAGIC
       || !cache->slots || !cache->surfaces || !cache->slot_count
       || !cache->slot_stride || !cache->reservation.base
       || !cache->reservation.capacity) {
        errno = EINVAL;
        return 0;
    }

    base = (uintptr_t)cache->reservation.base;
    if((base & 31u) || base < PVR_RAM_INT_BASE || base >= PVR_RAM_INT_TOP
       || cache->reservation.capacity > PVR_RAM_INT_TOP - base) {
        errno = EFAULT;
        return 0;
    }

    /* Caller-owned metadata is deliberately visible, so reject mutation that
       would otherwise turn a stale or incomplete slot into an eviction
       candidate. */
    for(i = 0; i < cache->slot_count; ++i) {
        const pvr_txr_residency_slot_t *slot = &cache->slots[i];

        if(slot->state < PVR_TXR_RESIDENCY_EMPTY
           || slot->state > PVR_TXR_RESIDENCY_READY
           || (slot->state == PVR_TXR_RESIDENCY_EMPTY
               && (slot->pin_count || slot->last_use))
           || (slot->state == PVR_TXR_RESIDENCY_LOADING
               && (slot->pin_count != 1u || !slot->generation
                   || !slot->last_use))
           || (slot->state == PVR_TXR_RESIDENCY_READY
               && (!slot->generation || !slot->last_use))) {
            errno = EINVAL;
            return 0;
        }
    }
    return 1;
}

static uint64_t next_stamp(pvr_txr_residency_t *cache) {
    size_t i;

    ++cache->clock;
    if(cache->clock)
        return cache->clock;

    /* Wrap is practically unreachable, but losing historical ordering is
       safer than allowing zero to masquerade as an unused stamp. */
    cache->clock = 1;
    for(i = 0; i < cache->slot_count; ++i) {
        if(cache->slots[i].state == PVR_TXR_RESIDENCY_READY)
            cache->slots[i].last_use = 1;
    }
    return cache->clock;
}

static int surface_valid(const pvr_txr_residency_t *cache, size_t slot) {
    uintptr_t expected;
    size_t offset;
    pvr_ptr_t address;

    if(slot >= cache->slot_count) {
        errno = ERANGE;
        return 0;
    }
    if(slot > SIZE_MAX / cache->slot_stride) {
        errno = ERANGE;
        return 0;
    }
    offset = slot * cache->slot_stride;
    if(offset >= cache->reservation.capacity) {
        errno = EINVAL;
        return 0;
    }
    expected = (uintptr_t)cache->reservation.base + offset;
    if((uintptr_t)cache->surfaces[slot].vram != expected
       || cache->surfaces[slot].owns_vram
       || cache->surfaces[slot].capacity
          != cache->surfaces[slot].byte_size) {
        errno = EINVAL;
        return 0;
    }
    return pvr_txr_surface_get_texture_address(&cache->surfaces[slot],
                                                &address) == 0;
}

static int handle_slot(pvr_txr_residency_t *cache,
                       pvr_txr_residency_handle_t handle,
                       pvr_txr_residency_slot_t **slot) {
    if(!cache_valid(cache))
        return -1;
    if(handle.slot >= cache->slot_count) {
        errno = ERANGE;
        return -1;
    }
    if(!handle.generation
       || cache->slots[handle.slot].generation != handle.generation) {
        errno = ESTALE;
        return -1;
    }
    *slot = &cache->slots[handle.slot];
    return 0;
}

int pvr_txr_residency_init(pvr_txr_residency_t *cache,
                           pvr_txr_residency_slot_t *slots,
                           pvr_txr_surface_t *surfaces, size_t slot_count,
                           const pvr_txr_surface_t *prototype) {
    pvr_txr_residency_t candidate;
    pvr_txr_level_info_t level;
    uintptr_t cache_start;
    uintptr_t slot_start;
    uintptr_t surface_start;
    uintptr_t prototype_start;
    size_t slot_bytes;
    size_t surface_bytes;
    size_t stride;
    size_t total;
    size_t i;

    if(!cache || !prototype) {
        errno = EINVAL;
        return -1;
    }
    if(checked_range(slots, slot_count, sizeof(*slots), &slot_start,
                     &slot_bytes) < 0
       || checked_range(surfaces, slot_count, sizeof(*surfaces),
                        &surface_start, &surface_bytes) < 0)
        return -1;

    cache_start = (uintptr_t)cache;
    prototype_start = (uintptr_t)prototype;
    if(ranges_overlap(cache_start, sizeof(*cache), slot_start, slot_bytes)
       || ranges_overlap(cache_start, sizeof(*cache), surface_start,
                         surface_bytes)
       || ranges_overlap(cache_start, sizeof(*cache), prototype_start,
                         sizeof(*prototype))
       || ranges_overlap(slot_start, slot_bytes, surface_start, surface_bytes)
       || ranges_overlap(prototype_start, sizeof(*prototype), slot_start,
                         slot_bytes)
       || ranges_overlap(prototype_start, sizeof(*prototype), surface_start,
                         surface_bytes)) {
        errno = EINVAL;
        return -1;
    }

    if(prototype->vram || prototype->capacity || prototype->owns_vram) {
        errno = EBUSY;
        return -1;
    }
    if(pvr_txr_surface_get_level(prototype, 0, &level) < 0)
        return -1;
    if(prototype->byte_size > SIZE_MAX - 31u) {
        errno = ERANGE;
        return -1;
    }
    stride = (prototype->byte_size + 31u) & ~(size_t)31u;
    if(slot_count - 1u > (SIZE_MAX - prototype->byte_size) / stride) {
        errno = ERANGE;
        return -1;
    }
    total = (slot_count - 1u) * stride + prototype->byte_size;
    if(total > PVR_RAM_SIZE) {
        errno = ENOSPC;
        return -1;
    }

    memset(&candidate, 0, sizeof(candidate));
    if(pvr_mem_reservation_alloc(&candidate.reservation, total) < 0)
        return -1;

    memset(slots, 0, slot_bytes);
    memset(surfaces, 0, surface_bytes);
    for(i = 0; i < slot_count; ++i) {
        surfaces[i] = *prototype;
        if(pvr_txr_surface_bind_reservation(&surfaces[i],
                                            &candidate.reservation,
                                            i * stride) < 0) {
            int saved_errno = errno;
            size_t clear;

            for(clear = 0; clear <= i; ++clear) {
                surfaces[clear].owns_vram = false;
                pvr_txr_surface_release(&surfaces[clear]);
            }
            pvr_mem_reservation_release(&candidate.reservation);
            memset(slots, 0, slot_bytes);
            memset(surfaces, 0, surface_bytes);
            errno = saved_errno;
            return -1;
        }
    }

    candidate.slots = slots;
    candidate.surfaces = surfaces;
    candidate.slot_count = slot_count;
    candidate.slot_stride = stride;
    candidate._magic = PVR_TXR_RESIDENCY_MAGIC;
    *cache = candidate;
    return 0;
}

int pvr_txr_residency_acquire(pvr_txr_residency_t *cache,
                              uint32_t identifier,
                              pvr_txr_residency_handle_t *handle,
                              pvr_txr_surface_t **surface) {
    size_t i;

    if(handle)
        memset(handle, 0, sizeof(*handle));
    if(surface)
        *surface = NULL;
    if(!handle || !surface || !cache_valid(cache)) {
        if(!handle || !surface)
            errno = EINVAL;
        return -1;
    }

    for(i = 0; i < cache->slot_count; ++i) {
        pvr_txr_residency_slot_t *slot = &cache->slots[i];

        if(slot->state == PVR_TXR_RESIDENCY_EMPTY
           || slot->identifier != identifier)
            continue;
        if(slot->state == PVR_TXR_RESIDENCY_LOADING) {
            ++cache->misses;
            errno = EAGAIN;
            return -1;
        }
        if(slot->state != PVR_TXR_RESIDENCY_READY) {
            errno = EINVAL;
            return -1;
        }
        if(slot->pin_count == UINT32_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        if(!surface_valid(cache, i))
            return -1;

        ++slot->pin_count;
        slot->last_use = next_stamp(cache);
        ++cache->hits;
        handle->slot = i;
        handle->generation = slot->generation;
        *surface = &cache->surfaces[i];
        return 0;
    }

    ++cache->misses;
    errno = ENOENT;
    return -1;
}

int pvr_txr_residency_reserve(pvr_txr_residency_t *cache,
                              uint32_t identifier,
                              pvr_txr_residency_handle_t *handle,
                              pvr_txr_surface_t **surface) {
    size_t candidate = SIZE_MAX;
    uint64_t oldest = UINT64_MAX;
    size_t i;

    if(handle)
        memset(handle, 0, sizeof(*handle));
    if(surface)
        *surface = NULL;
    if(!handle || !surface || !cache_valid(cache)) {
        if(!handle || !surface)
            errno = EINVAL;
        return -1;
    }

    for(i = 0; i < cache->slot_count; ++i) {
        pvr_txr_residency_slot_t *slot = &cache->slots[i];

        if(slot->state != PVR_TXR_RESIDENCY_EMPTY
           && slot->identifier == identifier) {
            errno = slot->state == PVR_TXR_RESIDENCY_LOADING
                  ? EALREADY : EEXIST;
            return -1;
        }
        if(slot->state == PVR_TXR_RESIDENCY_EMPTY && candidate == SIZE_MAX)
            candidate = i;
    }

    if(candidate == SIZE_MAX) {
        for(i = 0; i < cache->slot_count; ++i) {
            pvr_txr_residency_slot_t *slot = &cache->slots[i];

            if(slot->state == PVR_TXR_RESIDENCY_READY && !slot->pin_count
               && slot->last_use < oldest) {
                candidate = i;
                oldest = slot->last_use;
            }
        }
    }
    if(candidate == SIZE_MAX) {
        errno = EBUSY;
        return -1;
    }
    if(!surface_valid(cache, candidate))
        return -1;

    if(cache->slots[candidate].state == PVR_TXR_RESIDENCY_READY)
        ++cache->evictions;
    cache->slots[candidate].identifier = identifier;
    ++cache->slots[candidate].generation;
    if(!cache->slots[candidate].generation)
        ++cache->slots[candidate].generation;
    cache->slots[candidate].last_use = next_stamp(cache);
    cache->slots[candidate].pin_count = 1;
    cache->slots[candidate].state = PVR_TXR_RESIDENCY_LOADING;

    handle->slot = candidate;
    handle->generation = cache->slots[candidate].generation;
    *surface = &cache->surfaces[candidate];
    return 0;
}

int pvr_txr_residency_publish(pvr_txr_residency_t *cache,
                              pvr_txr_residency_handle_t handle) {
    pvr_txr_residency_slot_t *slot;

    if(handle_slot(cache, handle, &slot) < 0)
        return -1;
    if(slot->state != PVR_TXR_RESIDENCY_LOADING || slot->pin_count != 1u) {
        errno = EINVAL;
        return -1;
    }
    if(!surface_valid(cache, handle.slot))
        return -1;

    slot->state = PVR_TXR_RESIDENCY_READY;
    slot->last_use = next_stamp(cache);
    return 0;
}

int pvr_txr_residency_abort(pvr_txr_residency_t *cache,
                            pvr_txr_residency_handle_t handle) {
    pvr_txr_residency_slot_t *slot;

    if(handle_slot(cache, handle, &slot) < 0)
        return -1;
    if(slot->state != PVR_TXR_RESIDENCY_LOADING || slot->pin_count != 1u) {
        errno = EINVAL;
        return -1;
    }

    slot->identifier = 0;
    slot->pin_count = 0;
    slot->last_use = 0;
    slot->state = PVR_TXR_RESIDENCY_EMPTY;
    return 0;
}

int pvr_txr_residency_unpin(pvr_txr_residency_t *cache,
                            pvr_txr_residency_handle_t handle) {
    pvr_txr_residency_slot_t *slot;

    if(handle_slot(cache, handle, &slot) < 0)
        return -1;
    if(slot->state != PVR_TXR_RESIDENCY_READY || !slot->pin_count) {
        errno = EINVAL;
        return -1;
    }

    --slot->pin_count;
    return 0;
}

int pvr_txr_residency_get_status(const pvr_txr_residency_t *cache,
                                 pvr_txr_residency_status_t *status) {
    size_t i;

    if(status)
        memset(status, 0, sizeof(*status));
    if(!status || !cache_valid(cache)) {
        if(!status)
            errno = EINVAL;
        return -1;
    }

    status->slot_count = cache->slot_count;
    status->hits = cache->hits;
    status->misses = cache->misses;
    status->evictions = cache->evictions;
    for(i = 0; i < cache->slot_count; ++i) {
        const pvr_txr_residency_slot_t *slot = &cache->slots[i];

        if(slot->state == PVR_TXR_RESIDENCY_READY)
            ++status->ready_slots;
        else if(slot->state == PVR_TXR_RESIDENCY_LOADING)
            ++status->loading_slots;
        if(slot->pin_count)
            ++status->pinned_slots;
        status->pin_count += slot->pin_count;
    }
    return 0;
}

int pvr_txr_residency_destroy(pvr_txr_residency_t *cache) {
    size_t i;

    if(!cache_valid(cache))
        return -1;
    for(i = 0; i < cache->slot_count; ++i) {
        if(cache->slots[i].state == PVR_TXR_RESIDENCY_LOADING
           || cache->slots[i].pin_count) {
            errno = EBUSY;
            return -1;
        }
    }

    for(i = 0; i < cache->slot_count; ++i) {
        cache->surfaces[i].owns_vram = false;
        pvr_txr_surface_release(&cache->surfaces[i]);
    }
    memset(cache->slots, 0, cache->slot_count * sizeof(*cache->slots));
    pvr_mem_reservation_release(&cache->reservation);
    memset(cache, 0, sizeof(*cache));
    return 0;
}
