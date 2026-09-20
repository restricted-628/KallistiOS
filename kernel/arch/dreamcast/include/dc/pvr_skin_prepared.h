/* KallistiOS ##version##

   dc/pvr_skin_prepared.h
   Copyright (C) 2026 Joseph Black
*/

/** \file dc/pvr_skin_prepared.h
    \brief Caller-owned SH4ZAM skin palettes prepared once per pose.
    \ingroup pvr_deform
*/

#ifndef __DC_PVR_SKIN_PREPARED_H
#define __DC_PVR_SKIN_PREPARED_H

#include <dc/pvr_deform.h>
#include <dc/sh4zam.h>

__BEGIN_DECLS

/** \brief One imported joint; populated only by palette preparation. */
typedef struct pvr_skin_prepared_joint {
    shz_mat4x4_t position;
    shz_mat3x3_t normal;
} pvr_skin_prepared_joint_t;

/** \brief Immutable snapshot referencing caller-owned imported joints.

    Initialize only with pvr_skin_palette_prepare(). Neither the descriptor
    nor its joint storage may change during application. Source KOS matrices
    are copied, not borrowed, and may be changed or released after preparation.
    Reprepare after sampling a new pose; reuse the snapshot across all meshes
    sharing that palette. Do not serialize this runtime representation.
*/
typedef struct pvr_skin_prepared_palette {
    const pvr_skin_prepared_joint_t *joints;
    size_t joint_count;
    uint32_t version;
} pvr_skin_prepared_palette_t;

/** \brief Validate and import one palette into reusable SH4ZAM storage.

    Supply at least palette->joint_count naturally aligned joint records.
    Source arrays, storage, input descriptor, and output descriptor must not
    overlap any destination. All matrices are validated before any destination
    write; storage and prepared remain unchanged on failure. No allocation or
    XMTRX modification occurs. Preparation is required for every changed pose,
    not necessarily every draw.
*/
int pvr_skin_palette_prepare(const pvr_skin_palette_t *palette,
    pvr_skin_prepared_joint_t *storage, size_t joint_capacity,
    pvr_skin_prepared_palette_t *prepared);

/** \brief Skin four-influence vertices with an immutable prepared palette.

    Mirrors pvr_skin_apply(), but skips palette component scans and per-active-
    influence matrix imports. Weight/index validation, mutable source checking,
    output range/overlap checks, normalization, valid-prefix errors, and XMTRX
    preservation remain. Output must not overlap the prepared descriptor or
    joint storage. The snapshot must have been prepared successfully and remain
    intact; its version marker is not a substitute for that lifetime contract.
*/
int pvr_skin_apply_prepared_palette(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_stream_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result);

/** \brief Variable-span equivalent of pvr_skin_apply_prepared_palette(). */
int pvr_skin_apply_spans_prepared_palette(pvr_deform_vertex_t *output,
    size_t output_capacity, const pvr_deform_stream_t *vertices,
    const pvr_skin_span_stream_t *influences,
    const pvr_skin_prepared_palette_t *palette, pvr_deform_result_t *result);

__END_DECLS
#endif /* __DC_PVR_SKIN_PREPARED_H */
