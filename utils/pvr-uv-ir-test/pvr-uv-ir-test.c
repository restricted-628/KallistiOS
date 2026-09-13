/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include "pvr-uv-ir.h"
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void close_value(float value, double expected) {
    assert(fabs((double)value - expected) <= 2e-5 * fmax(1.0, fabs(expected)));
}

static pvr_uv_ir_transform_t identity(uint32_t set) {
    pvr_uv_ir_transform_t result;
    const float offset[2] = {0, 0}, scale[2] = {1, 1};
    assert(pvr_uv_ir_transform_init(&result, set, offset, scale, 0, 0) == 0);
    return result;
}

static void forward(void) {
    const float offset[2] = {1, -1}, scale[2] = {2, 3};
    const float uv[2] = {.25f, .5f};
    float output[2];
    pvr_uv_ir_transform_t transform;
    assert(pvr_uv_ir_transform_init(&transform, 7, offset, scale,
                                    1.5707963267948966f, 0) == 0);
    assert(transform.source_set == 7);
    assert(pvr_uv_ir_apply(&transform, uv, output) == 0);
    close_value(output[0], -.5);
    close_value(output[1], -.5);
    assert(pvr_uv_ir_transform_init(&transform, 7, offset, scale,
                                    1.5707963267948966f, 1) == 0);
    assert(pvr_uv_ir_apply(&transform, uv, output) == 0);
    close_value(output[0], -.5);
    close_value(output[1], 1.5);
    transform = identity(0);
    output[0] = -123.25f;
    output[1] = 74.5f;
    assert(pvr_uv_ir_apply(&transform, output, output) == 0);
    assert(output[0] == -123.25f && output[1] == 74.5f);
}

static void relative(void) {
    pvr_uv_ir_transform_t base = identity(2), auxiliary = identity(2);
    float mapping[2][3];
    /* Independently solved: base=(2u+1, 3v-2), aux=(-4u+5, .5v+6). */
    base.row[0][0] = 2;
    base.row[0][2] = 1;
    base.row[1][1] = 3;
    base.row[1][2] = -2;
    auxiliary.row[0][0] = -4;
    auxiliary.row[0][2] = 5;
    auxiliary.row[1][1] = .5;
    auxiliary.row[1][2] = 6;
    assert(pvr_uv_ir_relative(&base, &auxiliary, mapping) == PVR_UV_IR_SHARED);
    assert(mapping[0][0] == -2 && mapping[0][1] == 0 && mapping[0][2] == 7);
    assert(mapping[1][0] == 0);
    close_value(mapping[1][1], 1.0 / 6.0);
    close_value(mapping[1][2], 19.0 / 3.0);

    /* Rotated, reflected, nonuniform maps and every independent V-flip pair.
       The oracle evaluates authored operations, not the relative compiler. */
    for(unsigned variant = 0; variant < 32; ++variant) {
        float bo[2] = {.75f, -.125f}, ao[2] = {-1.25f, 2};
        float bs[2] = {-2, 3}, as[2] = {.5f, -1.5f};
        float br = (float)variant * .071f, ar = -.27f;
        int bf = (variant & 1) != 0, af = (variant & 2) != 0;
        assert(pvr_uv_ir_transform_init(&base, 3, bo, bs, br, bf) == 0);
        assert(pvr_uv_ir_transform_init(&auxiliary, 3, ao, as, ar, af) == 0);
        assert(pvr_uv_ir_relative(&base, &auxiliary, mapping) == PVR_UV_IR_SHARED);
        for(int corner = -16; corner <= 16; ++corner) {
            float uv[2] = {corner * .125f, corner * corner * .015625f};
            float canonical[2];
            assert(pvr_uv_ir_apply(&base, uv, canonical) == 0);
            double x = as[0] * uv[0], y = as[1] * uv[1];
            double au = ao[0] + cos(ar) * x - sin(ar) * y;
            double av = ao[1] + sin(ar) * x + cos(ar) * y;
            if(af)
                av = 1 - av;
            close_value(mapping[0][0] * canonical[0] +
                        mapping[0][1] * canonical[1] + mapping[0][2], au);
            close_value(mapping[1][0] * canonical[0] +
                        mapping[1][1] * canonical[1] + mapping[1][2], av);
        }
    }
}

static void independent_and_invalid(void) {
    pvr_uv_ir_transform_t base = identity(0), auxiliary = identity(1);
    float output[2][3], before[2][3];
    memset(output, 0x5a, sizeof(output));
    memcpy(before, output, sizeof(before));
    assert(pvr_uv_ir_relative(&base, &auxiliary, output) == PVR_UV_IR_INDEPENDENT);
    assert(!memcmp(output, before, sizeof(output)));
    auxiliary.source_set = 0;
    base.row[0][0] = 0; /* Collapsed mapping still valid for forward baking. */
    float uv[2] = {5, 7}, value[2];
    assert(pvr_uv_ir_apply(&base, uv, value) == 0 && value[0] == 0);
    assert(pvr_uv_ir_relative(&base, &auxiliary, output) == PVR_UV_IR_INDEPENDENT);
    assert(!memcmp(output, before, sizeof(output)));
    base.row[0][0] = 1e-12; /* Poor conditioning, not an exact singularity. */
    assert(pvr_uv_ir_relative(&base, &auxiliary, output) == PVR_UV_IR_INDEPENDENT);
    assert(!memcmp(output, before, sizeof(output)));
    base = identity(0);
    auxiliary.row[0][0] = (double)FLT_MAX * 2;
    assert(pvr_uv_ir_relative(&base, &auxiliary, output) == PVR_UV_IR_INDEPENDENT);
    assert(!memcmp(output, before, sizeof(output)));
    auxiliary.row[0][0] = NAN;
    assert(pvr_uv_ir_relative(&base, &auxiliary, output) == -1 && errno == EDOM);
    assert(!memcmp(output, before, sizeof(output)));
    assert(pvr_uv_ir_relative(NULL, &base, output) == -1 && errno == EINVAL);
    assert(!memcmp(output, before, sizeof(output)));

    value[0] = 11;
    value[1] = 12;
    base.row[1][1] = FLT_MAX;
    assert(pvr_uv_ir_apply(&base, uv, value) == -1 && errno == ERANGE);
    assert(value[0] == 11 && value[1] == 12); /* No partial first row. */
    base = identity(0);
    uv[0] = INFINITY;
    assert(pvr_uv_ir_apply(&base, uv, value) == -1 && errno == EDOM);
    assert(value[0] == 11 && value[1] == 12);
    pvr_uv_ir_transform_t saved;
    memcpy(&saved, &base, sizeof(saved));
    float offset[2] = {0, 0}, scale[2] = {1, 1};
    assert(pvr_uv_ir_transform_init(&base, 0, offset, scale, NAN, 0) == -1);
    assert(errno == EDOM && !memcmp(&base, &saved, sizeof(base)));
    assert(pvr_uv_ir_transform_init(&base, 0, offset, scale, 0, 2) == -1);
    assert(errno == EINVAL && !memcmp(&base, &saved, sizeof(base)));
}

static void quantization_is_not_equivalence(void) {
    pvr_uv_ir_transform_t base = identity(0), auxiliary = identity(0);
    float mapping[2][3], canonical[2], authored[2];
    const float uv[2] = {.001f, .25f};
    base.row[0][0] = base.row[1][1] = .001;
    assert(pvr_uv_ir_relative(&base, &auxiliary, mapping) == PVR_UV_IR_SHARED);
    assert(pvr_uv_ir_apply(&base, uv, canonical) == 0);
    assert(pvr_uv_ir_apply(&auxiliary, uv, authored) == 0);
    /* Both base coordinates round to zero in signed UV10. Inverting that
       stored pair cannot recover the auxiliary attributes. A future importer
       must reject this reuse against its corner-error budget. */
    canonical[0] = roundf(canonical[0] * 1024) / 1024;
    canonical[1] = roundf(canonical[1] * 1024) / 1024;
    float v = mapping[1][0] * canonical[0] +
              mapping[1][1] * canonical[1] + mapping[1][2];
    assert(fabsf(v - authored[1]) > .2f);
}

int main(void) {
    forward();
    relative();
    independent_and_invalid();
    quantization_is_not_equivalence();
    puts("pvr-uv-ir-test: PASS");
    return 0;
}
