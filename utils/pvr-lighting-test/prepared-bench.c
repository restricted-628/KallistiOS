/* KallistiOS ##version##
   Complete scalar preparation cost, including snapshot copies each batch.
   Copyright (C) 2026 Joseph Black
*/
#include <dc/pvr_lighting.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#ifdef __DREAMCAST__
#include <kos.h>
#else
#include <time.h>
#endif

static uint64_t now_us(void) {
#ifdef __DREAMCAST__
    return timer_us_gettime64();
#else
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
#endif
}

static volatile uint32_t checksum;
int main(void) {
    const unsigned counts[] = { 1, 4, 16, 64, 256 };
    const unsigned light_counts[] = { 1, 4, 9 };
    pvr_light_t lights[9], storage[9];
    pvr_lighting_context_t source = { { .02f, .03f, .04f }, lights, 0 };
    pvr_lighting_prepared_t prepared;
    pvr_lighting_sample_t sample = {
        .normal = { .z = 1 }, .color = { .25f, .5f, .75f, 1 }
    };
    pvr_lighting_stream_t one = { &sample, 1, sizeof(sample) };
    for(unsigned i = 0; i < 9; ++i)
        lights[i] = (pvr_light_t){ .kind = PVR_LIGHT_DIRECTIONAL,
            .source.direction = { .x = .3f + i, .y = .2f, .z = 2.1f },
            .color = { .x = .2f, .y = .3f, .z = .4f }, .intensity = .125f };
    printf("LIGHTPREP storage_per_light=%u descriptor=%u\n",
           (unsigned)sizeof(storage[0]), (unsigned)sizeof(prepared));
    for(unsigned l = 0; l < 3; ++l) {
        source.light_count = light_counts[l];
        for(unsigned c = 0; c < 5; ++c) {
            unsigned refs = counts[c], batches = 32768 / refs;
            uint32_t expected, actual;
            assert(pvr_lighting_apply(&expected, 1, &one, &source, NULL) == 0);
            assert(pvr_lighting_prepare(&prepared, storage, 9, &source) == 0);
            assert(pvr_lighting_apply_prepared(&actual, 1, &one, &prepared, NULL) == 0);
            assert(expected == actual);
            for(unsigned trial = 0; trial < 3; ++trial) {
                /* Alternate order; each path includes the same output use. */
                for(unsigned turn = 0; turn < 2; ++turn) {
                    unsigned use_prepared = turn ^ (trial & 1u);
                    uint64_t start = now_us();
                    for(unsigned b = 0; b < batches; ++b) {
                        if(use_prepared)
                            assert(pvr_lighting_prepare(&prepared, storage, 9, &source) == 0);
                        for(unsigned i = 0; i < refs; ++i) {
                            int rc = use_prepared ?
                                pvr_lighting_apply_prepared(&actual, 1, &one, &prepared, NULL) :
                                pvr_lighting_apply(&actual, 1, &one, &source, NULL);
                            assert(rc == 0 && actual == expected);
                            checksum ^= actual;
                        }
                    }
                    printf("LIGHTPREP lights=%u refs=%u batches=%u trial=%u prepared=%u total_us=%llu\n",
                        light_counts[l], refs, batches, trial, use_prepared,
                        (unsigned long long)(now_us() - start));
                }
            }
        }
    }
    puts("LIGHTPREP result=PASS");
    return 0;
}
