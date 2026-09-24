/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_CDROM);

enum { WORKERS = 4, ROUNDS = 128, REGION_BYTES = 64 * 1024 };
static _Alignas(MM_HEAP_ALIGNMENT) uint8_t region[REGION_BYTES];
static _Alignas(MM_HEAP_ALIGNMENT) uint8_t peer_region[4096];
typedef struct worker_state {
    mm_heap_t *heap;
    unsigned id;
    unsigned completed;
} worker_state_t;

#define CHECK(test) do { if(!(test)) { \
    printf("HEAP-SAFETY: FAIL line=%d errno=%d\n", __LINE__, errno); \
    return 1; \
} } while(0)

static void *worker(void *argument) {
    worker_state_t *state = argument;
    const uint8_t fill = (uint8_t)(0x20 + state->id);

    for(unsigned i = 0; i < ROUNDS; ++i) {
        size_t bytes = 33 + (i * 17 + state->id) % 192;
        uint8_t *ptr = mm_heap_alloc(state->heap, bytes);
        if(!ptr || (uintptr_t)ptr % MM_HEAP_ALIGNMENT)
            return (void *)1;
        memset(ptr, fill, bytes);
        thd_pass();
        uint8_t *grown = mm_heap_realloc(state->heap, ptr, bytes + 96);
        if(!grown)
            return (void *)1;
        for(size_t j = 0; j < bytes; ++j) {
            if(grown[j] != fill)
                return (void *)1;
        }
        thd_pass();
        if(mm_heap_free(state->heap, grown) != 0)
            return (void *)1;
        ++state->completed;
    }
    return NULL;
}

int main(void) {
    worker_state_t states[WORKERS];
    kthread_t *threads[WORKERS];
    mm_heap_stats_t initial, final, peer_stats;
    mm_heap_t *heap = mm_heap_create(region, sizeof(region));
    mm_heap_t *peer = mm_heap_create(peer_region + 1, sizeof(peer_region) - 1);
    CHECK(heap && peer);
    CHECK(mm_heap_get_stats(heap, &initial) == 0);
    uint8_t *data = mm_heap_calloc(peer, 32, 4);
    CHECK(data && (uintptr_t)data % MM_HEAP_ALIGNMENT == 0);
    for(unsigned i = 0; i < 128; ++i)
        CHECK(data[i] == 0);
    memset(data, 0x69, 128);
    errno = 0;
    CHECK(mm_heap_free(heap, data) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(mm_heap_realloc(peer, data, SIZE_MAX) == NULL && errno == ENOMEM);
    for(unsigned i = 0; i < 128; ++i)
        CHECK(data[i] == 0x69);
    errno = 0;
    CHECK(mm_heap_destroy(peer) == -1 && errno == EBUSY);

    for(unsigned i = 0; i < WORKERS; ++i) {
        states[i] = (worker_state_t){ .heap = heap, .id = i };
        threads[i] = thd_create(false, worker, &states[i]);
        CHECK(threads[i] != NULL);
    }
    for(unsigned i = 0; i < WORKERS; ++i) {
        void *result = (void *)1;
        CHECK(thd_join(threads[i], &result) == 0);
        CHECK(result == NULL && states[i].completed == ROUNDS);
    }
    CHECK(mm_heap_validate(heap) == 0);
    CHECK(mm_heap_get_stats(heap, &final) == 0);
    CHECK(final.live_allocations == 0 && final.free_blocks == 1);
    CHECK(final.free_bytes == initial.free_bytes);
    CHECK(mm_heap_get_stats(peer, &peer_stats) == 0);
    CHECK(peer_stats.live_allocations == 1 && peer_stats.allocated_bytes == 128);
    for(unsigned i = 0; i < 128; ++i)
        CHECK(data[i] == 0x69);
    CHECK(mm_heap_free(peer, data) == 0);
    errno = 0;
    CHECK(mm_heap_free(peer, data) == -1 && errno == EINVAL);
    CHECK(mm_heap_destroy(peer) == 0);
    CHECK(mm_heap_destroy(heap) == 0);
    printf("HEAP-SAFETY: PASS threads=%u iterations=%u isolation=2\n",
           WORKERS, WORKERS * ROUNDS);
    return 0;
}
