/* KallistiOS ##version##

   pvr_mem_reservation.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

static int reservation_valid(const pvr_mem_reservation_t *reservation) {
    uintptr_t address;

    if(!reservation || !reservation->base || !reservation->capacity) {
        errno = EINVAL;
        return 0;
    }

    address = (uintptr_t)reservation->base;
    if(address & 31u) {
        errno = EINVAL;
        return 0;
    }
    if(address < PVR_RAM_INT_BASE || address >= PVR_RAM_INT_TOP) {
        errno = EFAULT;
        return 0;
    }
    if(reservation->capacity > PVR_RAM_INT_TOP - address) {
        errno = ENOSPC;
        return 0;
    }
    return 1;
}

int pvr_mem_reservation_alloc(pvr_mem_reservation_t *reservation,
                              size_t byte_size) {
    pvr_mem_reservation_t candidate;

    if(!reservation || !byte_size) {
        errno = EINVAL;
        return -1;
    }
    if(byte_size > PVR_RAM_SIZE) {
        errno = ERANGE;
        return -1;
    }

    candidate.base = pvr_mem_malloc(byte_size);
    if(!candidate.base) {
        errno = ENOMEM;
        return -1;
    }
    candidate.capacity = byte_size;

    *reservation = candidate;
    return 0;
}

int pvr_mem_reservation_get(const pvr_mem_reservation_t *reservation,
                            size_t offset, size_t byte_size,
                            size_t alignment, pvr_ptr_t *slice) {
    uintptr_t address;

    if(slice)
        *slice = NULL;
    if(!slice || !byte_size || alignment < 8u ||
       (alignment & (alignment - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(alignment > PVR_RAM_SIZE) {
        errno = ERANGE;
        return -1;
    }
    if(!reservation_valid(reservation))
        return -1;
    if(offset > reservation->capacity ||
       byte_size > reservation->capacity - offset) {
        errno = ENOSPC;
        return -1;
    }

    address = (uintptr_t)reservation->base + offset;
    if(address & (alignment - 1u)) {
        errno = EINVAL;
        return -1;
    }
    *slice = (pvr_ptr_t)address;
    return 0;
}

void pvr_mem_reservation_release(pvr_mem_reservation_t *reservation) {
    if(!reservation)
        return;
    if(reservation->base)
        pvr_mem_free(reservation->base);
    memset(reservation, 0, sizeof(*reservation));
}
