/* Original scalar ADX math; no CRI code/tables or alternate runtime backend. */
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

/* Finite domain is established by integer API admission and the 32-step bound.
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

int media_adx_channel_group(const uint8_t block[18], unsigned frames,
                            const int16_t q12[2], int16_t history[2],
                            int16_t *pcm, unsigned stride) {
    if(!block || !q12 || !history || !pcm || !frames || frames > 32 ||
       (stride != 1 && stride != 2))
        return -1;
    if((block[0] & 0x80) || q12[0] < 0 || q12[0] > 8192 ||
       q12[1] < -4096 || q12[1] > 0)
        return -1;
    float c0 = (float)q12[0] * 0x1p-12f;
    float c1 = (float)q12[1] * 0x1p-12f;
    float h1 = (float)history[0] * 0x1p16f;
    float h2 = (float)history[1] * 0x1p16f;
    float scale = (float)((unsigned)block[0] * 256u + block[1] + 1u);
    unsigned clips = 0;
    for(unsigned i = 0; i < frames; ++i) {
        unsigned packed = block[2 + i / 2];
        int nibble = (int)((packed >> ((i & 1) ? 0 : 4)) & 15);
        if(nibble >= 8) nibble -= 16;
        float residual = (float)nibble * 0x1p16f;
        float value = multiply(c1, h2);
        value = multiply_add(c0, h1, value);
        value = multiply_add(scale, residual, value);
        pcm[i * stride] = pcm16(value, &clips);
        h2 = h1;
        h1 = value;
    }
    unsigned ignored = 0;
    history[0] = pcm16(h1, &ignored);
    history[1] = pcm16(h2, &ignored);
    return (int)clips;
}

#ifdef MEDIA_ADX_MATH_TEST_INTERNAL
int16_t media_adx_test_pcm(float scaled, unsigned *clips) {
    return pcm16(scaled, clips);
}
float media_adx_test_mac(float a, float b, float c) {
    return multiply_add(a, b, c);
}
#endif
