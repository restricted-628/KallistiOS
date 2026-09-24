/* KallistiOS ##version##
   Host-side UV mapping for Compact asset compilation.
   Copyright (C) 2026 Joseph Black
*/
#ifndef PVR_UV_IR_H
#define PVR_UV_IR_H

#include <stdint.h>
#include <stddef.h>

/* Source attribute identity is separate from texture/resource identity.
   Double coefficients retain precision until final binary32 UV emission.
   This is host compiler data, not a runtime or serialized model layout. */
typedef struct pvr_uv_ir_transform {
    uint32_t source_set;
    double row[2][3];
} pvr_uv_ir_transform_t;

enum {
    PVR_UV_IR_SHARED = 0,
    PVR_UV_IR_INDEPENDENT = 1
};

/* Build offset + rotation * scale, followed by optional v = 1-v.
   Negative and zero scale are valid. flip_v must be 0 or 1. Output is
   unchanged on invalid arguments (EINVAL) or nonfinite inputs (EDOM). */
int pvr_uv_ir_transform_init(pvr_uv_ir_transform_t *output,
                             uint32_t source_set, const float offset[2],
                             const float scale[2], float rotation, int flip_v);

/* Apply once in host precision, then round to the emitted float coordinates.
   In-place UV use is allowed. Invalid/nonfinite input or unrepresentable
   output preserves both coordinates (EINVAL/EDOM/ERANGE respectively). */
int pvr_uv_ir_apply(const pvr_uv_ir_transform_t *transform,
                     const float source[2], float output[2]);

/* Propose auxiliary * inverse(base) for coordinates already baked by base.
   Returns SHARED only for the same source set and a numerically usable base.
   Different sets, singular/ill-conditioned bases or an unrepresentable float
   mapping return INDEPENDENT: preserve auxiliary per-corner UVs instead.
   Invalid input returns -1. Every nonzero return preserves output.

   SHARED is an algebraic candidate, NOT a lossless quantization guarantee.
   The compiler must compare it against independently evaluated auxiliary UVs
   at every actual decoded/quantized base corner before choosing shared storage.
   Never infer a relationship between unrelated UV sets from their ordinal or
   from one triangle. This helper does not enable auxiliary material import. */
int pvr_uv_ir_relative(const pvr_uv_ir_transform_t *base,
                        const pvr_uv_ir_transform_t *auxiliary,
                        float output[2][3]);

/* One final source-reference corner, AFTER strip joining/order and base UV
   encoding/decoding. auxiliary contains the authored, untransformed UV from
   auxiliary.source_set for that same corner, not a canonical vertex lookup.
   Preserve seams and repeated references in this array. */
typedef struct pvr_uv_ir_sample {
    float canonical[2];
    float auxiliary[2];
} pvr_uv_ir_sample_t;

typedef struct pvr_uv_ir_selection {
    int storage; /* PVR_UV_IR_SHARED or PVR_UV_IR_INDEPENDENT. */
    float mapping[2][3]; /* Relative map if shared, identity if independent. */
    /* Largest component error of the binary32 relative-map probe, in UV
       units. INFINITY means no usable candidate or arithmetic overflow.
       This is not the error of the independent fallback. */
    double candidate_error;
} pvr_uv_ir_selection_t;

/* Decide storage against EVERY supplied decoded corner, not just the affine
   matrices. absolute_error is a finite, nonnegative per-component UV budget
   chosen by the caller; zero allows only equal probe values. Distinct source
   sets never share even if their coordinates happen to coincide.

   Shared maps are probed with separate binary32 multiply/add steps (no FMA).
   This is a host preparation check, not a guarantee about clipping, sampling
   or alternate target FP modes. An importer requiring independent rounding
   behavior can always choose independent storage instead.

   For INDEPENDENT, bake each authored pair with pvr_uv_ir_apply(auxiliary)
   into PUV1 and use the returned identity rows in PML1. There is no implicit
   reapplication of the authored transform. Inputs must remain immutable for
   the call. Zero count, bad pointers/budget or output/input overlap: EINVAL;
   size overflow: EOVERFLOW; nonfinite input: EDOM; unrepresentable authored
   output: ERANGE. Invalid late corners fail even after sharing was ruled out.
   Failure preserves output. No allocation or serialized format changes. */
int pvr_uv_ir_select(const pvr_uv_ir_transform_t *base,
                     const pvr_uv_ir_transform_t *auxiliary,
                     const pvr_uv_ir_sample_t *samples, size_t count,
                     double absolute_error, pvr_uv_ir_selection_t *output);

#endif /* PVR_UV_IR_H */
