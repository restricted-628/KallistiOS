/* KallistiOS ##version##
   Host-side UV mapping for Compact asset compilation.
   Copyright (C) 2026 Joseph Black
*/
#include "pvr-uv-ir.h"
#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>

static int valid(const pvr_uv_ir_transform_t *transform) {
    if(!transform) {
        errno = EINVAL;
        return 0;
    }
    for(unsigned r = 0; r < 2; ++r) {
        for(unsigned c = 0; c < 3; ++c) {
            if(!isfinite(transform->row[r][c])) {
                errno = EDOM;
                return 0;
            }
        }
    }
    return 1;
}

int pvr_uv_ir_transform_init(pvr_uv_ir_transform_t *output,
                             uint32_t source_set, const float offset[2],
                             const float scale[2], float rotation, int flip_v) {
    if(!output || !offset || !scale || (flip_v != 0 && flip_v != 1)) {
        errno = EINVAL;
        return -1;
    }
    if(!isfinite(rotation) || !isfinite(offset[0]) || !isfinite(offset[1]) ||
       !isfinite(scale[0]) || !isfinite(scale[1])) {
        errno = EDOM;
        return -1;
    }
    double cosine = cos((double)rotation), sine = sin((double)rotation);
    pvr_uv_ir_transform_t candidate = {
        .source_set = source_set,
        .row = {{cosine * scale[0], -sine * scale[1], offset[0]},
                {sine * scale[0], cosine * scale[1], offset[1]}}
    };
    if(flip_v) {
        candidate.row[1][0] = -candidate.row[1][0];
        candidate.row[1][1] = -candidate.row[1][1];
        candidate.row[1][2] = 1.0 - candidate.row[1][2];
    }
    *output = candidate;
    return 0;
}

int pvr_uv_ir_apply(const pvr_uv_ir_transform_t *transform,
                     const float source[2], float output[2]) {
    float candidate[2];
    if(!source || !output) {
        errno = EINVAL;
        return -1;
    }
    if(!valid(transform))
        return -1;
    if(!isfinite(source[0]) || !isfinite(source[1])) {
        errno = EDOM;
        return -1;
    }
    for(unsigned r = 0; r < 2; ++r) {
        double value = transform->row[r][0] * source[0] +
                       transform->row[r][1] * source[1] + transform->row[r][2];
        if(!isfinite(value) || fabs(value) > FLT_MAX) {
            errno = ERANGE;
            return -1;
        }
        candidate[r] = (float)value;
    }
    memcpy(output, candidate, sizeof(candidate));
    return 0;
}

int pvr_uv_ir_relative(const pvr_uv_ir_transform_t *base,
                        const pvr_uv_ir_transform_t *auxiliary,
                        float output[2][3]) {
    float candidate[2][3];
    if(!output) {
        errno = EINVAL;
        return -1;
    }
    if(!valid(base) || !valid(auxiliary))
        return -1;
    if(base->source_set != auxiliary->source_set)
        return PVR_UV_IR_INDEPENDENT;

    double a = base->row[0][0], b = base->row[0][1];
    double c = base->row[1][0], d = base->row[1][1];
    double determinant = a * d - b * c;
    if(determinant == 0.0 || !isfinite(determinant))
        return PVR_UV_IR_INDEPENDENT;
    double norm = fmax(fabs(a) + fabs(b), fabs(c) + fabs(d));
    double adj_norm = fmax(fabs(d) + fabs(b), fabs(c) + fabs(a));
    /* Do not amplify a collapsed or poorly conditioned canonical map into
       enormous float coefficients. This is a conservative preparation gate,
       not an error budget: final stored corner values still need comparison. */
    double condition = norm * adj_norm / fabs(determinant);
    if(!isfinite(condition) ||
       condition > 1.0 / (32.0 * FLT_EPSILON))
        return PVR_UV_IR_INDEPENDENT;

    for(unsigned r = 0; r < 2; ++r) {
        double x = (auxiliary->row[r][0] * d - auxiliary->row[r][1] * c) /
                   determinant;
        double y = (auxiliary->row[r][1] * a - auxiliary->row[r][0] * b) /
                   determinant;
        double z = auxiliary->row[r][2] - x * base->row[0][2] -
                   y * base->row[1][2];
        if(!isfinite(x) || !isfinite(y) || !isfinite(z) ||
           fabs(x) > FLT_MAX || fabs(y) > FLT_MAX || fabs(z) > FLT_MAX)
            return PVR_UV_IR_INDEPENDENT;
        candidate[r][0] = (float)x;
        candidate[r][1] = (float)y;
        candidate[r][2] = (float)z;
    }
    memcpy(output, candidate, sizeof(candidate));
    return PVR_UV_IR_SHARED;
}

static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}

int pvr_uv_ir_select(const pvr_uv_ir_transform_t *base,
                     const pvr_uv_ir_transform_t *auxiliary,
                     const pvr_uv_ir_sample_t *samples, size_t count,
                     double absolute_error, pvr_uv_ir_selection_t *output) {
    pvr_uv_ir_selection_t selected = {
        .storage = PVR_UV_IR_INDEPENDENT,
        .mapping = {{1, 0, 0}, {0, 1, 0}},
        .candidate_error = INFINITY
    };
    float mapping[2][3];
    if(!base || !auxiliary || !samples || !count || !output ||
       !isfinite(absolute_error) || absolute_error < 0) {
        errno = EINVAL;
        return -1;
    }
    if(count > SIZE_MAX / sizeof(*samples)) {
        errno = EOVERFLOW;
        return -1;
    }
    if(overlaps(output, sizeof(*output), base, sizeof(*base)) ||
       overlaps(output, sizeof(*output), auxiliary, sizeof(*auxiliary)) ||
       overlaps(output, sizeof(*output), samples, count * sizeof(*samples))) {
        errno = EINVAL;
        return -1;
    }
    int candidate = pvr_uv_ir_relative(base, auxiliary, mapping);
    if(candidate < 0)
        return -1;
    if(candidate == PVR_UV_IR_SHARED)
        selected.candidate_error = 0;
    for(size_t i = 0; i < count; ++i) {
        float authored[2];
        if(!isfinite(samples[i].canonical[0]) ||
           !isfinite(samples[i].canonical[1])) {
            errno = EDOM;
            return -1;
        }
        if(pvr_uv_ir_apply(auxiliary, samples[i].auxiliary, authored) < 0)
            return -1;
        /* Do not early-out on an over-budget corner: later authored values
           must be safe to bake before selecting the independent path. */
        if(candidate != PVR_UV_IR_SHARED)
            continue;
        for(unsigned r = 0; r < 2; ++r) {
            /* Force binary32 steps even with host contraction/excess
               precision enabled. Double arithmetic would test a different
               expression from the ordinary target layer callback. */
            volatile float x = mapping[r][0] * samples[i].canonical[0];
            volatile float y = mapping[r][1] * samples[i].canonical[1];
            volatile float sum = x + y;
            volatile float value = sum + mapping[r][2];
            double error = isfinite(value) ?
                fabs((double)value - authored[r]) : INFINITY;
            selected.candidate_error = fmax(selected.candidate_error, error);
        }
    }
    if(selected.candidate_error <= absolute_error) {
        selected.storage = PVR_UV_IR_SHARED;
        memcpy(selected.mapping, mapping, sizeof(mapping));
    }
    *output = selected;
    return 0;
}
