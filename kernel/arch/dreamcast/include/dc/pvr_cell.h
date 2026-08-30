/* KallistiOS ##version##

   dc/pvr_cell.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_cell.h
    \brief   Checked, caller-owned cell-sprite animation and composition.
    \ingroup pvr_cell

    This interface samples timestamped changes into caller-owned cell state,
    composes those cells under one sprite transform, and feeds the established
    sprite-geometry compiler. It owns no clock, texture, material, animation,
    output, or workspace storage and creates no threads.
*/

#ifndef __DC_PVR_CELL_H
#define __DC_PVR_CELL_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/animation.h>
#include <dc/pvr_sprite_geometry.h>

/** \defgroup pvr_cell Cell-sprite animation
    \brief                   Allocation-free cell composition and streams
    \ingroup                 pvr_sprite_geometry
    @{ */

/** \brief Per-cell visibility and texture-coordinate flags. */
typedef enum pvr_cell_flags {
    PVR_CELL_NONE = 0,
    PVR_CELL_FLIP_U = PVR_SPRITE_INSTANCE_FLIP_U,
    PVR_CELL_FLIP_V = PVR_SPRITE_INSTANCE_FLIP_V,
    PVR_CELL_HIDDEN = PVR_SPRITE_INSTANCE_HIDDEN
} pvr_cell_flags_t;

/** \brief Fields replaced by one timestamped cell key. */
typedef enum pvr_cell_key_fields {
    PVR_CELL_KEY_ATLAS_CELL = 1u << 0,
    PVR_CELL_KEY_OFFSET = 1u << 1,
    PVR_CELL_KEY_ROTATION = 1u << 2,
    PVR_CELL_KEY_SCALE = 1u << 3,
    PVR_CELL_KEY_PRIORITY = 1u << 4,
    PVR_CELL_KEY_FLAGS = 1u << 5,
    PVR_CELL_KEY_MATERIAL = 1u << 6,
    PVR_CELL_KEY_DIFFUSE = 1u << 7,
    PVR_CELL_KEY_SPECULAR = 1u << 8,
    PVR_CELL_KEY_ALL = (1u << 9) - 1u
} pvr_cell_key_fields_t;

/** \brief Complete state of one cell slot.

    Offset is local to the containing cell sprite; W is ignored. Priority is
    retained as a signed ordering key but does not silently alter PVR depth.
    Colors use A, B, C, D rectangle order. Uniform-color users may repeat one
    value four times and continue through the compact hardware sprite path.
*/
typedef struct pvr_cell_state {
    size_t atlas_cell_index;
    point_t offset;
    float rotation;
    float scale_x;
    float scale_y;
    int32_t priority;
    uint32_t flags;
    uint32_t material_id;
    uint32_t argb[4];
    uint32_t oargb[4];
} pvr_cell_state_t;

/** \brief One timestamped partial replacement of a cell slot. */
typedef struct pvr_cell_key {
    float time;
    size_t slot_index;
    uint32_t fields;
    pvr_cell_state_t value;
} pvr_cell_key_t;

/** \brief One independently timed stream of cell changes.

    Sampling adds \p time_offset before applying the time policy. A repeating
    stream wraps over `[0, time_max)`; a nonrepeating stream clamps to
    `[0, time_max]`. Keys are step changes ordered by nondecreasing time, so
    different slots may change at the same timestamp.
*/
typedef struct pvr_cell_stream {
    const pvr_cell_key_t *keys;
    size_t key_count;
    float time_offset;
    float time_max;
    uint32_t repeat;
} pvr_cell_stream_t;

/** \brief Validated immutable stream view. */
typedef struct pvr_cell_stream_view {
    pvr_cell_stream_t stream;
    size_t slot_count;
} pvr_cell_stream_view_t;

/** \brief Ordered stream list; later streams override earlier fields. */
typedef struct pvr_cell_stream_list {
    const pvr_cell_stream_view_t *streams;
    size_t stream_count;
} pvr_cell_stream_list_t;

/** \brief Whole-object transform and color modulation for a cell sprite.

    Base cells are copied before streams are sampled. Scale is strictly
    positive. Diffuse and specular colors multiply corresponding per-corner
    colors with rounded eight-bit channel arithmetic.
*/
typedef struct pvr_cell_sprite {
    const pvr_cell_state_t *base_cells;
    size_t cell_count;
    point_t position;
    float rotation;
    float scale_x;
    float scale_y;
    uint32_t argb;
    uint32_t oargb;
} pvr_cell_sprite_t;

/** \brief One composed cell and its application-owned routing metadata.

    The sprite instance is first deliberately, allowing this array to be used
    as a strided pvr_sprite_instance_stream_t without copying. Material and
    colors remain available to a higher-level material router or colored-quad
    compiler; the hardware sprite packet itself carries uniform material
    color rather than four vertex colors.
*/
typedef struct pvr_cell_resolved {
    pvr_sprite_instance_t instance;
    size_t slot_index;
    int32_t priority;
    uint32_t material_id;
    uint32_t argb[4];
    uint32_t oargb[4];
} pvr_cell_resolved_t;

/** \brief Progress from stream sampling. */
typedef struct pvr_cell_sample_result {
    size_t sampled_streams;
    size_t applied_keys;
    size_t published_cells;
} pvr_cell_sample_result_t;

/** \brief Progress from whole-sprite composition. */
typedef struct pvr_cell_resolve_result {
    size_t examined_cells;
    size_t resolved_cells;
    size_t visible_cells;
} pvr_cell_resolve_result_t;

/** \brief Validate and publish one immutable cell stream view. */
int pvr_cell_stream_open(const pvr_cell_stream_t *stream, size_t slot_count,
                         pvr_cell_stream_view_t *output);

/** \brief Apply one stream to an existing complete cell-state array.

    Keys at or before the mapped sample time replace only their selected
    fields. Failure leaves the destination untouched.
*/
int pvr_cell_stream_sample(const pvr_cell_stream_view_t *stream, float time,
                           pvr_cell_state_t *cells, size_t cell_count,
                           size_t *applied_keys);

/** \brief Copy base state and apply every listed stream in list order.

    The operation is failure-atomic. \p workspace supplies a second complete
    cell array so validation or a later stream failure cannot expose a partial
    pose. Output and workspace must not overlap any source state or each other.
*/
int pvr_cell_stream_list_sample(const pvr_cell_sprite_t *sprite,
                                const pvr_cell_stream_list_t *streams,
                                float time,
                                pvr_cell_state_t *output,
                                pvr_cell_state_t *workspace,
                                size_t capacity,
                                pvr_cell_sample_result_t *result);

/** \brief Collect forward event crossings on one stream's local time base.

    Previous and current are finite application times with
    `current_time >= previous_time`. The stream offset and repeat/clamp policy
    are applied before event traversal. Repeated full cycles are counted
    arithmetically; publication remains bounded by \p output_capacity and is
    chronological. Event times must lie in the stream interval, with the
    repeating endpoint excluded just like cell keys. `ERANGE` is returned when
    a repeated cycle index is too large for exact traversal accounting.
*/
int pvr_cell_stream_collect_events(
    const pvr_cell_stream_view_t *stream,
    float previous_time, float current_time,
    const anim_event_track_view_t *events,
    anim_event_occurrence_t *output, size_t output_capacity,
    anim_event_result_t *result);

/** \brief Apply one generic animated transform to a cell-sprite descriptor.

    Translation is added to the sprite position and XY scale is multiplied.
    Rotation must be planar about Z; genuinely three-dimensional quaternions
    return `ENOTSUP` instead of being silently projected. Dreamcast builds use
    SH4ZAM for quaternion normalization and Z-angle extraction. Failure leaves
    \p output unchanged.
*/
int pvr_cell_sprite_apply_transform(const pvr_cell_sprite_t *sprite,
                                    const anim_transform_t *transform,
                                    pvr_cell_sprite_t *output);

/** \brief Compose sampled cells under the whole cell-sprite transform.

    On Dreamcast, rotation uses SH4ZAM's paired sine/cosine primitive. The
    returned instance stream then reaches SH4ZAM again through the established
    three-dimensional sprite projection path. Failure is atomic.
*/
int pvr_cell_sprite_resolve(const pvr_cell_sprite_t *sprite,
                            const pvr_cell_state_t *cells,
                            size_t cell_count,
                            pvr_cell_resolved_t *output,
                            size_t output_capacity,
                            pvr_cell_resolve_result_t *result);

/** \brief Stably order resolved cells by signed priority, then slot index. */
int pvr_cell_resolved_sort(pvr_cell_resolved_t *cells, size_t cell_count);

/** \brief Build the strided instance view consumed by sprite geometry. */
int pvr_cell_resolved_stream(const pvr_cell_resolved_t *cells,
                             size_t cell_count,
                             pvr_sprite_instance_stream_t *output);

/** \brief Compile composed cells into screen-space hardware sprite packets. */
int pvr_cell_sprite_compile_2d(
    pvr_sprite_txr_t *output, size_t output_capacity,
    const pvr_sprite_atlas_t *atlas,
    const pvr_cell_resolved_t *cells, size_t cell_count,
    pvr_sprite_batch_result_t *result);

/** \brief Compile and project composed cells as camera-facing sprites. */
int pvr_cell_sprite_compile_3d(
    pvr_sprite_txr_t *output, size_t output_capacity,
    const pvr_sprite_atlas_t *atlas,
    const pvr_cell_resolved_t *cells, size_t cell_count,
    const pvr_sprite_billboard_basis_t *basis,
    const matrix_t *world_to_screen,
    pvr_sprite_batch_result_t *result);

/** \brief Compile screen-space cells as colored textured quad strips.

    This is the expressive path for independent A/B/C/D diffuse and offset
    colors. It emits four pvr_vertex_t records per visible cell; callers that
    use uniform material color should prefer pvr_cell_sprite_compile_2d(),
    whose hardware sprite packet uses half as much vertex storage. Material
    identifiers remain in the resolved records for caller-owned pass routing.
*/
int pvr_cell_sprite_compile_colored_2d(
    pvr_vertex_t *output, size_t output_capacity,
    const pvr_sprite_atlas_t *atlas,
    const pvr_cell_resolved_t *cells, size_t cell_count,
    pvr_sprite_batch_result_t *result);

/** \brief Compile and project colored cells as camera-facing quad strips.

    World construction uses SH4ZAM paired sine/cosine on Dreamcast. Projection
    uses the shared SH4ZAM-backed geometry batch and preserves the valid output
    prefix reported through \p result if a projected cell fails.
*/
int pvr_cell_sprite_compile_colored_3d(
    pvr_vertex_t *output, size_t output_capacity,
    const pvr_sprite_atlas_t *atlas,
    const pvr_cell_resolved_t *cells, size_t cell_count,
    const pvr_sprite_billboard_basis_t *basis,
    const matrix_t *world_to_screen,
    pvr_sprite_batch_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CELL_H */
