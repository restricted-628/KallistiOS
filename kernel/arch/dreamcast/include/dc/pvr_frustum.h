/* KallistiOS ##version##

   dc/pvr_frustum.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_frustum.h
    \brief   Caller-owned visibility and clipping for PVR geometry.
    \ingroup pvr_geometry
*/

#ifndef __DC_PVR_FRUSTUM_H
#define __DC_PVR_FRUSTUM_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>

#include <dc/matrix.h>
#include <dc/pvr.h>

/** \addtogroup pvr_geometry
    @{
*/

/** Maximum PVR vertices produced by clipping and triangulating one triangle. */
#define PVR_FRUSTUM_CLIP_MAX_VERTICES 21u

/** \brief Caller-owned screen-space frustum.

    The matrix maps object positions into the same homogeneous screen space
    consumed by pvr_geometry_project(). Bounds apply before division by W.
    W normally increases with camera distance under the established KOS
    perspective transform, so w_near and w_far provide explicit near and far
    clipping without relying on an unrelated transformed Z convention.
*/
typedef struct pvr_frustum {
    matrix_t object_to_screen; /**< Complete object-to-screen transform. */
    float left;                /**< Inclusive left screen coordinate. */
    float top;                 /**< Inclusive top screen coordinate. */
    float right;               /**< Inclusive right screen coordinate. */
    float bottom;              /**< Inclusive bottom screen coordinate. */
    float w_near;              /**< Inclusive nearest accepted W. */
    float w_far;               /**< Inclusive farthest accepted W. */
} pvr_frustum_t;

/** \brief Visibility relationship between a bound and a frustum. */
typedef enum pvr_frustum_classification {
    PVR_FRUSTUM_OUTSIDE = 0,   /**< Entire bound lies outside one plane. */
    PVR_FRUSTUM_INTERSECT,     /**< Bound crosses at least one plane. */
    PVR_FRUSTUM_INSIDE         /**< Entire bound lies inside every plane. */
} pvr_frustum_classification_t;

/** \brief Attributes interpolated at newly clipped triangle edges. */
typedef enum pvr_frustum_clip_attribute {
    PVR_FRUSTUM_CLIP_UV = 1u << 0,    /**< Interpolate floating U and V. */
    PVR_FRUSTUM_CLIP_ARGB = 1u << 1,  /**< Interpolate packed base color. */
    PVR_FRUSTUM_CLIP_OARGB = 1u << 2, /**< Interpolate packed offset color. */
    PVR_FRUSTUM_CLIP_ALL = PVR_FRUSTUM_CLIP_UV |
                            PVR_FRUSTUM_CLIP_ARGB |
                            PVR_FRUSTUM_CLIP_OARGB
} pvr_frustum_clip_attribute_t;

/** \brief Progress from clipping one input triangle. */
typedef struct pvr_frustum_clip_result {
    size_t polygon_vertices;  /**< Vertices in the clipped convex polygon. */
    size_t output_vertices;   /**< Vertices after triangle-fan expansion. */
} pvr_frustum_clip_result_t;

/** \brief Initialize a checked screen-space frustum.

    Output is unchanged on failure. All values and matrix elements must be
    finite; left must be less than right, top less than bottom, and W bounds
    must be positive and increasing. No global matrix state is read or changed.

    \retval 0  Success.
    \retval -1 Invalid or non-finite input, with errno set to EINVAL or EDOM.
*/
int pvr_frustum_init(pvr_frustum_t *frustum, const matrix_t *object_to_screen,
                     float left, float top, float right, float bottom,
                     float w_near, float w_far);

/** \brief Classify an object-space axis-aligned bounding box.

    The eight corners are transformed and tested against all six homogeneous
    frustum planes. Output is unchanged on failure.

    \param frustum     Initialized frustum.
    \param minimum     Minimum XYZ corner; W is ignored.
    \param maximum     Maximum XYZ corner; W is ignored.
    \param result      Classification destination.

    \retval 0  Classification produced.
    \retval -1 Invalid input or arithmetic overflow, with errno set to EINVAL,
               EDOM, or ERANGE.
*/
int pvr_frustum_classify_aabb(const pvr_frustum_t *frustum,
                              const point_t *minimum, const point_t *maximum,
                              pvr_frustum_classification_t *result);

/** \brief Clip, project, and triangulate one canonical PVR triangle.

    Input contains exactly three vertices. Generated intersections linearly
    interpolate the attributes selected by \p attributes; unselected fields
    are copied from the inside edge endpoint and are intended only for fields
    ignored by the compiled header. Output triangles use independent
    VERTEX/VERTEX/VERTEX_EOL command triplets.

    The operation stages its complete result before touching output, so
    insufficient capacity or malformed input leaves output unchanged. Exact
    and shifted overlap with the three input vertices are safe. Output must be
    32-byte aligned. At most PVR_FRUSTUM_CLIP_MAX_VERTICES are produced.

    \retval 0  Success, including a completely clipped triangle with zero
               output vertices.
    \retval -1 Error, with errno set to EINVAL, EILSEQ, ENOSPC, EDOM, or
               ERANGE.
*/
int pvr_frustum_clip_triangle(pvr_vertex_t *output, size_t output_capacity,
                              const pvr_vertex_t input[3],
                              const pvr_frustum_t *frustum,
                              uint32_t attributes,
                              pvr_frustum_clip_result_t *result);

/** @} */

__END_DECLS

#endif /* __DC_PVR_FRUSTUM_H */
