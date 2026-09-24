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
#include <stdbool.h>
#include <stdint.h>

#include <dc/vector.h>
#include <dc/matrix3d.h>
#include <dc/pvr_lighting.h>
#include <dc/pvr_deform.h>

/** \defgroup animation Keyframe animation
    \brief                 Checked caller-owned animation sampling
    \ingroup               math_matrices
    @{ */

/** \brief Value representation stored by a track. */
typedef enum anim_value_kind {
    ANIM_VALUE_SCALAR = 0,
    ANIM_VALUE_VECTOR,
    ANIM_VALUE_QUATERNION,
    ANIM_VALUE_BOOLEAN
} anim_value_kind_t;

/** \brief Interpolation applied between adjacent keys. */
typedef enum anim_interpolation {
    ANIM_INTERPOLATION_STEP = 0,
    ANIM_INTERPOLATION_LINEAR,
    /** Time-aware Catmull-Rom spline for scalar and vector tracks. */
    ANIM_INTERPOLATION_CATMULL_ROM,
    /** Cubic Hermite interpolation with explicit per-key tangents. */
    ANIM_INTERPOLATION_CUBIC_HERMITE
} anim_interpolation_t;

/** \brief Quaternion components in scalar-first WXYZ order. */
typedef struct anim_quaternion {
    float w;
    float x;
    float y;
    float z;
} anim_quaternion_t;

/** \brief Rotation representation and Euler application order.

    Euler modes store XYZ angles in a vector track, in radians. The named
    order is the order in which the three axis rotations are multiplied.
*/
typedef enum anim_rotation_mode {
    ANIM_ROTATION_QUATERNION = 0,
    ANIM_ROTATION_EULER_XYZ,
    ANIM_ROTATION_EULER_ZXY
} anim_rotation_mode_t;

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

/** \brief Scalar keyframe with incoming and outgoing time derivatives.

    Reserved words keep value and tangent slots layout-compatible with the
    canonical four-component Hermite keys used by heterogeneous asset arrays.
    They must be zero.
*/
typedef struct anim_scalar_hermite_key {
    float time;
    float value;
    float value_reserved[3];
    float in_tangent;
    float in_tangent_reserved[3];
    float out_tangent;
    float out_tangent_reserved[3];
} anim_scalar_hermite_key_t;

/** \brief Vector keyframe with incoming and outgoing time derivatives. */
typedef struct anim_vector_hermite_key {
    float time;
    vector_t value;
    vector_t in_tangent;
    vector_t out_tangent;
} anim_vector_hermite_key_t;

/** \brief Quaternion keyframe with component-wise time derivatives. */
typedef struct anim_quaternion_hermite_key {
    float time;
    anim_quaternion_t value;
    anim_quaternion_t in_tangent;
    anim_quaternion_t out_tangent;
} anim_quaternion_hermite_key_t;

/** \brief Step-only Boolean keyframe. Value must be zero or one. */
typedef struct anim_boolean_key {
    float time;
    uint32_t value;
} anim_boolean_key_t;

/** \brief One application-defined event marker. */
typedef struct anim_event_key {
    float time;
    uint32_t identifier;
    uint32_t value;
} anim_event_key_t;

/** \brief Bounded event marker source. */
typedef struct anim_event_track {
    const anim_event_key_t *events;
    size_t event_count;
} anim_event_track_t;

/** \brief Validated immutable event marker view. */
typedef struct anim_event_track_view {
    anim_event_track_t track;
    float start_time;
    float end_time;
} anim_event_track_view_t;

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
    anim_rotation_mode_t rotation_mode;
} anim_transform_tracks_t;

/** \brief Optional step visibility channel and its fallback. */
typedef struct anim_visibility_tracks {
    const anim_track_view_t *visible;
    bool fallback;
} anim_visibility_tracks_t;

/** \brief Optional weight channel and existing PVR morph-target binding. */
typedef struct anim_morph_target_tracks {
    const anim_track_view_t *weight;
    pvr_morph_target_t fallback;
} anim_morph_target_tracks_t;

/** \brief Progress from bounded morph-target weight sampling. */
typedef struct anim_morph_result {
    size_t sampled_targets;
} anim_morph_result_t;

/** \brief Bounded collection of object-transform tracks and a play interval.

    Track arrays, admitted track views, and their source keys are borrowed and
    must remain immutable for the lifetime of every view opened from the clip.
    The explicit interval may select a subrange of the underlying tracks.
*/
typedef struct anim_clip {
    const anim_transform_tracks_t *transforms;
    size_t transform_count;
    float start_time;
    float end_time;
    const anim_visibility_tracks_t *visibility;
} anim_clip_t;

/** \brief Validated immutable clip view. */
typedef struct anim_clip_view {
    anim_clip_t clip;
} anim_clip_view_t;

/** \brief Progress from bounded clip sampling. */
typedef struct anim_pose_result {
    size_t sampled_transforms;
} anim_pose_result_t;

/** \brief Time policy applied when playback reaches a clip boundary. */
typedef enum anim_playback_mode {
    /** Stop at the first terminal boundary reached. */
    ANIM_PLAYBACK_ONCE = 0,
    /** Wrap to the opposite clip boundary and continue. */
    ANIM_PLAYBACK_LOOP,
    /** Reflect at each clip boundary and continue. */
    ANIM_PLAYBACK_PING_PONG
} anim_playback_mode_t;

/** \brief Current playback direction. */
typedef enum anim_playback_direction {
    ANIM_PLAYBACK_BACKWARD = -1,
    ANIM_PLAYBACK_FORWARD = 1
} anim_playback_direction_t;

/** \brief Observable state of a caller-owned playback cursor. */
typedef enum anim_playback_state {
    ANIM_PLAYBACK_STOPPED = 0,
    ANIM_PLAYBACK_PLAYING,
    ANIM_PLAYBACK_PAUSED,
    ANIM_PLAYBACK_COMPLETE
} anim_playback_state_t;

/** \brief Caller-owned clip playback cursor.

    Applications should initialize this structure with anim_playback_init()
    and mutate it only through the playback functions. It owns no clip or
    output storage and performs no work unless explicitly advanced.
*/
typedef struct anim_playback {
    const anim_clip_view_t *clip;
    float time;
    float rate;
    uint64_t boundary_count;
    anim_playback_mode_t mode;
    anim_playback_direction_t direction;
    anim_playback_state_t state;
} anim_playback_t;

/** \brief One atomic playback-advance result. */
typedef struct anim_playback_result {
    float previous_time;
    float current_time;
    uint64_t crossed_boundaries;
    anim_playback_direction_t previous_direction;
    anim_playback_direction_t current_direction;
    anim_playback_state_t state;
} anim_playback_result_t;

/** \brief One event occurrence in playback traversal order. */
typedef struct anim_event_occurrence {
    anim_event_key_t event;
    anim_playback_direction_t direction;
} anim_event_occurrence_t;

/** \brief Bounded event collection result. */
typedef struct anim_event_result {
    uint64_t matching_events;
    size_t published_events;
    bool truncated;
} anim_event_result_t;

/** \brief Sampled camera state independent of a retained camera object.

    Vertical field of view and roll are in radians. Roll rotates the supplied
    up direction around the eye-to-target axis before look-at construction.
*/
typedef struct anim_camera_pose {
    point_t eye;
    point_t target;
    vector_t up;
    float roll;
    float vertical_fov;
} anim_camera_pose_t;

/** \brief Optional tracks and fallback state for one camera. */
typedef struct anim_camera_tracks {
    const anim_track_view_t *eye;
    const anim_track_view_t *target;
    const anim_track_view_t *up;
    const anim_track_view_t *roll;
    const anim_track_view_t *vertical_fov;
    anim_camera_pose_t fallback;
} anim_camera_tracks_t;

/** \brief Optional tracks and fallback state for one existing PVR light.

    The source track samples either position or direction according to the
    fallback light kind. Color is a vector track; intensity and range are
    scalar tracks. Attenuation and kind remain application-defined constants.
*/
typedef struct anim_light_tracks {
    const anim_track_view_t *source;
    const anim_track_view_t *color;
    const anim_track_view_t *intensity;
    const anim_track_view_t *range;
    pvr_light_t fallback;
} anim_light_tracks_t;

/** \brief Validate a complete immutable key track.

    Key times must be finite and strictly increasing. Values must be finite;
    quaternion magnitudes must be nonzero. Failure leaves \p output unchanged.
*/
int anim_track_open(const anim_track_t *track, anim_track_view_t *output);

/** \brief Sample a scalar track, clamping time to its endpoints.

    Catmull-Rom tracks use adjacent key times when deriving their tangents, so
    unevenly spaced keys retain a consistent time-domain slope. Endpoint keys
    are repeated as the missing outer control point. Cubic Hermite tracks use
    the lower key's outgoing derivative and upper key's incoming derivative,
    scaled by the active key interval.
*/
int anim_track_sample_scalar(const anim_track_view_t *track, float time,
                             float *output, anim_sample_info_t *info);

/** \brief Sample a vector track, clamping time to its endpoints.

    Catmull-Rom interpolation applies independently to XYZW with the same
    time-aware tangent and endpoint rules as scalar tracks. Cubic Hermite
    interpolation applies the explicit derivative slots independently.
*/
int anim_track_sample_vector(const anim_track_view_t *track, float time,
                             vector_t *output, anim_sample_info_t *info);

/** \brief Sample an XYZ Euler-angle vector over shortest angular arcs.

    Angles are in radians. Step, linear, time-aware Catmull-Rom, and explicit
    cubic Hermite tracks are supported. For Catmull-Rom sampling, neighboring
    control points are unwrapped around the active interval before
    interpolation. The output W is zero.
*/
int anim_track_sample_euler(const anim_track_view_t *track, float time,
                            vector_t *output, anim_sample_info_t *info);

/** \brief Sample a quaternion track with shortest-path interpolation.

    Step keys are normalized before publication. Linear interpolation uses a
    normalized shortest-path spherical interpolation. Cubic Hermite performs
    component-wise interpolation with explicit derivatives and normalizes the
    sampled result before publication.
*/
int anim_track_sample_quaternion(const anim_track_view_t *track, float time,
                                 anim_quaternion_t *output,
                                 anim_sample_info_t *info);

/** \brief Convert finite XYZ Euler angles to a normalized quaternion.

    Angles are in radians. \a mode must be ANIM_ROTATION_EULER_XYZ or
    ANIM_ROTATION_EULER_ZXY. Failure leaves \p output unchanged.
*/
int anim_euler_to_quaternion(const vector_t *angles,
                             anim_rotation_mode_t mode,
                             anim_quaternion_t *output);

/** \brief Sample a step-only Boolean track, clamping to its endpoints. */
int anim_track_sample_boolean(const anim_track_view_t *track, float time,
                              bool *output, anim_sample_info_t *info);

/** \brief Validate strictly ordered, application-defined event markers. */
int anim_event_track_open(const anim_event_track_t *track,
                          anim_event_track_view_t *output);

/** \brief Sample the available tracks of one object.

    Missing channels retain their fallback values. Translation and scale tracks
    must be vector tracks. Rotation is a quaternion track in quaternion mode
    and an XYZ-angle vector track in either Euler mode. Euler tracks are sampled
    before conversion to the published quaternion. Failure leaves \p output
    unchanged.
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

/** \brief Validate and publish an immutable transform clip.

    The play interval must be finite and have positive duration. Every fallback
    transform and referenced track view is checked before publication. Failure
    leaves \p output unchanged.
*/
int anim_clip_open(const anim_clip_t *clip, anim_clip_view_t *output);

/** \brief Sample every transform in a clip at one clamped time.

    The caller supplies at least `clip.transform_count` outputs. On a sampling
    failure, the result identifies the valid output prefix.
*/
int anim_clip_sample(const anim_clip_view_t *clip, float time,
                     anim_transform_t *output, size_t output_capacity,
                     anim_pose_result_t *result);

/** \brief Sample and build one local matrix per transform in a clip.

    The matrix array may be passed directly as the local-transform override to
    hierarchy traversal. On failure, the result identifies the valid prefix.
*/
int anim_clip_sample_matrices(const anim_clip_view_t *clip, float time,
                              matrix_t *output, size_t output_capacity,
                              anim_pose_result_t *result);

/** \brief Sample one visibility value per transform in a clip.

    A clip without a visibility array publishes `true` for every transform.
    Missing channels use their caller-provided fallback. Failure reports the
    valid output prefix through \p result.
*/
int anim_clip_sample_visibility(const anim_clip_view_t *clip, float time,
                                bool *output, size_t output_capacity,
                                anim_pose_result_t *result);

/** \brief Sample and blend corresponding transforms from two clips.

    Clips must contain the same number of transforms in the same application-
    defined order. This provides allocation-free motion linking without
    retaining either clip or pose. Weight must be in `[0, 1]`.
*/
int anim_clip_sample_blend(const anim_clip_view_t *from, float from_time,
                           const anim_clip_view_t *to, float to_time,
                           float weight, anim_transform_t *output,
                           size_t output_capacity,
                           anim_pose_result_t *result);

/** \brief Initialize stopped, forward playback at the clip's start time. */
int anim_playback_init(anim_playback_t *playback,
                       const anim_clip_view_t *clip,
                       anim_playback_mode_t mode);

/** \brief Begin or resume playback.

    Resuming completed one-shot playback restarts at the boundary opposite the
    selected direction. Other states retain the current time.
*/
int anim_playback_play(anim_playback_t *playback);

/** \brief Pause a valid playback cursor without changing its time. */
int anim_playback_pause(anim_playback_t *playback);

/** \brief Stop playback and reset it to the clip start, facing forward. */
int anim_playback_stop(anim_playback_t *playback);

/** \brief Seek within the inclusive clip interval without dispatching work. */
int anim_playback_seek(anim_playback_t *playback, float time);

/** \brief Set a finite, strictly positive playback rate. */
int anim_playback_set_rate(anim_playback_t *playback, float rate);

/** \brief Select forward or backward playback without changing time. */
int anim_playback_set_direction(anim_playback_t *playback,
                                anim_playback_direction_t direction);

/** \brief Advance playback by a nonnegative elapsed time.

    Boundary traversal is computed in constant time even when a large elapsed
    value crosses many loops or reflections. Paused, stopped, and completed
    cursors do not move. Failure leaves both the cursor and \p result unchanged.
*/
int anim_playback_advance(anim_playback_t *playback, float elapsed,
                          anim_playback_result_t *result);

/** \brief Collect event markers crossed by one playback advance.

    \p playback must contain the post-advance cursor and \p advance must be the
    corresponding result from anim_playback_advance(). Events are published in
    traversal order, including direction changes and loop discontinuities.
    Work and writes never exceed \p output_capacity; a NULL output is valid
    when capacity is zero. The result always reports the full event count and
    whether the caller-owned output truncated it. Large loop counts are counted
    arithmetically rather than iterated.
*/
int anim_playback_collect_events(const anim_playback_t *playback,
                                 const anim_playback_result_t *advance,
                                 const anim_event_track_view_t *events,
                                 anim_event_occurrence_t *output,
                                 size_t output_capacity,
                                 anim_event_result_t *result);

/** \brief Sample the current pose of a valid playback cursor. */
int anim_playback_sample(const anim_playback_t *playback,
                         anim_transform_t *output, size_t output_capacity,
                         anim_pose_result_t *result);

/** \brief Sample camera position, target, up, roll, and vertical field of view.

    Vector channels must be vector tracks and scalar channels must be scalar
    tracks. Failure leaves \p output unchanged.
*/
int anim_camera_sample(const anim_camera_tracks_t *tracks, float time,
                       anim_camera_pose_t *output);

/** \brief Build a checked look-at matrix from a sampled camera pose.

    Roll is incorporated by rotating the up direction about the viewing axis.
    Failure leaves \p output unchanged and never changes XMTRX.
*/
int anim_camera_view_matrix_build(const anim_camera_pose_t *camera,
                                  matrix_t *output);

/** \brief Build a checked perspective matrix from a sampled camera pose.

    The camera's vertical field of view is converted to the cotangent form used
    by mat_perspective_build(). Screen center and near/far distances remain
    explicit application policy. Failure leaves \p output unchanged.
*/
int anim_camera_projection_matrix_build(const anim_camera_pose_t *camera,
                                        float x_center, float y_center,
                                        float z_near, float z_far,
                                        matrix_t *output);

/** \brief Sample a camera at the current time of a playback cursor. */
int anim_playback_sample_camera(const anim_playback_t *playback,
                                const anim_camera_tracks_t *tracks,
                                anim_camera_pose_t *output);

/** \brief Sample one directional or point light into the existing PVR form.

    Sampled colors, intensities, and ranges must remain nonnegative. A sampled
    directional source must remain nonzero. Failure leaves \p output unchanged.
*/
int anim_light_sample(const anim_light_tracks_t *tracks, float time,
                      pvr_light_t *output);

/** \brief Sample a light at the current time of a playback cursor. */
int anim_playback_sample_light(const anim_playback_t *playback,
                               const anim_light_tracks_t *tracks,
                               pvr_light_t *output);

/** \brief Sample caller-owned morph bindings into existing PVR targets.

    Each output copies the corresponding fallback target and replaces only its
    finite weight when a scalar channel is present. The result can be passed
    directly to pvr_morph_apply().
*/
int anim_morph_targets_sample(const anim_morph_target_tracks_t *tracks,
                              size_t target_count, float time,
                              pvr_morph_target_t *output,
                              size_t output_capacity,
                              anim_morph_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_ANIMATION_H */
