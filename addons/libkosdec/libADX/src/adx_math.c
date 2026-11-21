/* Original scalar ADX math. */
#include "adx_math.h"
#include <sh4zam/shz_cdefs.h>
#if !defined(__sh__)
# if !defined(MEDIA_ADX_MATH_HOST_TEST)
#  error "Non-SH execution requires explicit test-only primitive emulation"
# endif
# include <math.h>
# include <string.h>
/* Emulate DN=1 only for host tests. No target call or runtime fallback. */
static inline float flush_subnormal(float x) {
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    if((bits & UINT32_C(0x7fffffff)) < UINT32_C(0x00800000)) {
        bits &= UINT32_C(0x80000000);
        memcpy(&x, &bits, sizeof(x));
    }
    return x;
}
#endif

/* Pinned shz_fmaf is a*b+c, separately rounded under our strict flags.
   This narrowly owned native primitive explicitly selects one-round FMAC. */
SHZ_FORCE_INLINE float multiply_add(float a, float b, float c) {
#if defined(__sh__)
    register float first __asm__("fr0") = a;
    __asm__("fmac %1,%2,%0" : "+f"(c) : "f"(first), "f"(b));
    return c;
#else
    return flush_subnormal(fmaf(a, b, c));
#endif
}

SHZ_FORCE_INLINE float multiply(float a, float b) {
    float result = a * b;
#if !defined(__sh__)
    result = flush_subnormal(result);
#endif
    return result;
}

/* Finite domain is established by coefficient admission and bounded jobs.
   Test representable endpoints BEFORE the signed conversion. */
SHZ_FORCE_INLINE int16_t pcm16(float scaled, unsigned *clips) {
    if(scaled >= 0x1p31f) {
        ++*clips;
        return INT16_MAX;
    }
    if(scaled <= -0x1p31f) {
        *clips += scaled < -0x1p31f;
        return INT16_MIN;
    }
    int32_t word = (int32_t)scaled;
    uint32_t upper = (uint32_t)word >> 16;
    int32_t sample = upper < 32768u ? (int32_t)upper : (int32_t)upper - 65536;
    return (int16_t)sample;
}

struct predictor {
    float c0, c1;
    float latest, preceding;
};

SHZ_FORCE_INLINE void expand_block(struct predictor *p, const uint8_t block[18],
                                  unsigned frames, unsigned visible, int16_t *pcm,
                                  unsigned stride, unsigned *clips) {
    /* Encoding 3 uses 13 scale bits; the high control bit was checked above. */
    unsigned scale_word = (unsigned)block[0] * 256u + block[1];
    float scale = (float)((scale_word & 0x1fffu) + 1u);
    float h1 = p->latest;
    float h2 = p->preceding;
    for(unsigned i = 0; i < frames; ++i) {
        unsigned packed = block[2 + i / 2];
        int nibble = (int)((packed >> ((i & 1) ? 0 : 4)) & 15);
        if(nibble >= 8) nibble -= 16;
        float residual = (float)nibble * 0x1p16f;
        float value = multiply(p->c1, h2);
        value = multiply_add(p->c0, h1, value);
        value = multiply_add(scale, residual, value);
        if(i < visible)
            pcm[i * stride] = pcm16(value, clips);
        h2 = h1;
        /* The predictor feeds back the float even if the emitted PCM clipped. */
        h1 = value;
    }
    p->latest = h1;
    p->preceding = h2;
}

SHZ_FORCE_INLINE int channel_job(const uint8_t *blocks, unsigned frames,
                                 unsigned visible, const int16_t q12[2],
                                 int16_t history[2], int16_t *pcm, unsigned stride,
                                 unsigned maximum) {
    if(!blocks || !q12 || !history || (!pcm && visible) || !frames ||
       frames > maximum || visible > frames || (stride != 1 && stride != 2))
        return -1;
    if(q12[0] < 0 || q12[0] > 8192 || q12[1] < -4096 || q12[1] > 0)
        return -1;
    /* With the admitted individual bounds, this places both predictor poles
       at or inside the unit circle for a multi-block job. */
    if(frames > 32 && q12[0] + q12[1] > 4096)
        return -1;
    for(unsigned i = 0; i < frames; i += 32)
        if(blocks[i / 32 * 18 * stride] & 0x80)
            return -1;

    struct predictor p = {
        .c0 = (float)q12[0] * 0x1p-12f,
        .c1 = (float)q12[1] * 0x1p-12f,
        .latest = (float)history[0] * 0x1p16f,
        .preceding = (float)history[1] * 0x1p16f,
    };
    unsigned clips = 0;
    for(unsigned i = 0; i < frames; i += 32) {
        unsigned count = frames - i < 32 ? frames - i : 32;
        unsigned copy = i < visible ? visible - i : 0;
        if(copy > count)
            copy = count;
        expand_block(&p, blocks + i / 32 * 18 * stride, count, copy,
                     copy ? pcm + i * stride : pcm, stride, &clips);
    }
    unsigned ignored = 0;
    history[0] = pcm16(p.latest, &ignored);
    history[1] = pcm16(p.preceding, &ignored);
    return (int)clips;
}

int media_adx_channel_group(const uint8_t block[18], unsigned frames,
                            const int16_t q12[2], int16_t history[2],
                            int16_t *pcm, unsigned stride) {
    return channel_job(block, frames, frames, q12, history, pcm, stride, 32);
}

int media_adx_channel_job(const uint8_t *blocks, unsigned decode_frames,
                         unsigned visible_frames, const int16_t q12[2],
                         int16_t history[2], int16_t *pcm, unsigned stride) {
    return channel_job(blocks, decode_frames, visible_frames, q12, history,
                       pcm, stride, MEDIA_ADX_JOB_MAX_FRAMES);
}

#ifdef MEDIA_ADX_MATH_TEST_INTERNAL
int16_t media_adx_test_pcm(float scaled, unsigned *clips) {
    return pcm16(scaled, clips);
}
float media_adx_test_mac(float a, float b, float c) {
    return multiply_add(a, b, c);
}
#endif
