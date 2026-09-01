/* KallistiOS ##version##

   dc/pvr_chunk_morph_animation_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_morph_animation_asset.h
    \brief   Serialized morph-weight animation for compact scenes.
    \ingroup pvr_chunk_model

    Morph weights belong to scene-node instances rather than transform
    channels or model geometry. This section binds one scalar track per target
    to a hierarchy node and model ordinal, then materializes directly into the
    existing compact-shape animation types using caller-owned storage.
*/

#ifndef __DC_PVR_CHUNK_MORPH_ANIMATION_ASSET_H
#define __DC_PVR_CHUNK_MORPH_ANIMATION_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_model_table.h>
#include <dc/pvr_chunk_scene.h>
#include <dc/pvr_chunk_shape_asset.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PMW1` at the start of the section. */
#define PVR_CHUNK_MORPH_ANIMATION_MAGIC UINT32_C(0x31574d50)

/** \brief Current serialized morph-animation version. */
#define PVR_CHUNK_MORPH_ANIMATION_VERSION 1u

/** \brief Fixed section header size. */
#define PVR_CHUNK_MORPH_ANIMATION_HEADER_BYTES 64u

/** \brief Fixed serialized node-binding size. */
#define PVR_CHUNK_MORPH_ANIMATION_BINDING_BYTES 16u

/** \brief Fixed serialized target-channel size. */
#define PVR_CHUNK_MORPH_ANIMATION_CHANNEL_BYTES 8u

/** \brief Fixed serialized scalar-track size. */
#define PVR_CHUNK_MORPH_ANIMATION_TRACK_BYTES 16u

/** \brief Fixed serialized scalar-key size. */
#define PVR_CHUNK_MORPH_ANIMATION_KEY_BYTES 8u

/** \brief Pointer-free node binding decoded from one section record. */
typedef struct pvr_chunk_morph_animation_section_binding {
    uint32_t node_index;
    uint32_t model_ordinal;
    uint32_t first_channel;
    uint32_t channel_count;
} pvr_chunk_morph_animation_section_binding_t;

/** \brief Pointer-free scalar-track span decoded from one section record. */
typedef struct pvr_chunk_morph_animation_section_track {
    anim_interpolation_t interpolation;
    uint32_t first_key;
    uint32_t key_count;
} pvr_chunk_morph_animation_section_track_t;

/** \brief Runtime binding for one animated hierarchy-node instance. */
typedef struct pvr_chunk_morph_animation_binding {
    size_t node_index;
    size_t model_ordinal;
    const pvr_chunk_shape_channel_t *channels;
    size_t channel_count;
} pvr_chunk_morph_animation_binding_t;

/** \brief Materialized collection of instance-specific morph curves. */
typedef struct pvr_chunk_morph_animation {
    const pvr_chunk_morph_animation_binding_t *bindings;
    size_t binding_count;
    float start_time;
    float end_time;
} pvr_chunk_morph_animation_t;

/** \brief Checked immutable view of one serialized morph animation. */
typedef struct pvr_chunk_morph_animation_section_view {
    const void *data;
    size_t size;
    const void *bindings;
    size_t binding_count;
    const void *channels;
    size_t channel_count;
    const void *tracks;
    size_t track_count;
    const void *keys;
    size_t key_count;
    float start_time;
    float end_time;
    uint16_t version;
} pvr_chunk_morph_animation_section_view_t;

/** \brief Parse and completely validate one morph-animation section.

    Bindings must be ordered by unique hierarchy-node index. Channel and key
    spans are gapless, every channel has one scalar STEP or LINEAR track, and
    all finite keys are strictly ordered. The complete image and both CRCs are
    admitted before \p view changes.
*/
int pvr_chunk_morph_animation_section_open(
    const void *data, size_t size,
    pvr_chunk_morph_animation_section_view_t *view);

/** \brief Decode one admitted node binding by index. */
int pvr_chunk_morph_animation_section_binding_get(
    const pvr_chunk_morph_animation_section_view_t *view, size_t index,
    pvr_chunk_morph_animation_section_binding_t *binding);

/** \brief Decode one admitted target channel by index. */
int pvr_chunk_morph_animation_section_channel_get(
    const pvr_chunk_morph_animation_section_view_t *view, size_t index,
    uint32_t *track_ordinal, float *fallback_weight);

/** \brief Decode one admitted scalar-track span by index. */
int pvr_chunk_morph_animation_section_track_get(
    const pvr_chunk_morph_animation_section_view_t *view, size_t index,
    pvr_chunk_morph_animation_section_track_t *track);

/** \brief Decode one admitted scalar key by packed index. */
int pvr_chunk_morph_animation_section_key_get(
    const pvr_chunk_morph_animation_section_view_t *view, size_t index,
    anim_scalar_key_t *key);

/** \brief Cross-check instance bindings against scene and model metadata.

    \p shapes is indexed by morph-section ordinal. Each hierarchy node must
    reference the recorded model; that model's PMT1 record must own a shape
    section whose target count exactly matches the binding's channel count.
*/
int pvr_chunk_morph_animation_section_validate_scene(
    const pvr_chunk_morph_animation_section_view_t *view,
    const pvr_chunk_scene_hierarchy_view_t *hierarchy,
    const pvr_chunk_model_table_view_t *models,
    const pvr_chunk_shape_section_view_t *const *shapes,
    size_t shape_count);

/** \brief Materialize into existing scalar-track and shape-channel types.

    All arrays remain caller-owned. Bindings borrow \p channels, channels
    borrow \p tracks, and tracks borrow \p keys. Complete preflight precedes
    publication, so every output remains unchanged on failure.
*/
int pvr_chunk_morph_animation_section_materialize(
    const pvr_chunk_morph_animation_section_view_t *view,
    anim_scalar_key_t *keys, size_t key_capacity,
    anim_track_view_t *tracks, size_t track_capacity,
    pvr_chunk_shape_channel_t *channels, size_t channel_capacity,
    pvr_chunk_morph_animation_binding_t *bindings,
    size_t binding_capacity,
    pvr_chunk_morph_animation_t *animation);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_MORPH_ANIMATION_ASSET_H */
