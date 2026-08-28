# Keyframe animation contract

`dc/animation.h` provides format-neutral interpolation, caller-owned clip
playback, and object/camera/light sampling. It does not create a clock, thread,
fiber, event dispatcher, model, camera object, light manager, or pose buffer.
Nothing runs until an application explicitly calls it.

## Track admission and sampling

An `anim_track_t` is a bounded strided view over scalar, four-component vector,
WXYZ quaternion, or step-only Boolean keys. `anim_track_open()` validates the
complete address range, supported value and interpolation kinds, finite key
data, nonzero quaternions, and strictly increasing finite times before publishing an
immutable view. The source must not change while the view is in use.

Sampling clamps time before the first key and after the last key. Times inside
the track use binary search, so admitted tracks do not pay a linear validation
or interval-search cost every frame. Step interpolation publishes the lower
key. Linear scalar and vector tracks interpolate components. Catmull-Rom
scalar and vector tracks use adjacent key times to derive time-domain tangents,
so uneven key spacing does not silently become uniform parameter spacing. A
missing outer endpoint is represented by repeating the endpoint value one
current interval beyond the track. Quaternion tracks normalize their inputs
and take the shortest spherical path; cubic quaternion and Boolean tracks are
rejected rather than being assigned misleading component-wise semantics.

Individual tracks clamp rather than wrap. An admitted `anim_clip_view_t`
collects object tracks under an explicit positive-duration play interval.

## Playback

`anim_playback_t` is a small cursor stored and owned by the caller. It supports
one-shot, looping, and ping-pong time policy, forward or backward direction,
pause, stop, seek, rate control, and a cumulative boundary count. Advancing is
constant-time even when one elapsed value crosses many boundaries. It never
reads a system clock and is equally usable from a KOS thread, an opt-in fiber,
or a conventional frame loop.

The cursor owns no clip. Its referenced clip view, track descriptions, and key
storage must remain accessible and immutable while it is in use.

## Visibility and events

An optional visibility array associates one step-only Boolean channel and
fallback with each clip transform. `anim_clip_sample_visibility()` publishes a
bounded caller-owned Boolean array; clips without visibility state publish
`true` for every transform. Rendering or subtree suppression remains explicit
application policy.

Event tracks contain strictly ordered, application-defined identifiers and
values. `anim_playback_collect_events()` consumes one playback-advance result
and publishes crossed markers in traversal order across forward, backward,
looping, and reflected motion. A zero-capacity call counts without publishing.
Large loop counts are handled arithmetically, and insufficient output reports
truncation rather than performing unbounded callback work.

## Object transforms and hierarchy binding

`anim_transform_tracks_t` combines optional translation, rotation, and scale
views with a complete fallback transform. Missing channels retain fallback
values. `anim_transform_blend()` linearly blends translation and scale and
spherically blends rotation between two sampled poses.

`anim_transform_matrix_build()` converts one transform to an explicit
column-major `translation * rotation * scale` matrix.
`anim_clip_sample_matrices()` publishes a bounded array of these local
matrices. The array feeds `pvr_chunk_hierarchy_traverse_transforms()` directly,
so animation can replace local node transforms without copying or mutating the
model hierarchy. Exact in-place local-to-world composition is supported.

`anim_clip_sample_blend()` samples corresponding transforms from two clips and
blends them into caller-owned output. This supplies allocation-free motion
linking while leaving transition timing and clip selection to the application.

## Camera and light binding

Camera tracks sample eye, target, up, roll, and vertical field of view into an
`anim_camera_pose_t`. Checked helpers convert that pose into the established
KOS look-at and perspective matrices without changing XMTRX.

Light tracks sample source, color, intensity, and range into the existing
`pvr_light_t` representation. Kind and attenuation remain explicit fallback
policy, and the resulting lights feed the established bounded CPU-lighting
kernel directly. Applications retain the camera and light arrays; no duplicate
scene manager is introduced.

Scalar morph-weight channels copy caller-owned target bindings into the
existing `pvr_morph_target_t` representation. The sampled array passes directly
to `pvr_morph_apply()`; delta streams and deformed vertex storage retain their
existing ownership and validation rules.

On Dreamcast, vector interpolation, quaternion normalization and slerp,
trigonometry, reciprocal square roots, and quaternion matrix construction use
SH4ZAM without loading or changing XMTRX. Host tests use a scalar path with the
same checked contract.

The layer allocates nothing, creates no thread, and performs no work unless
called. Its only mutable state is an optional cursor supplied by the caller.
