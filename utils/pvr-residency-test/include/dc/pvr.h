/* KallistiOS ##version##

   Host-test PVR declarations for fixed-slot texture residency.
   Copyright (C) 2026 Joseph Black
*/

#ifndef TEST_RESIDENCY_DC_PVR_H
#define TEST_RESIDENCY_DC_PVR_H

#include <stddef.h>
#include <stdint.h>

#include "../../../pvr-reservation-test/include/dc/pvr.h"

typedef enum pvr_txr_residency_state {
    PVR_TXR_RESIDENCY_EMPTY = 0,
    PVR_TXR_RESIDENCY_LOADING,
    PVR_TXR_RESIDENCY_READY
} pvr_txr_residency_state_t;

typedef struct pvr_txr_residency_slot {
    uint32_t identifier;
    uint32_t pin_count;
    uint64_t generation;
    uint64_t last_use;
    pvr_txr_residency_state_t state;
} pvr_txr_residency_slot_t;

typedef struct pvr_txr_residency_handle {
    size_t slot;
    uint64_t generation;
} pvr_txr_residency_handle_t;

typedef struct pvr_txr_residency_status {
    size_t slot_count;
    size_t ready_slots;
    size_t loading_slots;
    size_t pinned_slots;
    uint64_t pin_count;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} pvr_txr_residency_status_t;

typedef struct pvr_txr_residency {
    pvr_mem_reservation_t reservation;
    pvr_txr_residency_slot_t *slots;
    pvr_txr_surface_t *surfaces;
    size_t slot_count;
    size_t slot_stride;
    uint64_t clock;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint32_t _magic;
} pvr_txr_residency_t;

int pvr_txr_residency_init(pvr_txr_residency_t *cache,
                           pvr_txr_residency_slot_t *slots,
                           pvr_txr_surface_t *surfaces, size_t slot_count,
                           const pvr_txr_surface_t *prototype);
int pvr_txr_residency_acquire(pvr_txr_residency_t *cache,
                              uint32_t identifier,
                              pvr_txr_residency_handle_t *handle,
                              pvr_txr_surface_t **surface);
int pvr_txr_residency_reserve(pvr_txr_residency_t *cache,
                              uint32_t identifier,
                              pvr_txr_residency_handle_t *handle,
                              pvr_txr_surface_t **surface);
int pvr_txr_residency_publish(pvr_txr_residency_t *cache,
                              pvr_txr_residency_handle_t handle);
int pvr_txr_residency_abort(pvr_txr_residency_t *cache,
                            pvr_txr_residency_handle_t handle);
int pvr_txr_residency_unpin(pvr_txr_residency_t *cache,
                            pvr_txr_residency_handle_t handle);
int pvr_txr_residency_get_status(const pvr_txr_residency_t *cache,
                                 pvr_txr_residency_status_t *status);
int pvr_txr_residency_destroy(pvr_txr_residency_t *cache);

#endif /* TEST_RESIDENCY_DC_PVR_H */
