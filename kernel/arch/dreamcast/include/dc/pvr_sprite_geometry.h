/* KallistiOS ##version##

   dc/pvr_sprite_geometry.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_sprite_geometry.h
    \brief   Checked, caller-owned textured sprite-cell geometry.
    \ingroup pvr_sprite_geometry

    This interface converts reusable atlas cells and strided instances into
    established pvr_sprite_txr_t packets. It owns no texture, material, scene,
    list, clock, animation, or instance storage.
*/

#ifndef __DC_PVR_SPRITE_GEOMETRY_H
#define __DC_PVR_SPRITE_GEOMETRY_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/matrix.h>
#include <dc/pvr_geometry.h>

/** \defgroup pvr_sprite_geometry Textured sprite geometry
    \brief                                Checked sprite-cell packet building
    \ingroup                              pvr_geometry
    @{ */

/** \brief Per-instance sprite-cell flags. */
typedef enum pvr_sprite_instance_flags {
    PVR_SPRITE_INSTANCE_NONE = 0,
    PVR_SPRITE_INSTANCE_FLIP_U = 1u << 0,
    PVR_SPRITE_INSTANCE_FLIP_V = 1u << 1,
    PVR_SPRITE_INSTANCE_HIDDEN = 1u << 2
} pvr_sprite_instance_flags_t;

/** \brief One reusable normalized texture region and local rectangle.

    Width and height are positive local units. Origin is expressed in
    normalized rectangle units: `(0, 0)` selects its top-left and
    `(0.5, 0.5)` its center. Any finite origin is accepted so applications may
    deliberately place a pivot outside the visible rectangle. Texture
    coordinates are an increasing normalized region in `[0, 1]`.
*/
typedef struct pvr_sprite_cell {
    float width;
    float height;
    float origin_x;
    float origin_y;
    float u0;
    float v0;
    float u1;
    float v1;
} pvr_sprite_cell_t;

/** \brief Bounded caller-owned sprite-cell table. */
typedef struct pvr_sprite_atlas {
    const pvr_sprite_cell_t *cells;
    size_t cell_count;
} pvr_sprite_atlas_t;

/** \brief One sprite instance referencing a cell-table index.

    Rotation is in radians. Scale must be finite and strictly positive. For 2D
    compilation, position is already in PVR screen/depth coordinates. For 3D
    compilation it is a world-space billboard origin.
*/
typedef struct pvr_sprite_instance {
    size_t cell_index;
    point_t position;
    float rotation;
    float scale_x;
    float scale_y;
    uint32_t flags;
} pvr_sprite_instance_t;

/** \brief Bounded strided sprite-instance source. */
typedef struct pvr_sprite_instance_stream {
    const void *instances;
    size_t instance_count;
    size_t stride;
} pvr_sprite_instance_stream_t;

/** \brief Shared world-space billboard axes for a 3D batch.

    Positive X moves toward the right side of a sprite and positive Y toward
    its bottom. The finite axes must be nonzero and nonparallel; they need not
    be unit length, permitting an explicit shared skew or scale.
*/
typedef struct pvr_sprite_billboard_basis {
    vector_t x_axis;
    vector_t y_axis;
} pvr_sprite_billboard_basis_t;

/** \brief Progress from sprite-cell compilation. */
typedef struct pvr_sprite_batch_result {
    size_t examined_instances;
    size_t produced_sprites;
} pvr_sprite_batch_result_t;

/** \brief Compile screen-space sprite cells into textured PVR packets.

    The complete atlas references, input range, visible count, flags, cells,
    and instance arithmetic are checked before the first output write. Hidden
    instances consume input but produce no packet. Output must be 32-byte
    aligned and may be emitted through a PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED
    sink after submitting a matching sprite material. Texture state and the
    uniform sprite color belong to that material because pvr_sprite_txr_t has
    no per-corner color fields.

    \retval 0  Every instance was consumed.
    \retval -1 Invalid input, insufficient capacity, or arithmetic failure.
*/
int pvr_sprite_batch_compile_2d(
    pvr_sprite_txr_t *output, size_t output_capacity,
    const pvr_sprite_atlas_t *atlas,
    const pvr_sprite_instance_stream_t *stream,
    pvr_sprite_batch_result_t *result);

/** \brief Compile and project camera-facing 3D sprite cells.

    Local cell geometry is expanded over the supplied billboard axes and then
    projected through pvr_geometry_project_vertices(). The planar fourth depth
    is inferred by the established textured-sprite format. A projection failure
    leaves `[0, result->produced_sprites)` valid; later output is undefined.

    \retval 0  Every visible instance was projected.
    \retval -1 Invalid input, insufficient capacity, or projection failure.
*/
int pvr_sprite_batch_compile_3d(
    pvr_sprite_txr_t *output, size_t output_capacity,
    const pvr_sprite_atlas_t *atlas,
    const pvr_sprite_instance_stream_t *stream,
    const pvr_sprite_billboard_basis_t *basis,
    const matrix_t *world_to_screen,
    pvr_sprite_batch_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_SPRITE_GEOMETRY_H */
