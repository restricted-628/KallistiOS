/* KallistiOS ##version##

   Host-side contiguous PVR reservation contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static pvr_ptr_t allocator_result;
static size_t allocator_size;
static size_t allocation_calls;
static size_t free_calls;

pvr_ptr_t pvr_mem_malloc(size_t size) {
    ++allocation_calls;
    allocator_size = size;
    return allocator_result;
}

void pvr_mem_free(pvr_ptr_t pointer) {
    assert(pointer == allocator_result);
    ++free_calls;
}

void pvr_txr_surface_release(pvr_txr_surface_t *surface) {
    memset(surface, 0, sizeof(*surface));
}

static void reset_allocator(void) {
    allocator_result = (pvr_ptr_t)(uintptr_t)(PVR_RAM_INT_BASE + 0x1000u);
    allocator_size = 0;
    allocation_calls = 0;
    free_calls = 0;
}

static void test_reservation(void) {
    pvr_mem_reservation_t reservation = {
        (pvr_ptr_t)(uintptr_t)UINT32_C(0x11111111), 123u
    };
    pvr_mem_reservation_t unchanged = reservation;
    pvr_ptr_t slice = (pvr_ptr_t)(uintptr_t)UINT32_C(0x22222222);

    reset_allocator();
    allocator_result = NULL;
    errno = 0;
    assert(pvr_mem_reservation_alloc(&reservation, 4096) == -1);
    assert(errno == ENOMEM && allocation_calls == 1);
    assert(!memcmp(&reservation, &unchanged, sizeof(reservation)));

    reset_allocator();
    assert(pvr_mem_reservation_alloc(&reservation, 4096) == 0);
    assert(reservation.base == allocator_result &&
           reservation.capacity == 4096 && allocator_size == 4096);
    assert(pvr_mem_reservation_get(&reservation, 32, 1024, 32,
                                   &slice) == 0);
    assert(slice == (uint8_t *)reservation.base + 32);

    errno = 0;
    assert(pvr_mem_reservation_get(&reservation, 8, 64, 32,
                                   &slice) == -1);
    assert(errno == EINVAL && !slice);
    errno = 0;
    assert(pvr_mem_reservation_get(&reservation, 4000, 128, 8,
                                   &slice) == -1);
    assert(errno == ENOSPC && !slice);

    pvr_mem_reservation_release(&reservation);
    assert(free_calls == 1 && !reservation.base && !reservation.capacity);
}

static void init_surfaces(pvr_txr_surface_t surfaces[3]) {
    assert(pvr_txr_surface_init(&surfaces[0], 64, 64,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    assert(pvr_txr_surface_init(&surfaces[1], 32, 32,
                                PVR_TXR_SURFACE_ARGB4444,
                                PVR_TXR_SURFACE_TWIDDLED, false) == 0);
    assert(pvr_txr_surface_init_vq(&surfaces[2], 64, 64,
                                   PVR_TXR_SURFACE_RGB565,
                                   128, false) == 0);
}

static void test_surface_plan_and_binding(void) {
    pvr_txr_surface_t surfaces[3];
    pvr_txr_surface_t unchanged;
    pvr_mem_reservation_t reservation;
    size_t offsets[3] = { SIZE_MAX, SIZE_MAX, SIZE_MAX };
    size_t total = SIZE_MAX;
    size_t i;

    init_surfaces(surfaces);
    assert(pvr_txr_surface_plan_reservation(surfaces, 3, 32,
                                            offsets, &total) == 0);
    assert(offsets[0] == 0);
    assert(offsets[1] == surfaces[0].byte_size);
    assert(!(offsets[1] & 31u) && !(offsets[2] & 31u));
    assert(offsets[1] >= offsets[0] + surfaces[0].byte_size);
    assert(offsets[2] >= offsets[1] + surfaces[1].byte_size);
    assert(total == offsets[2] + surfaces[2].byte_size);

    reset_allocator();
    assert(pvr_mem_reservation_alloc(&reservation, total) == 0);
    for(i = 0; i < 3; ++i) {
        assert(pvr_txr_surface_bind_reservation(
            &surfaces[i], &reservation, offsets[i]) == 0);
        assert(surfaces[i].vram ==
               (uint8_t *)reservation.base + offsets[i]);
        assert(surfaces[i].capacity == surfaces[i].byte_size);
        assert(!surfaces[i].owns_vram);
    }
    {
        pvr_ptr_t texture_address;

        assert(pvr_txr_surface_get_texture_address(
            &surfaces[2], &texture_address) == 0);
        assert(texture_address == (uint8_t *)surfaces[2].vram - 1024u);
    }

    unchanged = surfaces[0];
    errno = 0;
    assert(pvr_txr_surface_bind_reservation(
        &surfaces[0], &reservation, offsets[0]) == -1);
    assert(errno == EBUSY &&
           !memcmp(&surfaces[0], &unchanged, sizeof(unchanged)));

    for(i = 0; i < 3; ++i)
        pvr_txr_surface_release(&surfaces[i]);
    pvr_mem_reservation_release(&reservation);
    assert(free_calls == 1);
}

static void test_plan_failures(void) {
    pvr_txr_surface_t surfaces[5];
    size_t offsets[5] = { 11, 22, 33, 44, 55 };
    size_t unchanged_offsets[5];
    size_t total = 77;
    size_t i;

    memcpy(unchanged_offsets, offsets, sizeof(offsets));
    for(i = 0; i < 5; ++i) {
        assert(pvr_txr_surface_init(&surfaces[i], 1024, 1024,
                                    PVR_TXR_SURFACE_RGB565,
                                    PVR_TXR_SURFACE_LINEAR, false) == 0);
    }
    errno = 0;
    assert(pvr_txr_surface_plan_reservation(surfaces, 5, 32,
                                            offsets, &total) == -1);
    assert(errno == ENOSPC && total == 77 &&
           !memcmp(offsets, unchanged_offsets, sizeof(offsets)));

    assert(pvr_txr_surface_init(&surfaces[0], 64, 64,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    surfaces[0].vram = (pvr_ptr_t)(uintptr_t)PVR_RAM_INT_BASE;
    errno = 0;
    assert(pvr_txr_surface_plan_reservation(surfaces, 1, 32,
                                            offsets, &total) == -1);
    assert(errno == EBUSY && total == 77 && offsets[0] == 11);

    surfaces[0].vram = NULL;
    errno = 0;
    assert(pvr_txr_surface_plan_reservation(surfaces, 1, 64,
                                            offsets, &total) == -1);
    assert(errno == ERANGE && total == 77 && offsets[0] == 11);

    total = SIZE_MAX;
    assert(pvr_txr_surface_plan_reservation(NULL, 0, 32,
                                            NULL, &total) == 0);
    assert(total == 0);
}

int main(void) {
    test_reservation();
    test_surface_plan_and_binding();
    test_plan_failures();
    puts("PVR contiguous reservation tests passed");
    return 0;
}
