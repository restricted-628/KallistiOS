/* KallistiOS ##version##

   g2-state.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRANSFER_BYTES 32u

static alignas(32) uint8_t source[TRANSFER_BYTES];
static alignas(32) uint8_t destination[TRANSFER_BYTES];
static void *chain_address;
static volatile unsigned int callbacks;
static volatile bool chain_failed;

static void chain(void *data) {
    (void)data;
    ++callbacks;
    if(callbacks == 1 &&
       g2_dma_transfer(source, chain_address, TRANSFER_BYTES, 0, chain, NULL,
                       G2_DMA_TO_G2, 0, G2_DMA_CHAN_SPU, 0) < 0)
        chain_failed = true;
}

static int status_complete(uint32_t channel, size_t bytes) {
    g2_dma_status_t status;

    return g2_dma_get_status(channel, &status) == 0
        && status.state == G2_DMA_STATE_COMPLETE
        && status.requested_bytes == bytes
        && status.remaining_bytes == 0
        && status.result == 0;
}

int main(int argc, char **argv) {
    uint32_t sound_offset;
    void *g2_address;
    unsigned int index;
    int failed = 0;

    (void)argc;
    (void)argv;

    for(index = 0; index < TRANSFER_BYTES; ++index)
        source[index] = (uint8_t)(index * 7u + 3u);
    memset(destination, 0, sizeof(destination));

    if(snd_init() < 0) {
        printf("G2-STATE: FAIL sound initialization\n");
        return EXIT_FAILURE;
    }
    sound_offset = snd_mem_malloc(TRANSFER_BYTES);
    if(!sound_offset) {
        printf("G2-STATE: FAIL sound allocation\n");
        return EXIT_FAILURE;
    }

    g2_address = (void *)(uintptr_t)(SPU_RAM_BASE | sound_offset);
    if(g2_dma_transfer(source, g2_address, TRANSFER_BYTES, 1, NULL, NULL,
                       G2_DMA_TO_G2, 0, G2_DMA_CHAN_SPU, 0) < 0 ||
       !status_complete(G2_DMA_CHAN_SPU, TRANSFER_BYTES))
        failed = 1;

    if(!failed &&
       (g2_dma_transfer(destination, g2_address, TRANSFER_BYTES, 1, NULL,
                        NULL, G2_DMA_TO_SH4, 0, G2_DMA_CHAN_SPU, 0) < 0 ||
        !status_complete(G2_DMA_CHAN_SPU, TRANSFER_BYTES) ||
        memcmp(source, destination, TRANSFER_BYTES) != 0))
        failed = 1;

    chain_address = g2_address;
    if(!failed &&
       (g2_dma_transfer(source, g2_address, TRANSFER_BYTES, 1, chain, NULL,
                        G2_DMA_TO_G2, 0, G2_DMA_CHAN_SPU, 0) < 0 ||
        g2_dma_wait(G2_DMA_CHAN_SPU, 1000) < 0))
        failed = 1;
    if(callbacks != 2 || chain_failed) failed = 1;
    /* A timed wait never returns buffer ownership. Drain any started channel
       before releasing the sound allocation, even on the failure path. */
    while(g2_dma_cancel(G2_DMA_CHAN_SPU) < 0 && errno == EBUSY)
        (void)g2_dma_wait(G2_DMA_CHAN_SPU, 0);
    snd_mem_free(sound_offset);
    snd_shutdown();
    printf("G2-STATE: %s roundtrip=32 callbacks=%u\n",
           failed ? "FAIL" : "PASS", callbacks);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
