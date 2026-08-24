/* KallistiOS ##version##

   dc/animation.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/animation.h
    \brief   Bounded, format-neutral keyframe and transform sampling.
    \ingroup animation

    This interface owns no clip, model, clock, hierarchy, or output storage.
    Track admission is linear; subsequent clamped sampling is logarithmic.
*/

#ifndef __DC_ANIMATION_H
#define __DC_ANIMATION_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>

#include <dc/vector.h>

/** \defgroup animation Keyframe animation
    \brief                 Checked caller-owned animation sampling
    \ingroup               math_matrices
    @{ */

/** \brief Value representation stored by a track. */
typedef enum anim_value_kind {
    ANIM_VALUE_SCALAR = 0,
    ANIM_VALUE_VECTOR,
    ANIM_VALUE_QUATERNION
} anim_value_kind_t;

/** \brief Interpolation applied between adjacent keys. */
typedef enum anim_interpolation {
    ANIM_INTERPOLATION_STEP = 0,
    ANIM_INTERPOLATION_LINEAR
} anim_interpolation_t;

/** \brief Quaternion components in scalar-first WXYZ order. */
typedef struct anim_quaternion {
    float w;
    float x;
    float y;
    float z;
} anim_quaternion_t;

/** \brief Scalar keyframe. */
typedef struct anim_scalar_key {
    float time;
    float value;
} anim_scalar_key_t;

/** \brief Four-component vector keyframe. */
typedef struct anim_vector_key {
    float time;
    vector_t value;
} anim_vector_key_t;

/** \brief Quaternion keyframe. */
typedef struct anim_quaternion_key {
    float time;
    anim_quaternion_t value;
} anim_quaternion_key_t;

/** \brief Bounded source track description.

    Every key representation begins with its finite time. Stride permits a key
    to be the first member of a larger application-owned structure.
*/
typedef struct anim_track {
    anim_value_kind_t kind;
    anim_interpolation_t interpolation;
    const void *keys;
    size_t key_count;
    size_t stride;
} anim_track_t;

/** \brief Validated immutable track view.

    Create this through anim_track_open(). Source keys must remain immutable and
    accessible for the view's lifetime.
*/
typedef struct anim_track_view {
    anim_track_t track;
    float start_time;
    float end_time;
} anim_track_view_t;

/** \brief Interval selected by a sample operation. */
typedef struct anim_sample_info {
    size_t lower_key;
    size_t upper_key;
    float factor;
} anim_sample_info_t;

/** \brief Translation, rotation, and scale for one object.

    Translation and scale use XYZ; their W components are ignored. Rotation is
    normalized before interpolation or matrix publication.
*/
typedef struct anim_transform {
    vector_t translation;
    anim_quaternion_t rotation;
    vector_t scale;
} anim_transform_t;

/** \brief Optional tracks and fallback state for one object. */
typedef struct anim_transform_tracks {
    const anim_track_view_t *translation;
    const anim_track_view_t *rotation;
    const anim_track_view_t *scale;
    anim_transform_t fallback;
} anim_transform_tracks_t;

/** \brief Validate a complete immutable key track.

    Key times must be finite and strictly increasing. Values must be finite;
    quaternion magnitudes must be nonzero. Failure leaves \p output unchanged.
*/
int anim_track_open(const anim_track_t *track, anim_track_view_t *output);

/** \brief Sample a scalar track, clamping time to its endpoints. */
int anim_track_sample_scalar(const anim_track_view_t *track, float time,
                             float *output, anim_sample_info_t *info);

/** \brief Sample a vector track, clamping time to its endpoints. */
int anim_track_sample_vector(const anim_track_view_t *track, float time,
                             vector_t *output, anim_sample_info_t *info);

/** \brief Sample a quaternion track with shortest-path interpolation.

    Step keys are normalized before publication. Linear interpolation uses a
    normalized shortest-path spherical interpolation.
*/
int anim_track_sample_quaternion(const anim_track_view_t *track, float time,
                                 anim_quaternion_t *output,
                                 anim_sample_info_t *info);

/** \brief Sample the available tracks of one object.

    Missing channels retain their fallback values. Translation and scale tracks
    must be vector tracks; rotation must be a quaternion track. Failure leaves
    \p output unchanged.
*/
int anim_transform_sample(const anim_transform_tracks_t *tracks, float time,
                          anim_transform_t *output);

/** \brief Blend two complete object transforms.

    Translation and scale use linear interpolation. Rotation uses normalized
    shortest-path spherical interpolation. Weight must be in `[0, 1]`.
*/
int anim_transform_blend(const anim_transform_t *from,
                         const anim_transform_t *to, float weight,
                         anim_transform_t *output);

/** \brief Publish a column-major `translation * rotation * scale` matrix.

    Failure leaves \p output unchanged and never changes XMTRX.
*/
int anim_transform_matrix_build(const anim_transform_t *transform,
                                matrix_t *output);

/** @} */

__END_DECLS
#endif /* __DC_ANIMATION_H */
