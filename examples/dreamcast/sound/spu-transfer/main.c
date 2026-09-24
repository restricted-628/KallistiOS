/* KallistiOS ##version##

   main.c
   Copyright (C) 2026 Joseph Black

   Exercise queued DMA and exact-byte PIO sound-RAM transfers.
*/

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos.h>
#include <dc/biosfont.h>
#include <dc/spu.h>
#include <dc/video.h>

#define DMA_BYTES       (64u * 1024u)
#define PIO_BYTES       4099u
#define DMA_SPU_OFFSET  0x00040000u
#define PIO_SPU_OFFSET  0x00060003u

static alignas(32) uint8_t dma_source[DMA_BYTES];
static alignas(32) uint8_t dma_result[DMA_BYTES];
static uint8_t pio_source[PIO_BYTES];
static uint8_t pio_result[PIO_BYTES];
static volatile unsigned int callbacks;
static volatile spu_transfer_state_t callback_state;

static void show_result(bool passed, const char *message) {
    vid_clear(passed ? 0 : 96, passed ? 96 : 0, 0);
    bfont_draw_str(vram_s + vid_mode->width * 80 + 24,
                   vid_mode->width, true,
                   passed ? "SPU TRANSFER TEST: PASS"
                          : "SPU TRANSFER TEST: FAIL");
    bfont_draw_str(vram_s + vid_mode->width * 112 + 24,
                   vid_mode->width, true, message);
    thd_sleep(passed ? 10000 : 15000);
}

static void transfer_complete(spu_transfer_request_t *request,
                              const spu_transfer_status_t *status,
                              void *data) {
    (void)request;
    (void)data;
    callback_state = status->state;
    ++callbacks;
}

static int finish_transfer(spu_transfer_request_t *request,
                           spu_transfer_method_t expected_method) {
    spu_transfer_status_t status;
    unsigned int prior_callbacks = callbacks;

    if(spu_transfer_wait(request, 3000, &status) < 0) {
        perror("spu_transfer_wait");
        return -1;
    }
    if(status.state != SPU_TRANSFER_COMPLETE
       || status.active_method != expected_method
       || status.completed_bytes != status.requested_bytes) {
        fprintf(stderr, "unexpected terminal transfer status\n");
        errno = EIO;
        return -1;
    }
    if(spu_transfer_wait_callback(request, 1000) < 0) {
        perror("spu_transfer_wait_callback");
        return -1;
    }
    if(callbacks != prior_callbacks + 1
       || callback_state != SPU_TRANSFER_COMPLETE) {
        fprintf(stderr, "completion callback was not delivered coherently\n");
        errno = EIO;
        return -1;
    }
    if(spu_transfer_destroy(request) < 0) {
        perror("spu_transfer_destroy");
        return -1;
    }
    return 0;
}

static int submit_upload(uintptr_t offset, const void *source, size_t bytes,
                         spu_transfer_method_t method) {
    spu_transfer_request_t *request;

    if(spu_memload_async(offset, source, bytes, method, 2000,
                         transfer_complete, NULL, &request) < 0) {
        perror("spu_memload_async");
        return -1;
    }
    return finish_transfer(request, method);
}

static int submit_readback(void *destination, uintptr_t offset, size_t bytes,
                           spu_transfer_method_t method) {
    spu_transfer_request_t *request;

    if(spu_memread_async(destination, offset, bytes, method, 2000,
                         transfer_complete, NULL, &request) < 0) {
        perror("spu_memread_async");
        return -1;
    }
    return finish_transfer(request, method);
}

int main(int argc, char **argv) {
    size_t i;

    (void)argc;
    (void)argv;

    for(i = 0; i < DMA_BYTES; ++i)
        dma_source[i] = (uint8_t)((i * 37u + (i >> 8)) & 0xffu);
    for(i = 0; i < PIO_BYTES; ++i)
        pio_source[i] = (uint8_t)((i * 13u + 0x5au) & 0xffu);

    puts("Testing queued 64 KiB sound-RAM DMA round trip...");
    if(submit_upload(DMA_SPU_OFFSET, dma_source, sizeof(dma_source),
                     SPU_TRANSFER_DMA) < 0
       || submit_readback(dma_result, DMA_SPU_OFFSET, sizeof(dma_result),
                          SPU_TRANSFER_DMA) < 0
       || memcmp(dma_source, dma_result, sizeof(dma_source))) {
        fprintf(stderr, "DMA round trip failed\n");
        show_result(false, "DMA round trip failed");
        return EXIT_FAILURE;
    }

    puts("Testing queued odd-address, odd-length PIO round trip...");
    if(submit_upload(PIO_SPU_OFFSET, pio_source, sizeof(pio_source),
                     SPU_TRANSFER_PIO) < 0
       || submit_readback(pio_result, PIO_SPU_OFFSET, sizeof(pio_result),
                          SPU_TRANSFER_PIO) < 0
       || memcmp(pio_source, pio_result, sizeof(pio_source))) {
        fprintf(stderr, "PIO round trip failed\n");
        show_result(false, "PIO round trip failed");
        return EXIT_FAILURE;
    }

    printf("PASS: two round trips and %u callbacks completed.\n", callbacks);
    show_result(true, "DMA and exact-byte PIO readback matched");
    return EXIT_SUCCESS;
}
