/* KallistiOS ##version##

   Host-side fixed-slot PVR texture residency contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr.h>

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BASE (PVR_RAM_INT_BASE + UINT32_C(0x10000))

static size_t allocation_calls;
static size_t free_calls;
static int allocation_fails;

pvr_ptr_t pvr_mem_malloc(size_t byte_size) {
    ++allocation_calls;
    if(allocation_fails || !byte_size)
        return NULL;
    return (pvr_ptr_t)(uintptr_t)TEST_BASE;
}

void pvr_mem_free(pvr_ptr_t pointer) {
    assert(pointer == (pvr_ptr_t)(uintptr_t)TEST_BASE);
    ++free_calls;
}

void pvr_txr_surface_release(pvr_txr_surface_t *surface) {
    assert(surface && !surface->owns_vram);
    memset(surface, 0, sizeof(*surface));
}

static void reset_allocator(void) {
    allocation_calls = 0;
    free_calls = 0;
    allocation_fails = 0;
}

static void release_handle(pvr_txr_residency_t *cache,
                           pvr_txr_residency_handle_t handle) {
    assert(pvr_txr_residency_unpin(cache, handle) == 0);
}

static void test_lifecycle_and_lru(void) {
    pvr_txr_residency_t cache;
    pvr_txr_residency_slot_t slots[3];
    pvr_txr_surface_t surfaces[3];
    pvr_txr_surface_t prototype;
    pvr_txr_surface_t *surface;
    pvr_txr_residency_handle_t ten;
    pvr_txr_residency_handle_t ten_again;
    pvr_txr_residency_handle_t twenty;
    pvr_txr_residency_handle_t thirty;
    pvr_txr_residency_handle_t forty;
    pvr_txr_residency_handle_t handle;
    pvr_txr_residency_status_t status;
    size_t original_size;

    memset(&cache, 0xa5, sizeof(cache));
    memset(slots, 0xa5, sizeof(slots));
    memset(surfaces, 0xa5, sizeof(surfaces));
    assert(pvr_txr_surface_init(&prototype, 64, 64,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, false) == 0);

    reset_allocator();
    assert(pvr_txr_residency_init(&cache, slots, surfaces, 3,
                                  &prototype) == 0);
    assert(allocation_calls == 1 && free_calls == 0);
    assert(cache.slot_stride == prototype.byte_size);
    assert(cache.reservation.capacity == prototype.byte_size * 3u);
    assert(surfaces[0].vram == (pvr_ptr_t)(uintptr_t)TEST_BASE);
    assert(surfaces[1].vram == (uint8_t *)surfaces[0].vram
                               + cache.slot_stride);
    assert(surfaces[2].vram == (uint8_t *)surfaces[1].vram
                               + cache.slot_stride);

    assert(pvr_txr_residency_reserve(&cache, 10, &ten, &surface) == 0);
    assert(surface == &surfaces[0]);
    errno = 0;
    assert(pvr_txr_residency_acquire(&cache, 10, &handle,
                                     &surface) == -1);
    assert(errno == EAGAIN && !surface && !handle.generation);
    errno = 0;
    assert(pvr_txr_residency_reserve(&cache, 10, &handle,
                                     &surface) == -1);
    assert(errno == EALREADY && !surface && !handle.generation);

    assert(pvr_txr_residency_publish(&cache, ten) == 0);
    assert(pvr_txr_residency_acquire(&cache, 10, &ten_again,
                                     &surface) == 0);
    assert(ten_again.slot == ten.slot
           && ten_again.generation == ten.generation);
    release_handle(&cache, ten_again);
    release_handle(&cache, ten);

    assert(pvr_txr_residency_reserve(&cache, 20, &twenty,
                                     &surface) == 0);
    assert(pvr_txr_residency_publish(&cache, twenty) == 0);
    release_handle(&cache, twenty);

    assert(pvr_txr_residency_acquire(&cache, 10, &ten_again,
                                     &surface) == 0);
    release_handle(&cache, ten_again);

    assert(pvr_txr_residency_reserve(&cache, 30, &thirty,
                                     &surface) == 0);
    assert(pvr_txr_residency_publish(&cache, thirty) == 0);
    release_handle(&cache, thirty);

    /* Identifier 20 is older than 10 and 30, so the full cache replaces it. */
    assert(pvr_txr_residency_reserve(&cache, 40, &forty, &surface) == 0);
    assert(forty.slot == twenty.slot && forty.generation != twenty.generation);
    assert(pvr_txr_residency_publish(&cache, forty) == 0);
    release_handle(&cache, forty);
    errno = 0;
    assert(pvr_txr_residency_unpin(&cache, twenty) == -1);
    assert(errno == ESTALE);

    assert(pvr_txr_residency_acquire(&cache, 10, &ten, &surface) == 0);
    assert(pvr_txr_residency_acquire(&cache, 30, &thirty, &surface) == 0);
    assert(pvr_txr_residency_acquire(&cache, 40, &forty, &surface) == 0);
    errno = 0;
    assert(pvr_txr_residency_reserve(&cache, 50, &handle,
                                     &surface) == -1);
    assert(errno == EBUSY && !surface && !handle.generation);
    release_handle(&cache, ten);
    release_handle(&cache, thirty);
    release_handle(&cache, forty);

    assert(pvr_txr_residency_reserve(&cache, 50, &handle, &surface) == 0);
    assert(pvr_txr_residency_abort(&cache, handle) == 0);
    errno = 0;
    assert(pvr_txr_residency_acquire(&cache, 50, &handle,
                                     &surface) == -1);
    assert(errno == ENOENT && !surface);

    assert(pvr_txr_residency_get_status(&cache, &status) == 0);
    assert(status.slot_count == 3 && status.ready_slots == 2);
    assert(status.loading_slots == 0 && status.pinned_slots == 0);
    assert(status.pin_count == 0 && status.hits == 5);
    assert(status.misses == 2 && status.evictions == 2);

    original_size = surfaces[thirty.slot].byte_size;
    ++surfaces[thirty.slot].byte_size;
    errno = 0;
    assert(pvr_txr_residency_acquire(&cache, 30, &handle,
                                     &surface) == -1);
    assert(errno == EINVAL && !surface);
    surfaces[thirty.slot].byte_size = original_size;

    surfaces[thirty.slot].vram = (uint8_t *)surfaces[thirty.slot].vram + 32u;
    errno = 0;
    assert(pvr_txr_residency_acquire(&cache, 30, &handle,
                                     &surface) == -1);
    assert(errno == EINVAL && !surface);
    surfaces[thirty.slot].vram
        = (uint8_t *)surfaces[thirty.slot].vram - 32u;

    assert(pvr_txr_residency_acquire(&cache, 30, &ten, &surface) == 0);
    errno = 0;
    assert(pvr_txr_residency_destroy(&cache) == -1);
    assert(errno == EBUSY);
    release_handle(&cache, ten);
    assert(pvr_txr_residency_destroy(&cache) == 0);
    assert(free_calls == 1 && !cache.reservation.base && !cache.slots);
    assert(!memcmp(slots, (pvr_txr_residency_slot_t[3]){{0}},
                   sizeof(slots)));
    assert(!memcmp(surfaces, (pvr_txr_surface_t[3]){{0}},
                   sizeof(surfaces)));
}

static void test_init_failures(void) {
    pvr_txr_residency_t cache;
    pvr_txr_residency_slot_t slots[2];
    pvr_txr_surface_t surfaces[2];
    pvr_txr_surface_t prototype;
    alignas(max_align_t) unsigned char overlap[256];

    assert(pvr_txr_surface_init(&prototype, 64, 64,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, false) == 0);

    errno = 0;
    assert(pvr_txr_residency_init(NULL, slots, surfaces, 2,
                                  &prototype) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_txr_residency_init(&cache, slots, surfaces, 0,
                                  &prototype) == -1);
    assert(errno == EINVAL);
    errno = 0;
    assert(pvr_txr_residency_init(
        &cache, (pvr_txr_residency_slot_t *)overlap,
        (pvr_txr_surface_t *)overlap, 1, &prototype) == -1);
    assert(errno == EINVAL);

    prototype.vram = (pvr_ptr_t)(uintptr_t)TEST_BASE;
    prototype.capacity = prototype.byte_size;
    errno = 0;
    assert(pvr_txr_residency_init(&cache, slots, surfaces, 2,
                                  &prototype) == -1);
    assert(errno == EBUSY);
    prototype.vram = NULL;
    prototype.capacity = 0;

    reset_allocator();
    allocation_fails = 1;
    errno = 0;
    assert(pvr_txr_residency_init(&cache, slots, surfaces, 2,
                                  &prototype) == -1);
    assert(errno == ENOMEM && allocation_calls == 1 && free_calls == 0);
}

int main(void) {
    test_lifecycle_and_lru();
    test_init_failures();
    puts("PVR texture residency tests passed");
    return 0;
}
