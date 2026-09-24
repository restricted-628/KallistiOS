/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   PCM16 sample-order, tail, guard and nested MMU mapping regression. */
#include <kos.h>
#include <arch/mmu.h>
#include <dc/sound/sound.h>
#include <dc/spu.h>
#include <dc/sq.h>
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef INIT_MMU
KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_MMU);
#else
KOS_INIT_FLAGS(INIT_DEFAULT);
#endif

#define SOURCE_BYTES 16416u
static alignas(32) uint32_t source[SOURCE_BYTES / 4];
static alignas(32) uint8_t result[SOURCE_BYTES / 2 + 64];
static alignas(32) uint32_t outer_result[8];
static unsigned cases;

static int run_case(size_t bytes, uintptr_t left, uintptr_t right,
                     uintptr_t alias) {
    size_t channel_bytes = bytes / 2;
    spu_memset(left - 32, 0xa5a5a5a5u, channel_bytes + 64);
    spu_memset(right - 32, 0xa5a5a5a5u, channel_bytes + 64);
    memset(outer_result, 0, sizeof(outer_result));
    dcache_purge_range((uintptr_t)outer_result, sizeof(outer_result));
    uint32_t *outer = sq_lock(outer_result);
    if(!outer)
        return -1;
    uintptr_t base = alias ? SPU_RAM_BASE | alias : 0;
    errno = 0;
    snd_pcm16_split_sq(source, base + left, base + right, bytes);
    if(errno) {
        sq_unlock();
        return -1;
    }
    /* Both channel uploads must have restored the application's mapping. */
    for(size_t i = 0; i < 8; ++i)
        outer[i] = 0x12345678u;
    sq_flush(outer);
    sq_wait();
    sq_unlock();
    dcache_inval_range((uintptr_t)outer_result, sizeof(outer_result));
    for(size_t i = 0; i < 8; ++i)
        if(outer_result[i] != 0x12345678u)
            return -1;

    for(unsigned channel = 0; channel < 2; ++channel) {
        uintptr_t offset = channel ? right : left;
        spu_memread(result, offset - 32, channel_bytes + 64);
        for(size_t i = 0; i < 32; ++i)
            if(result[i] != 0xa5 || result[channel_bytes + 32 + i] != 0xa5)
                return -1;
        for(size_t i = 0; i < bytes / 4; ++i) {
            uint16_t sample;
            memcpy(&sample, result + 32 + 2 * i, sizeof(sample));
            if(sample != (uint16_t)(source[i] >> (16 * channel))) {
                printf("sample mismatch mode=%d channel=%u frame=%u\n",
                       mmu_enabled(), channel, (unsigned)i);
                return -1;
            }
        }
    }
    ++cases;
    return 0;
}

int main(int argc, char **argv) {
    static const size_t sizes[] = {32, 64, 96, 128, 160, 8192, 8224, 16416};
    (void)argc;
    (void)argv;
    for(size_t i = 0; i < SOURCE_BYTES / 4; ++i)
        source[i] = ((uint32_t)(uint16_t)(i * 997u + 0x8001u) << 16)
                  | (uint16_t)(i * 313u + 0x7fffu);

    for(unsigned mode = 0; mode < 2; ++mode) {
        if(mode)
            mmu_init_basic();
        if(mmu_enabled() != (mode != 0))
            goto fail;
        for(size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            if(run_case(sizes[i], 0xfffe0, 0x17ffe0, 0) < 0
               || run_case(sizes[i], 0x17ffe0, 0xfffe0, MEM_AREA_P2_BASE) < 0)
                goto fail;
        }
    }
    mmu_shutdown_basic();
    printf("STEREO-SQ: PASS cases=%u mmu=2 order=1 tails=1 guards=1 restore=1\n",
           cases);
    return EXIT_SUCCESS;
fail:
    fprintf(stderr, "STEREO-SQ: FAIL case=%u errno=%d\n", cases, errno);
    if(mmu_enabled())
        mmu_shutdown_basic();
    return EXIT_FAILURE;
}
