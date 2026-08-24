/* KallistiOS ##version##

   dc/pvr_lighting.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_lighting.h
    \brief   Checked, allocation-free PVR vertex-lighting kernels.
    \ingroup pvr_lighting

    This interface transforms normals and evaluates basic CPU vertex lighting.
    It owns no lights, models, materials, scenes, or output storage.
*/

#ifndef __DC_PVR_LIGHTING_H
#define __DC_PVR_LIGHTING_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/vector.h>

/** \defgroup pvr_lighting PVR vertex lighting
    \brief                       Checked CPU lighting preparation
    \ingroup                     pvr_geometry
    @{ */

/** \brief Inverse-transpose upper 3x3 matrix for transforming normals. */
typedef struct pvr_normal_matrix {
    float column[3][3];
} pvr_normal_matrix_t;

/** \brief Strided input view whose first member is a vector_t normal. */
typedef struct pvr_normal_stream {
    const void *normals;
    size_t normal_count;
    size_t stride;
} pvr_normal_stream_t;

/** \brief Progress from a checked normal transformation. */
typedef struct pvr_normal_result {
    size_t transformed_normals;
} pvr_normal_result_t;

/** \brief Supported CPU light families. */
typedef enum pvr_light_kind {
    PVR_LIGHT_DIRECTIONAL = 0,
    PVR_LIGHT_POINT
} pvr_light_kind_t;

/** \brief One directional or point light.

    Directional-light vector points from the shaded surface toward the light.
    Point lights use position and the attenuation equation
    `1 / (constant + linear * distance + quadratic * distance^2)`. A zero
    range is unbounded. RGB color and intensity are linear, nonnegative values.
*/
typedef struct pvr_light {
    pvr_light_kind_t kind;
    union {
        vector_t direction;
        point_t position;
    } source;
    vector_t color;
    float intensity;
    float attenuation_constant;
    float attenuation_linear;
    float attenuation_quadratic;
    float range;
} pvr_light_t;

/** \brief One world-space input sample for CPU vertex lighting.

    Color components are ordered red, green, blue, alpha and are clamped to
    `[0, 1]` during packing. The normal must already be unit length within the
    tolerance documented by pvr_lighting_apply().
*/
typedef struct pvr_lighting_sample {
    point_t position;
    vector_t normal;
    float color[4];
} pvr_lighting_sample_t;

/** \brief Strided view whose first member is a pvr_lighting_sample_t. */
typedef struct pvr_lighting_stream {
    const void *samples;
    size_t sample_count;
    size_t stride;
} pvr_lighting_stream_t;

/** \brief Caller-owned lighting description. */
typedef struct pvr_lighting_context {
    float ambient[3];
    const pvr_light_t *lights;
    size_t light_count;
} pvr_lighting_context_t;

/** \brief Progress from a checked lighting pass. */
typedef struct pvr_lighting_result {
    size_t shaded_samples;
} pvr_lighting_result_t;

/** \brief Build a checked inverse-transpose normal matrix.

    The upper 3x3 of \p object_to_world may contain rotation and nonuniform
    scale, but must be finite and nonsingular. Translation is ignored. Failure
    leaves \p output unchanged.

    \retval 0  Success.
    \retval -1 Invalid, non-finite, or singular input, with errno set to
               EINVAL, EDOM, or ERANGE.
*/
int pvr_normal_matrix_build(pvr_normal_matrix_t *output,
                            const matrix_t *object_to_world);

/** \brief Transform and normalize a bounded stream of normals.

    Each output vector receives XYZ and W=0. Exact in-place operation is
    supported when input stride is sizeof(vector_t); other overlap is rejected.
    On failure, `[0, result->transformed_normals)` remains a valid output
    prefix.
*/
int pvr_normal_transform(vector_t *output, size_t output_capacity,
                         const pvr_normal_stream_t *stream,
                         const pvr_normal_matrix_t *matrix,
                         pvr_normal_result_t *result);

/** \brief Saturate and pack linear RGBA components as `0xAARRGGBB`.

    Finite values are clamped to `[0, 1]` and rounded to the nearest 8-bit
    integer. Failure leaves \p output unchanged.
*/
int pvr_color_pack_argb(uint32_t *output, float alpha, float red,
                        float green, float blue);

/** \brief Apply ambient and diffuse Lambert lighting to a sample stream.

    The complete context is validated before the first output write. Surface
    normals must be finite, nonzero, and unit length within 0.1 percent.
    Lighting is accumulated in linear RGB, multiplied by the sample color,
    saturated, and emitted as `0xAARRGGBB`; sample alpha is not lit. Input and
    output ranges must not overlap. On a sample failure, the result identifies
    the valid packed-color prefix.
*/
int pvr_lighting_apply(uint32_t *output, size_t output_capacity,
                       const pvr_lighting_stream_t *stream,
                       const pvr_lighting_context_t *context,
                       pvr_lighting_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_LIGHTING_H */
