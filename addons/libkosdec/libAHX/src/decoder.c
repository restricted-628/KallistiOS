/* Original bounded AHX decoder and synthesis. */
#include <kosdec/ahx.h>
#include "frame.h"
#include "tables.h"

#include <stdalign.h>
#include <string.h>
#if defined(__sh__)
#ifndef KOSDEC_AHX_FMAC
/* The pinned SH4ZAM dot defaults to scalar C, even on SH-4. Select its
   FIPR implementation for this translation unit only. */
#define SHZ_SH4_DOT_FIPR
#include <sh4zam/shz_scalar.h>
#if SHZ_BACKEND != SHZ_SH4
#error libAHX FIPR synthesis requires the SH4ZAM single-precision SH-4 backend
#endif
#endif
#else
#include <fenv.h>
#include <math.h>
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#endif
#endif

enum { HISTORY_LENGTH = 1024 };

struct band_state {
    unsigned quantizer;
    unsigned scales[3];
};

struct kosdec_ahx_decoder {
    alignas(max_align_t) float history[HISTORY_LENGTH];
    struct kosdec_ahx_parser *parser;
    struct kosdec_ahx_frame frame;
    struct band_state bands[AHX_BANDS];
    int16_t pcm[KOSDEC_AHX_GRANULE_SAMPLES];
    unsigned position, granule, head, read, available, valid;
    enum kosdec_ahx_result terminal;
};

size_t kosdec_ahx_decoder_workspace_size(void) {
    return sizeof(struct kosdec_ahx_decoder) + kosdec_ahx_parser_workspace_size();
}

size_t kosdec_ahx_decoder_workspace_alignment(void) {
    return alignof(struct kosdec_ahx_decoder);
}

struct kosdec_ahx_decoder *kosdec_ahx_decoder_init(void *workspace, size_t bytes) {
    if(!workspace || bytes < kosdec_ahx_decoder_workspace_size() ||
       (uintptr_t)workspace % alignof(struct kosdec_ahx_decoder))
        return NULL;
    struct kosdec_ahx_decoder *d = workspace;
    struct kosdec_ahx_parser *parser = kosdec_ahx_parser_init(d + 1, bytes - sizeof(*d));
    if(!parser)
        return NULL;
    *d = (struct kosdec_ahx_decoder){ .parser = parser, .granule = AHX_GRANULES };
    return d;
}

bool kosdec_ahx_decoder_info(const struct kosdec_ahx_decoder *d, struct kosdec_ahx_info *info) {
    return d && kosdec_ahx_parser_info(d->parser, info);
}

static void read_scales(struct band_state *band, unsigned selection, const uint8_t *bytes, unsigned *at) {
    unsigned first = ahx_read_bits(bytes, at, 6);
    band->scales[0] = first;
    band->scales[1] = selection == 0 ? ahx_read_bits(bytes, at, 6) : first;
    band->scales[2] = selection == 2 ? first : ahx_read_bits(bytes, at, 6);
    if(selection == 3)
        band->scales[1] = band->scales[2];
}

static void prepare_frame(struct kosdec_ahx_decoder *d) {
    const uint8_t *bytes = d->frame.data;
    unsigned at = 32;
    unsigned selection[AHX_BANDS];
    for(unsigned band = 0; band < AHX_BANDS; ++band) {
        unsigned allocation = ahx_read_bits(bytes, &at, ahx_allocation_width(band));
        d->bands[band].quantizer = ahx_quantizer(band, allocation);
    }
    for(unsigned band = 0; band < AHX_BANDS; ++band)
        if(d->bands[band].quantizer)
            selection[band] = ahx_read_bits(bytes, &at, 2);
    for(unsigned band = 0; band < AHX_BANDS; ++band)
        if(d->bands[band].quantizer)
            read_scales(&d->bands[band], selection[band], bytes, &at);
    d->position = at;
    d->granule = 0;
}

static inline float multiply_add(float a, float b, float c) {
#if defined(__sh__)
    register float first __asm__("fr0") = a;
    __asm__("fmac %1,%2,%0" : "+f"(c) : "f"(first), "f"(b));
    return c;
#else
    return fmaf(a, b, c);
#endif
}

#ifndef KOSDEC_AHX_FMAC
static inline float dot4(const float *a, const float *b) {
#if defined(__sh__)
    /* The pinned scalar helper loads y,x,w,z into the physical lanes. */
    return shz_dot8f(a[1], a[0], a[3], a[2], b[1], b[0], b[3], b[2]);
#else
    /* Reference Flycast FIPR operation under the saved rounding mode. */
    double sum = (double)a[0] * b[0];
    sum += (double)a[1] * b[1];
    sum += (double)a[2] * b[2];
    sum += (double)a[3] * b[3];
    return (float)sum;
#endif
}
#endif

static float dot(const float *a, const float *b, unsigned length) {
    float sum = 0;
#ifdef KOSDEC_AHX_FMAC
    for(unsigned i = 0; i < length; ++i)
        sum = multiply_add(a[i], b[i], sum);
#else
    for(unsigned i = 0; i < length; i += 4)
        sum += dot4(a + i, b + i);
#endif
    return sum;
}

static int16_t pcm16(float sample) {
    if(sample >= 0x1p31f)
        return INT16_MAX;
    if(sample <= -0x1p31f)
        return INT16_MIN;
    int32_t upper = (uint32_t)(int32_t)sample >> 16;
    return (int16_t)(upper <= INT16_MAX ? upper : upper - 65536);
}

static void synthesize(struct kosdec_ahx_decoder *d, const float *subband, int16_t *pcm) {
    d->head = (d->head - 64u) & (HISTORY_LENGTH - 1);
    for(unsigned row = 0; row < 64; ++row)
        d->history[d->head + row] = dot(ahx_cosine[row], subband, AHX_SUBBANDS);
    for(unsigned i = 0; i < AHX_SUBBANDS; ++i) {
        float taps[16];
        for(unsigned tap = 0; tap < 16; ++tap) {
            unsigned at = d->head + i + 64u * tap + 32u * (tap % 2u);
            taps[tap] = d->history[at & (HISTORY_LENGTH - 1)];
        }
        pcm[i] = pcm16(dot(ahx_window[i], taps, 16));
    }
}

static bool unpack_band(unsigned q, const uint8_t *input, unsigned *at, unsigned samples[3]) {
    unsigned width = ahx_code_bits(q);
    if(ahx_grouped(q)) {
        unsigned packed = ahx_read_bits(input, at, width);
        unsigned levels = ahx_levels[q];
        if(packed >= levels * levels * levels)
            return false;
        for(unsigned i = 0; i < 3; ++i) {
            samples[i] = packed % levels;
            packed /= levels;
        }
    } else {
        for(unsigned i = 0; i < 3; ++i)
            samples[i] = ahx_read_bits(input, at, width);
    }
    return true;
}

static __attribute__((noinline)) bool synthesize_granule(struct kosdec_ahx_decoder *d) {
    float subband[3][AHX_SUBBANDS] = {{0}};
    const uint8_t *input = d->frame.data;
    unsigned at = d->position;
    for(unsigned band = 0; band < AHX_BANDS; ++band) {
        const struct band_state *state = &d->bands[band];
        unsigned q = state->quantizer;
        if(!q)
            continue;
        unsigned sample[3];
        if(!unpack_band(q, input, &at, sample))
            return false;
        float scale = ahx_scale[state->scales[d->granule / 4]];
        float gain = ahx_gain[q] * scale;
        float bias = ahx_bias[q] * scale;
        for(unsigned i = 0; i < 3; ++i)
            /* Samples fit signed int. On SH-4 an unsigned-to-float cast
               can temporarily switch PR to double precision. */
            subband[i][band] = multiply_add(gain, (float)(int)sample[i], bias);
    }
    for(unsigned i = 0; i < 3; ++i)
        synthesize(d, subband[i], d->pcm + i * AHX_SUBBANDS);
    d->position = at;
    unsigned start = d->granule++ * KOSDEC_AHX_GRANULE_SAMPLES;
    unsigned valid = d->frame.valid_samples > start ? d->frame.valid_samples - start : 0;
    d->valid = valid < KOSDEC_AHX_GRANULE_SAMPLES ? valid : KOSDEC_AHX_GRANULE_SAMPLES;
    d->available = KOSDEC_AHX_GRANULE_SAMPLES;
    d->read = 0;
    return true;
}

static enum kosdec_ahx_result decode_granule(struct kosdec_ahx_decoder *d) {
#if defined(__sh__)
    uint32_t saved_fpscr;
    uint32_t known_fpscr = 0x00040001; /* Single precision, DN, round toward zero. */
    __asm__ volatile("sts fpscr,%0" : "=r"(saved_fpscr));
    __asm__ volatile("lds %0,fpscr" : : "r"(known_fpscr) : "memory");
    bool valid = synthesize_granule(d);
    __asm__ volatile("lds %0,fpscr" : : "r"(saved_fpscr) : "memory");
#else
    fenv_t environment;
    if(feholdexcept(&environment))
        return KOSDEC_AHX_FP_ERROR;
    if(fesetround(FE_TOWARDZERO)) {
        (void)fesetenv(&environment);
        return KOSDEC_AHX_FP_ERROR;
    }
    bool valid = synthesize_granule(d);
    if(fesetenv(&environment))
        return KOSDEC_AHX_FP_ERROR;
#endif
    return valid ? KOSDEC_AHX_MORE : KOSDEC_AHX_INVALID;
}

struct kosdec_ahx_decode_result kosdec_ahx_decode(struct kosdec_ahx_decoder *d,
    const uint8_t *input, size_t bytes, bool final_input, int16_t *output, size_t capacity) {
    if(!d || (!input && bytes) || (!output && capacity))
        return (struct kosdec_ahx_decode_result){ .status = KOSDEC_AHX_BAD_ARGUMENT };
    if(d->terminal >= KOSDEC_AHX_INVALID)
        return (struct kosdec_ahx_decode_result){ .status = d->terminal };

    struct kosdec_ahx_decode_result result = { .status = KOSDEC_AHX_MORE };
    if(!d->available && d->granule == AHX_GRANULES) {
        enum kosdec_ahx_result state = kosdec_ahx_parser_feed(d->parser, input, bytes,
            final_input, &result.consumed, &d->frame);
        if(state != KOSDEC_AHX_FRAME) {
            result.status = state;
            return result;
        }
        prepare_frame(d);
    }
    if(!capacity) {
        result.status = KOSDEC_AHX_NEED_OUTPUT;
        return result;
    }
    if(!d->available) {
        result.status = decode_granule(d);
        if(result.status != KOSDEC_AHX_MORE) {
            d->terminal = result.status;
            return result;
        }
    }
    result.samples = capacity < d->available ? capacity : d->available;
    result.valid_samples = result.samples < d->valid ? result.samples : d->valid;
    memcpy(output, d->pcm + d->read, result.samples * sizeof(*output));
    d->read += result.samples;
    d->available -= result.samples;
    d->valid -= result.valid_samples;
    return result;
}
