/* KallistiOS ##version##

   g2-state.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRANSFER_BYTES 32u

static alignas(32) uint8_t source[TRANSFER_BYTES];
static alignas(32) uint8_t destination[TRANSFER_BYTES];

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

    snd_mem_free(sound_offset);
    printf("G2-STATE: %s\n", failed ? "FAIL" : "PASS");
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
