# Keyframe animation contract

`dc/animation.h` provides format-neutral interpolation and transform math. It
does not retain a clip, advance a clock, choose looping behavior, dispatch
events, bind a model, or allocate pose memory.

## Track admission and sampling

An `anim_track_t` is a bounded strided view over scalar, four-component vector,
or WXYZ quaternion keys. `anim_track_open()` validates the complete address
range, supported value and interpolation kinds, finite key data, nonzero
quaternions, and strictly increasing finite times before publishing an
immutable view. The source must not change while the view is in use.

Sampling clamps time before the first key and after the last key. Times inside
the track use binary search, so admitted tracks do not pay a linear validation
or interval-search cost every frame. Step interpolation publishes the lower
key. Linear scalar and vector tracks interpolate components; quaternion tracks
normalize their inputs and take the shortest spherical path.

The sampler intentionally does not wrap or mirror time. Those are playback
policies: a caller may map its clock into the admitted start/end interval before
sampling.

## Object transforms

`anim_transform_tracks_t` combines optional translation, rotation, and scale
views with a complete fallback transform. Missing channels retain fallback
values. `anim_transform_blend()` linearly blends translation and scale and
spherically blends rotation between two sampled poses.

`anim_transform_matrix_build()` converts one transform to an explicit
column-major `translation * rotation * scale` matrix. The resulting matrix can
be copied into a compact-model hierarchy node and composed by
`pvr_chunk_hierarchy_traverse()`.

On Dreamcast, vector interpolation, quaternion normalization and slerp,
trigonometry, reciprocal square roots, and quaternion matrix construction use
SH4ZAM without loading or changing XMTRX. Host tests use a scalar path with the
same checked contract.
