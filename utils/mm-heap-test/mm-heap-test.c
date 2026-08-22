/* KallistiOS ##version##

   mm-heap-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/heap.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REGION_BYTES (64u * 1024u)
#define SLOT_COUNT 64u
#define ITERATIONS 20000u
#define THREAD_COUNT 4u
#define THREAD_ITERATIONS 5000u

_Thread_local bool mm_heap_test_irq_context;

typedef struct allocation {
    void *ptr;
    size_t size;
    uint8_t fill;
} allocation_t;

static int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

static uint32_t next_random(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void check_bytes(const allocation_t *allocation) {
    const uint8_t *bytes = allocation->ptr;
    size_t i;

    for(i = 0; i < allocation->size; ++i)
        CHECK(bytes[i] == allocation->fill);
}

static void test_alignment_and_stats(void) {
    uint8_t region[4096 + MM_HEAP_ALIGNMENT];
    mm_heap_stats_t initial;
    mm_heap_stats_t used;
    mm_heap_stats_t final;
    mm_heap_t *heap;
    void *a;
    void *b;

    heap = mm_heap_create(region + 1, sizeof(region) - 1);
    CHECK(heap != NULL);
    CHECK(mm_heap_get_stats(heap, &initial) == 0);
    CHECK(initial.live_allocations == 0);
    CHECK(initial.free_blocks == 1);

    a = mm_heap_alloc(heap, 1);
    b = mm_heap_calloc(heap, 17, 3);
    CHECK(a != NULL && b != NULL);
    CHECK(((uintptr_t)a & (MM_HEAP_ALIGNMENT - 1u)) == 0);
    CHECK(((uintptr_t)b & (MM_HEAP_ALIGNMENT - 1u)) == 0);
    CHECK(((uint8_t *)b)[50] == 0);
    CHECK(mm_heap_get_stats(heap, &used) == 0);
    CHECK(used.live_allocations == 2);
    CHECK(used.allocated_bytes == 52);
    CHECK(used.reserved_bytes >= used.allocated_bytes);

    errno = 0;
    CHECK(mm_heap_destroy(heap) < 0 && errno == EBUSY);
    CHECK(mm_heap_free(heap, a) == 0);
    CHECK(mm_heap_free(heap, b) == 0);
    CHECK(mm_heap_validate(heap) == 0);
    CHECK(mm_heap_get_stats(heap, &final) == 0);
    CHECK(final.free_blocks == 1);
    CHECK(final.free_bytes == initial.free_bytes);
    CHECK(final.high_watermark == 52);
    CHECK(mm_heap_destroy(heap) == 0);
}

static void test_reallocation(void) {
    uint8_t region[8192];
    mm_heap_t *heap = mm_heap_create(region, sizeof(region));
    uint8_t *first;
    uint8_t *second;
    uint8_t *grown;
    size_t i;

    CHECK(heap != NULL);
    first = mm_heap_alloc(heap, 100);
    second = mm_heap_alloc(heap, 100);
    CHECK(first != NULL && second != NULL);
    memset(first, 0x5a, 100);
    CHECK(mm_heap_free(heap, second) == 0);

    grown = mm_heap_realloc(heap, first, 700);
    CHECK(grown == first);

    for(i = 0; i < 100; ++i)
        CHECK(grown[i] == 0x5a);

    grown = mm_heap_realloc(heap, grown, 37);
    CHECK(grown == first);

    for(i = 0; i < 37; ++i)
        CHECK(grown[i] == 0x5a);

    CHECK(mm_heap_free(heap, grown) == 0);
    CHECK(mm_heap_destroy(heap) == 0);
}

static void test_randomized_operations(void) {
    uint8_t region[REGION_BYTES + MM_HEAP_ALIGNMENT];
    allocation_t slots[SLOT_COUNT] = { { 0 } };
    mm_heap_stats_t stats;
    mm_heap_t *heap;
    uint32_t random = UINT32_C(0xc001d00d);
    size_t i;

    heap = mm_heap_create(region + 7, sizeof(region) - 7);
    CHECK(heap != NULL);

    for(i = 0; i < ITERATIONS; ++i) {
        const size_t index = next_random(&random) % SLOT_COUNT;
        allocation_t *slot = &slots[index];
        const uint32_t operation = next_random(&random) % 3u;

        if(!slot->ptr) {
            const size_t size = 1u + next_random(&random) % 1200u;
            void *ptr = mm_heap_alloc(heap, size);

            if(ptr) {
                slot->ptr = ptr;
                slot->size = size;
                slot->fill = (uint8_t)(index + 1u);
                memset(slot->ptr, slot->fill, slot->size);
            }
            else {
                CHECK(errno == ENOMEM);
            }
        }
        else if(operation == 0) {
            check_bytes(slot);
            CHECK(mm_heap_free(heap, slot->ptr) == 0);
            memset(slot, 0, sizeof(*slot));
        }
        else {
            const size_t size = 1u + next_random(&random) % 1600u;
            const size_t preserved = slot->size < size ? slot->size : size;
            uint8_t *ptr;
            size_t byte;

            check_bytes(slot);
            ptr = mm_heap_realloc(heap, slot->ptr, size);

            if(ptr) {
                for(byte = 0; byte < preserved; ++byte)
                    CHECK(ptr[byte] == slot->fill);

                slot->ptr = ptr;
                slot->size = size;
                memset(slot->ptr, slot->fill, slot->size);
            }
            else {
                CHECK(errno == ENOMEM);
                check_bytes(slot);
            }
        }

        CHECK(mm_heap_validate(heap) == 0);
    }

    for(i = 0; i < SLOT_COUNT; ++i) {
        if(slots[i].ptr) {
            check_bytes(&slots[i]);
            CHECK(mm_heap_free(heap, slots[i].ptr) == 0);
        }
    }

    CHECK(mm_heap_get_stats(heap, &stats) == 0);
    CHECK(stats.live_allocations == 0);
    CHECK(stats.free_blocks == 1);
    CHECK(stats.free_bytes == stats.capacity_bytes);
    CHECK(mm_heap_destroy(heap) == 0);
}

static void test_failures(void) {
    uint8_t region[2048];
    mm_heap_t *heap;
    void *ptr;
    int unrelated;

    errno = 0;
    CHECK(mm_heap_create(NULL, sizeof(region)) == NULL && errno == EINVAL);
    errno = 0;
    CHECK(mm_heap_create(region, 16) == NULL && errno == ENOSPC);

    heap = mm_heap_create(region, sizeof(region));
    CHECK(heap != NULL);
    errno = 0;
    CHECK(mm_heap_alloc(heap, 0) == NULL && errno == EINVAL);
    errno = 0;
    CHECK(mm_heap_calloc(heap, SIZE_MAX, 2) == NULL && errno == ENOMEM);
    errno = 0;
    CHECK(mm_heap_free(heap, &unrelated) < 0 && errno == EINVAL);

    ptr = mm_heap_alloc(heap, 64);
    CHECK(ptr != NULL);
    errno = 0;
    CHECK(mm_heap_free(heap, (uint8_t *)ptr + 1) < 0 && errno == EINVAL);
    CHECK(mm_heap_free(heap, ptr) == 0);
    errno = 0;
    CHECK(mm_heap_free(heap, ptr) < 0 && errno == EINVAL);
    CHECK(mm_heap_destroy(heap) == 0);
}

typedef struct worker_context {
    mm_heap_t *heap;
    uint32_t random;
    int failed;
} worker_context_t;

static void *heap_worker(void *argument) {
    worker_context_t *context = argument;
    size_t iteration;

    for(iteration = 0; iteration < THREAD_ITERATIONS; ++iteration) {
        const size_t old_size = 1u + next_random(&context->random) % 1024u;
        const size_t new_size = 1u + next_random(&context->random) % 1536u;
        const size_t preserved = old_size < new_size ? old_size : new_size;
        const uint8_t fill = (uint8_t)(context->random | 1u);
        uint8_t *allocation = mm_heap_alloc(context->heap, old_size);
        size_t byte;

        if(!allocation) {
            context->failed = 1;
            break;
        }

        memset(allocation, fill, old_size);
        allocation = mm_heap_realloc(context->heap, allocation, new_size);

        if(!allocation) {
            context->failed = 1;
            break;
        }

        for(byte = 0; byte < preserved; ++byte) {
            if(allocation[byte] != fill) {
                context->failed = 1;
                break;
            }
        }

        if(mm_heap_free(context->heap, allocation) < 0)
            context->failed = 1;

        if(context->failed)
            break;
    }

    return NULL;
}

static void test_concurrent_operations(void) {
    uint8_t region[64u * 1024u + MM_HEAP_ALIGNMENT];
    worker_context_t contexts[THREAD_COUNT];
    pthread_t threads[THREAD_COUNT];
    mm_heap_stats_t stats;
    mm_heap_t *heap = mm_heap_create(region, sizeof(region));
    size_t created = 0;
    size_t thread;

    CHECK(heap != NULL);
    if(!heap)
        return;

    for(thread = 0; thread < THREAD_COUNT; ++thread) {
        contexts[thread].heap = heap;
        contexts[thread].random = UINT32_C(0x12340000) + (uint32_t)thread;
        contexts[thread].failed = 0;

        if(pthread_create(&threads[thread], NULL, heap_worker,
                          &contexts[thread]) != 0)
            break;

        ++created;
    }

    CHECK(created == THREAD_COUNT);

    for(thread = 0; thread < created; ++thread) {
        CHECK(pthread_join(threads[thread], NULL) == 0);
        CHECK(contexts[thread].failed == 0);
    }

    CHECK(mm_heap_validate(heap) == 0);
    CHECK(mm_heap_get_stats(heap, &stats) == 0);
    CHECK(stats.live_allocations == 0);
    CHECK(stats.free_blocks == 1);
    CHECK(mm_heap_destroy(heap) == 0);
}

static void test_interrupt_rejection(void) {
    uint8_t region[2048];
    mm_heap_stats_t stats;
    mm_heap_t *heap = mm_heap_create(region, sizeof(region));

    CHECK(heap != NULL);
    if(!heap)
        return;

    mm_heap_test_irq_context = true;
    errno = 0;
    CHECK(mm_heap_alloc(heap, 32) == NULL && errno == EPERM);
    errno = 0;
    CHECK(mm_heap_get_stats(heap, &stats) < 0 && errno == EPERM);
    errno = 0;
    CHECK(mm_heap_destroy(heap) < 0 && errno == EPERM);
    mm_heap_test_irq_context = false;

    CHECK(mm_heap_destroy(heap) == 0);
}

int main(void) {
    test_alignment_and_stats();
    test_reallocation();
    test_randomized_operations();
    test_failures();
    test_concurrent_operations();
    test_interrupt_rejection();

    if(failures) {
        fprintf(stderr, "%d independent-heap checks failed\n", failures);
        return 1;
    }

    puts("independent-heap tests passed");
    return 0;
}
