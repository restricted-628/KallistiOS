/*
 * Two-matrix SH-4 IDCT for the libSFD float coefficient domain.
 * Copyright (c) 2026 Falco Girgis (original SH4ZAM implementation)
 * Copyright (c) 2026 Joseph Black (libSFD float integration)
 *
 * Adapted for the natural coefficient domain and float residual contract.
 */
#include "idct.h"
#include <sh4zam/shz_matrix.h>

#define C1 1.3870398453221475f
#define C2 1.3065629648763766f
#define C3 1.1758756024193588f
#define C5 0.78569495838710235f
#define C6 0.54119610014619712f
#define C7 0.27589937928294306f

static const shz_mat4x4_t basis[2] = {
    {.elem = {1, 1, 1, 1, C2, C6, -C6, -C2, 1, -1, -1, 1, C6, -C2, C2, -C6}},
    {.elem = {C1, C3, C5, C7, C3, -C7, -C1, -C5, C5, -C1, C7, C3, C7, -C5, C3, -C1}}
};

static void pass_transpose(const float *input, float *output) {
    shz_vec4_t even[8];
    shz_xmtrx_load_4x4(&basis[0]);
    for(unsigned row = 0; row < 8; ++row) {
        const float *line = input + 8 * row;
        even[row] = shz_xmtrx_transform_vec4(shz_vec4_init(line[0], line[2], line[4], line[6]));
    }
    shz_xmtrx_load_4x4(&basis[1]);
    for(unsigned row = 0; row < 8; ++row) {
        const float *line = input + 8 * row;
        shz_vec4_t odd = shz_xmtrx_transform_vec4(shz_vec4_init(line[1], line[3], line[5], line[7]));
        const shz_vec4_t e = even[row];
        const float a[4] = {e.x, e.y, e.z, e.w};
        const float b[4] = {odd.x, odd.y, odd.z, odd.w};
        for(unsigned i = 0; i < 4; ++i) {
            output[8 * i + row] = a[i] + b[i];
            output[8 * (7 - i) + row] = a[i] - b[i];
        }
    }
}

void kosdec_sfd_private_idct_dual_f32(const float *coeff, float *residual) {
    float transpose[64];
    shz_mat4x4_t saved;
    shz_xmtrx_store_4x4(&saved);
    pass_transpose(coeff, transpose);
    pass_transpose(transpose, residual);
    shz_xmtrx_load_4x4(&saved);
    for(unsigned i = 0; i < 64; ++i) {
        float value = residual[i] * 0.125f;
        float rounded = shz_truncf(value + (value < 0.0f ? -0.5f : 0.5f));
        residual[i] = rounded == 0.0f ? 0.0f : rounded;
    }
}
