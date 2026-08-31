/* KallistiOS ##version##

   dc/pvr_chunk_animation_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_animation_asset.h
    \brief   Serialized animation clips for compact assets.
    \ingroup pvr_chunk_model

    The section stores pointer-free transform-channel references and canonical
    key records. Checked data is materialized into the existing animation
    runtime using caller-owned arrays; no second playback or interpolation
    system is introduced.
*/

#ifndef __DC_PVR_CHUNK_ANIMATION_ASSET_H
#define __DC_PVR_CHUNK_ANIMATION_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/animation.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Little-endian bytes `PAT1` at the start of an animation section. */
#define PVR_CHUNK_ANIMATION_SECTION_MAGIC UINT32_C(0x31544150)

/** \brief Current serialized animation-section version. */
#define PVR_CHUNK_ANIMATION_SECTION_VERSION 1u

/** \brief Fixed serialized animation header size. */
#define PVR_CHUNK_ANIMATION_SECTION_HEADER_BYTES 64u

/** \brief Fixed serialized transform binding size. */
#define PVR_CHUNK_ANIMATION_SECTION_TRANSFORM_BYTES 64u

/** \brief Fixed serialized track descriptor size. */
#define PVR_CHUNK_ANIMATION_SECTION_TRACK_BYTES 16u

/** \brief Fixed serialized canonical key size. */
#define PVR_CHUNK_ANIMATION_SECTION_KEY_BYTES 24u

/** \brief Serialized ordinal for an absent channel. */
#define PVR_CHUNK_ANIMATION_TRACK_NONE UINT32_MAX

/** \brief Pointer-free description of one serialized key track. */
typedef struct pvr_chunk_animation_section_track {
    anim_value_kind_t kind;
    anim_interpolation_t interpolation;
    uint32_t first_key;
    uint32_t key_count;
} pvr_chunk_animation_section_track_t;

/** \brief Pointer-free channels and fallback state for one transform. */
typedef struct pvr_chunk_animation_section_transform {
    uint32_t translation_track;
    uint32_t rotation_track;
    uint32_t scale_track;
    uint32_t visibility_track;
    anim_transform_t fallback;
    uint32_t fallback_visible;
} pvr_chunk_animation_section_transform_t;

/** \brief Canonical caller-owned key storage accepted by anim_track_open(). */
typedef union pvr_chunk_animation_key_value {
    float scalar;
    vector_t vector;
    anim_quaternion_t quaternion;
    uint32_t boolean;
} pvr_chunk_animation_key_value_t;

/** \brief One materialized key with the value beginning after its time. */
typedef struct pvr_chunk_animation_key {
    float time;
    pvr_chunk_animation_key_value_t value;
} pvr_chunk_animation_key_t;

/** \brief Checked immutable view of one serialized animation clip.

    The source byte image must remain immutable and accessible for the view's
    lifetime and while any materialized runtime objects are being used.
*/
typedef struct pvr_chunk_animation_section_view {
    const void *data;
    size_t size;
    const void *transforms;
    size_t transform_count;
    const void *tracks;
    size_t track_count;
    const void *keys;
    size_t key_count;
    float start_time;
    float end_time;
    uint16_t version;
} pvr_chunk_animation_section_view_t;

/** \brief Parse and completely validate one serialized animation clip.

    Validation covers framing, checksums, channel kinds, gapless key spans,
    finite strictly ordered keys, quaternion magnitude, canonical unused key
    components, fallback transforms, and transform-to-track references.
*/
int pvr_chunk_animation_section_open(
    const void *data, size_t size,
    pvr_chunk_animation_section_view_t *view);

/** \brief Decode one admitted transform binding by index. */
int pvr_chunk_animation_section_transform_get(
    const pvr_chunk_animation_section_view_t *view, size_t index,
    pvr_chunk_animation_section_transform_t *transform);

/** \brief Decode one admitted track descriptor by index. */
int pvr_chunk_animation_section_track_get(
    const pvr_chunk_animation_section_view_t *view, size_t index,
    pvr_chunk_animation_section_track_t *track);

/** \brief Materialize an admitted section into the animation runtime.

    Every capacity, alignment, and overlap is checked before output storage is
    modified. Track views borrow the caller-owned canonical key array;
    transform and visibility bindings borrow the caller-owned track array.
    The resulting clip can be sampled or attached to a caller-owned playback
    cursor immediately. All output storage remains unchanged on failure.
*/
int pvr_chunk_animation_section_materialize(
    const pvr_chunk_animation_section_view_t *view,
    pvr_chunk_animation_key_t *keys, size_t key_capacity,
    anim_track_view_t *tracks, size_t track_capacity,
    anim_transform_tracks_t *transforms, size_t transform_capacity,
    anim_visibility_tracks_t *visibility, size_t visibility_capacity,
    anim_clip_view_t *clip);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_ANIMATION_ASSET_H */
