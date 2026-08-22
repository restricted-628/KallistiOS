/* KallistiOS ##version##

   independent-heap.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STREAM_REGION_BYTES (48u * 1024u)
#define UI_REGION_BYTES     (12u * 1024u)

static alignas(32) uint8_t stream_region[STREAM_REGION_BYTES];
static alignas(32) uint8_t ui_region[UI_REGION_BYTES];

static int print_stats(const char *name, mm_heap_t *heap) {
    mm_heap_stats_t stats;

    if(mm_heap_get_stats(heap, &stats) < 0) {
        perror("mm_heap_get_stats");
        return -1;
    }

    printf("%s: requested=%lu reserved=%lu free=%lu largest=%lu live=%lu\n",
           name, (unsigned long)stats.allocated_bytes,
           (unsigned long)stats.reserved_bytes,
           (unsigned long)stats.free_bytes,
           (unsigned long)stats.largest_free_block,
           (unsigned long)stats.live_allocations);
    return 0;
}

int main(int argc, char **argv) {
    mm_heap_t *stream_heap = NULL;
    mm_heap_t *ui_heap = NULL;
    void *read_buffer = NULL;
    void *menu_state = NULL;
    int result = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    stream_heap = mm_heap_create(stream_region, sizeof(stream_region));
    ui_heap = mm_heap_create(ui_region, sizeof(ui_region));

    if(!stream_heap || !ui_heap) {
        perror("mm_heap_create");
        goto cleanup;
    }

    read_buffer = mm_heap_alloc(stream_heap, 24u * 1024u);
    menu_state = mm_heap_calloc(ui_heap, 96, sizeof(uint32_t));

    if(!read_buffer || !menu_state) {
        perror("independent heap allocation");
        goto cleanup;
    }

    if(((uintptr_t)read_buffer & (MM_HEAP_ALIGNMENT - 1u)) != 0 ||
       ((uintptr_t)menu_state & (MM_HEAP_ALIGNMENT - 1u)) != 0) {
        puts("independent heap returned a misaligned allocation");
        goto cleanup;
    }

    if(print_stats("stream", stream_heap) < 0 ||
       print_stats("ui", ui_heap) < 0)
        goto cleanup;

    result = EXIT_SUCCESS;

cleanup:
    if(read_buffer && mm_heap_free(stream_heap, read_buffer) < 0) {
        perror("mm_heap_free stream");
        result = EXIT_FAILURE;
    }

    if(menu_state && mm_heap_free(ui_heap, menu_state) < 0) {
        perror("mm_heap_free ui");
        result = EXIT_FAILURE;
    }

    if(stream_heap && (mm_heap_validate(stream_heap) < 0 ||
                       mm_heap_destroy(stream_heap) < 0)) {
        perror("stream heap cleanup");
        result = EXIT_FAILURE;
    }

    if(ui_heap && (mm_heap_validate(ui_heap) < 0 ||
                   mm_heap_destroy(ui_heap) < 0)) {
        perror("UI heap cleanup");
        result = EXIT_FAILURE;
    }

    if(result == EXIT_SUCCESS)
        puts("independent heap example passed");

    return result;
}
