/*
 * SH-4 IDCT for the libSFD float coefficient domain.
 * Copyright (c) 2026 Falco Girgis (original SH4ZAM implementation)
 * Copyright (c) 2026 Joseph Black (libSFD float integration)
 */
#include "idct.h"
#include <sh4zam/shz_matrix.h>

/* sqrt(2)*cos(k*pi/16), rounded by the compiler to binary32. The odd
   matrix is column-major. Native type alignment applies to this object. */
#define C1 1.3870398453221475f
#define C2 1.3065629648763766f
#define C3 1.1758756024193588f
#define C5 0.78569495838710235f
#define C6 0.54119610014619712f
#define C7 0.27589937928294306f
static const shz_mat4x4_t odd_basis = { .elem = {
     C1,  C3,  C5,  C7,
     C3, -C7, -C1, -C5,
     C5, -C1,  C7,  C3,
     C7, -C5,  C3, -C1
} };

static inline void transform(const float *a, size_t stride, float out[8]) {
    float p = a[0] + a[4 * stride];
    float q = a[0] - a[4 * stride];
    float r = C2 * a[2 * stride] + C6 * a[6 * stride];
    float s = C6 * a[2 * stride] - C2 * a[6 * stride];
    shz_vec4_t odd = shz_xmtrx_transform_vec4(shz_vec4_init(
        a[stride], a[3 * stride], a[5 * stride], a[7 * stride]));
    out[0] = p + r + odd.x;
    out[7] = p + r - odd.x;
    out[1] = q + s + odd.y;
    out[6] = q + s - odd.y;
    out[2] = q - s + odd.z;
    out[5] = q - s - odd.z;
    out[3] = p - r + odd.w;
    out[4] = p - r - odd.w;
}

static inline float round_float_residual(float biased) {
    float rounded = shz_truncf(biased);
    return rounded == 0.0f ? 0.0f : rounded;
}

/* Constant output selection is specialized into each public entry. */
SHZ_FORCE_INLINE int idct_batch(const float *coeff, void *residual,
                                size_t count, int float_output) {
    if(count > 6 || (count && (!coeff || !residual)))
        return -1;
    if(!count)
        return 0;

    shz_mat4x4_t saved;
    float rows[64], column[8];
    shz_xmtrx_store_4x4(&saved);
    shz_xmtrx_load_4x4(&odd_basis);
    for(size_t block = 0; block < count; ++block) {
        for(size_t y = 0; y < 8; ++y)
            transform(coeff + block * 64 + y * 8, 1, rows + y * 8);
        for(size_t x = 0; x < 8; ++x) {
            transform(rows + x, 8, column);
            for(size_t y = 0; y < 8; ++y) {
                float value = column[y] * 0.125f;
                /* Admitted domain gives |value| <= 16384, safely in range. */
                /* Preserve the legacy binary32 bias before truncation. This
                   is not mathematical nearest rounding at every half edge. */
                float biased = value + (value < 0.0f ? -0.5f : 0.5f);
                size_t index = block * 64 + y * 8 + x;
                if(float_output)
                    ((float *)residual)[index] = round_float_residual(biased);
                else
                    ((int16_t *)residual)[index] = (int16_t)biased;
            }
        }
    }
    shz_xmtrx_load_4x4(&saved);
    return 0;
}

int kosdec_sfd_private_idct_batch(const float *coeff, int16_t *residual, size_t count) {
    return idct_batch(coeff, residual, count, 0);
}

int kosdec_sfd_private_idct_batch_f32(const float *coeff, float *residual, size_t count) {
    return idct_batch(coeff, residual, count, 1);
}

int kosdec_sfd_private_idct_block_f32(const float *coeff, float *residual,
                                     enum kosdec_sfd_idct_class kind) {
    if(!coeff || !residual || (unsigned)kind > KOSDEC_SFD_IDCT_DENSE)
        return -1;
    if(kind == KOSDEC_SFD_IDCT_DENSE)
        return kosdec_sfd_private_idct_batch_f32(coeff, residual, 1);

    /* Both passes have a constant DC basis. The validated coefficient
       domain makes this scale exact, with the same bias and truncation
       as the dense path. Neither shortcut needs matrix state. */
    float sample = 0.0f;
    if(kind == KOSDEC_SFD_IDCT_DC) {
        float value = coeff[0] * 0.125f;
        sample = round_float_residual(value + (value < 0.0f ? -0.5f : 0.5f));
    }
    for(unsigned i = 0; i < 64; ++i)
        residual[i] = sample;
    return 0;
}
