/* KallistiOS ##version##
   Host-side UV mapping for Compact asset compilation.
   Copyright (C) 2026 Joseph Black
*/
#ifndef PVR_UV_IR_H
#define PVR_UV_IR_H

#include <stdint.h>

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

#endif /* PVR_UV_IR_H */
