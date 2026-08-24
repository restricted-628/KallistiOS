/* KallistiOS ##version##

   dc/pvr_deform.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_deform.h
    \brief   Bounded, allocation-free geometry deformation kernels.
    \ingroup pvr_deform
*/

#ifndef __DC_PVR_DEFORM_H
#define __DC_PVR_DEFORM_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_lighting.h>

/** \defgroup pvr_deform PVR geometry deformation
    \brief                      Checked morphing and skinning kernels
    \ingroup                    pvr_geometry
    @{ */

/** \brief Canonical object-space or deformed position and normal. */
typedef struct pvr_deform_vertex {
    point_t position;
    vector_t normal;
} pvr_deform_vertex_t;

/** \brief Strided vertex source whose first member is pvr_deform_vertex_t. */
typedef struct pvr_deform_stream {
    const void *vertices;
    size_t vertex_count;
    size_t stride;
} pvr_deform_stream_t;

/** \brief Additive position and normal delta for one morph target. */
typedef struct pvr_morph_delta {
    vector_t position;
    vector_t normal;
} pvr_morph_delta_t;

/** \brief One caller-owned morph target and its finite blend weight. */
typedef struct pvr_morph_target {
    const void *deltas;
    size_t stride;
    float weight;
} pvr_morph_target_t;

/** \brief Four indexed joint influences for one source vertex.

    Joint indices with zero weight are ignored. Active weights must be finite
    and nonnegative; the kernel normalizes their sum.
*/
typedef struct pvr_skin_influences {
    uint16_t joint[4];
    float weight[4];
} pvr_skin_influences_t;

/** \brief Strided influence source. */
typedef struct pvr_skin_stream {
    const void *influences;
    size_t vertex_count;
    size_t stride;
} pvr_skin_stream_t;

/** \brief Caller-owned position and normal joint matrices. */
typedef struct pvr_skin_palette {
    const matrix_t *position_matrices;
    const pvr_normal_matrix_t *normal_matrices;
    size_t joint_count;
} pvr_skin_palette_t;

/** \brief Valid prefix reported by a deformation operation. */
typedef struct pvr_deform_result {
    size_t deformed_vertices;
} pvr_deform_result_t;

/** \brief Blend additive morph targets over a bounded base stream.

    Base and delta positions/normals must be finite. Result normals are
    normalized and output W values are set to one for positions and zero for
    normals. Exact canonical in-place base deformation is supported; all other
    overlap with output is rejected. Target structure and address ranges are
    preflighted before publication. Per-vertex arithmetic failure leaves the
    reported output prefix valid.
*/
int pvr_morph_apply(pvr_deform_vertex_t *output, size_t output_capacity,
                    const pvr_deform_stream_t *base,
                    const pvr_morph_target_t *targets, size_t target_count,
                    pvr_deform_result_t *result);

/** \brief Apply indexed linear-blend skinning to positions and normals.

    Vertex and influence counts must match. Every palette matrix, active
    weight, and active joint index is checked before the first output write.
    Position matrices transform points; inverse-transpose normal matrices
    transform normals. Accumulated normals are normalized. Exact canonical
    in-place vertex deformation is supported; influence and palette storage
    must not overlap output.
*/
int pvr_skin_apply(pvr_deform_vertex_t *output, size_t output_capacity,
                   const pvr_deform_stream_t *vertices,
                   const pvr_skin_stream_t *influences,
                   const pvr_skin_palette_t *palette,
                   pvr_deform_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_DEFORM_H */
