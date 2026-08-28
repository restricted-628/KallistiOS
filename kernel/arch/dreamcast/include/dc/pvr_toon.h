/* KallistiOS ##version##

   dc/pvr_toon.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_toon.h
    \brief   Allocation-free scalar-band shading geometry.
    \ingroup pvr_geometry
*/

#ifndef __DC_PVR_TOON_H
#define __DC_PVR_TOON_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/matrix.h>

/** \addtogroup pvr_geometry
    @{
*/

/** Source index assigned to a vertex created at a band boundary. */
#define PVR_TOON_GENERATED_INDEX UINT32_MAX

/** \brief Directional scalar equations supplied by the small standard helper.

    The geometric partitioner accepts an arbitrary scalar and is not limited
    to these equations. They cover conventional diffuse bands, an inverted
    signed profile, and a half-Lambert profile without introducing renderer or
    model ownership.
*/
typedef enum pvr_toon_shade_equation {
    PVR_TOON_SHADE_DOT = 0,       /**< ambient + intensity * N dot L. */
    PVR_TOON_SHADE_INVERTED_DOT,  /**< ambient - intensity * N dot L. */
    PVR_TOON_SHADE_HALF_LAMBERT   /**< ambient + intensity*(N dot L*.5+.5). */
} pvr_toon_shade_equation_t;

/** \brief Admitted normalized directional light for scalar shading. */
typedef struct pvr_toon_light {
    vector_t direction;           /**< Unit XYZ direction; W is zero. */
    float intensity;              /**< Finite signed strength. */
    float ambient;                /**< Finite scalar bias. */
} pvr_toon_light_t;

/** \brief Strided normalized-or-normalizable input normal stream. */
typedef struct pvr_toon_shade_stream {
    const void *normals;
    size_t normal_count;
    size_t stride;
} pvr_toon_shade_stream_t;

/** \brief Completed prefix from one scalar shade batch. */
typedef struct pvr_toon_shade_result {
    size_t shaded_normals;
} pvr_toon_shade_result_t;

/** \brief Canonical working vertex used only while partitioning geometry.

    This is not an asset or Tile Accelerator packet. Compact UV encodings have
    already become floating point before reaching it. Generated vertices
    preserve every field and set source_index to PVR_TOON_GENERATED_INDEX.
*/
typedef struct pvr_toon_vertex {
    point_t position;
    vector_t normal;
    float u;
    float v;
    float shade;
    uint32_t argb;
    uint32_t oargb;
    uint32_t source_index;
} pvr_toon_vertex_t;

/** \brief One independent output triangle and its selected band. */
typedef struct pvr_toon_triangle {
    pvr_toon_vertex_t vertices[3];
    size_t band;
} pvr_toon_triangle_t;

/** \brief Result from partitioning one triangle. */
typedef struct pvr_toon_split_result {
    size_t output_triangles;      /**< Required or produced triangle count. */
    size_t generated_vertices;    /**< Generated output vertex instances. */
    size_t crossed_thresholds;    /**< Thresholds strictly inside shade span. */
} pvr_toon_split_result_t;

/** \brief Initialize and normalize one directional scalar light.

    Output is unchanged on failure. Direction W is ignored. Signed intensity
    permits lightening or darkening profiles.
*/
int pvr_toon_light_init(pvr_toon_light_t *light,
                        const vector_t *direction,
                        float intensity, float ambient);

/** \brief Evaluate one normalized normal through a standard scalar equation.

    The returned scalar is intentionally not clamped: arbitrary ordered band
    thresholds can consume negative values and values above one.
*/
int pvr_toon_shade_evaluate(float *shade, const vector_t *normal,
                            const pvr_toon_light_t *light,
                            pvr_toon_shade_equation_t equation);

/** \brief Evaluate a checked strided normal batch into contiguous scalars.

    Light admission is amortized across the stream. Each input normal is
    normalized before the selected equation is evaluated. The output and
    complete strided input span must not overlap.
*/
int pvr_toon_shade_apply(float *output, size_t output_capacity,
                         const pvr_toon_shade_stream_t *stream,
                         const pvr_toon_light_t *light,
                         pvr_toon_shade_equation_t equation,
                         pvr_toon_shade_result_t *result);

/** \brief Return the band containing one finite scalar.

    Thresholds must be finite and strictly increasing. A scalar equal to a
    threshold belongs to the higher band. With N thresholds the result is in
    `[0, N]`.
*/
int pvr_toon_band_index(size_t *band, float shade,
                        const float *thresholds, size_t threshold_count);

/** \brief Query the worst-case triangle capacity for N thresholds.

    Each threshold can add at most two independent triangles, so the exact
    conservative bound is `2*N + 1`. Output is unchanged on overflow.
*/
int pvr_toon_triangle_capacity(size_t threshold_count, size_t *capacity);

/** \brief Partition one attributed triangle into scalar bands.

    Each band is produced by clipping the input triangle against its lower and
    upper scalar boundaries, then expanding the resulting convex polygon into
    independent triangles. Position, normalized normal, UV, packed base and
    offset color, and shade are interpolated. Exactly cancelling endpoint
    normals select the nearer endpoint normal with a deterministic low-shade
    tie break. Boundary shade is assigned the threshold exactly. Edge
    interpolation orders endpoints by shade, making a shared edge
    deterministic even when adjacent triangles traverse it in opposite
    directions.

    Vertices within \p epsilon of a threshold are snapped to that threshold
    before partitioning. Threshold gaps must exceed twice epsilon so one input
    cannot be ambiguous between neighboring bands. Input positions require
    W=1 and normals require W=0. Output is unchanged on validation failure or
    ENOSPC. On ENOSPC, result::output_triangles reports required capacity.

    No allocation, global state, scene ownership, or PVR submission occurs.
*/
int pvr_toon_split_triangle(
    pvr_toon_triangle_t *output, size_t output_capacity,
    const pvr_toon_vertex_t input[3],
    const float *thresholds, size_t threshold_count, float epsilon,
    pvr_toon_split_result_t *result);

/** \brief Multiply two packed ARGB colors with rounded channel products. */
int pvr_toon_color_modulate(uint32_t *output, uint32_t base,
                            uint32_t modulation);

/** @} */

__END_DECLS

#endif /* __DC_PVR_TOON_H */
