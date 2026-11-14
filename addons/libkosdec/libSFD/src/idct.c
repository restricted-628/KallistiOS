/* Original separable cosine factorization; no vendor decoder code. */
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
                if(float_output) {
                    float rounded = shz_truncf(biased);
                    ((float *)residual)[index] = rounded == 0.0f ? 0.0f : rounded;
                }
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
