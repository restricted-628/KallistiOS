# PVR geometry deformation

The deformation API supplies reusable morph-target and linear-blend skinning
kernels without owning models, skeletons, animation state, memory, or PVR
scenes. Applications opt in by including `dc/pvr_deform.h` and providing all
streams, palettes, and output storage.

## Vertex and stream contract

`pvr_deform_vertex_t` contains one object-space position and normal. A
`pvr_deform_stream_t` may point at a tightly packed array or at application
records whose first member is that canonical vertex. The stride describes the
complete application record. Output is always a tightly packed canonical
array.

Both operations accept exact in-place input only when the source is a tightly
packed canonical array beginning at the output address. Shifted or partial
overlap is rejected. Morph deltas and skin influence streams must not overlap
output. Positions are published with W equal to one and normals with W equal
to zero.

## Morph targets

Each `pvr_morph_target_t` supplies one strided delta per base vertex and one
finite blend weight. Deltas are additive:

```
result = base + sum(target_delta * target_weight)
```

Target framing and complete address ranges are checked before output begins.
Base and delta values are checked when their vertex is processed. If a later
vertex is malformed or its arithmetic overflows, `deformed_vertices` reports
the valid prefix already published.

## Linear-blend skinning

Each `pvr_skin_influences_t` supplies up to four joint indices and weights.
Zero-weight slots are ignored, including their index. Active weights must be
finite and nonnegative, and at least one active weight must exist. The kernel
normalizes the active sum, so callers do not need to pre-normalize weights.

The palette has one point-transform matrix and one inverse-transpose normal
matrix per joint. Build normal matrices with `pvr_normal_matrix_build()` when
the corresponding point transform can contain nonuniform scale. Every matrix,
active weight, and active index is validated before the first output write;
invalid palette or influence input therefore leaves all output untouched.
Source vertex and arithmetic failures are checked while deforming and retain
the reported valid output prefix.

## Prepare a shared palette once per pose

Include `dc/pvr_skin_prepared.h` to opt into caller-owned SH4ZAM palette
storage. Allocate `pvr_skin_prepared_joint_t joints[joint_count]` with its
natural alignment, and prepare a `pvr_skin_prepared_palette_t` with
`pvr_skin_palette_prepare(&palette, joints, joint_count, &prepared)` after
sampling the current position and inverse-transpose normal matrices.

Preparation validates every matrix before writing any destination and imports
each joint once. The original KOS matrices are copied, not borrowed; changing
or freeing them does not change the prepared pose. The descriptor and imported
joint storage must remain intact and immutable until all applications finish.
Reprepare when the pose changes, then reuse it for every mesh sharing that
palette. A zeroed or hand-authored descriptor is not an admitted palette.
Failed preparation leaves both the descriptor and joint storage unchanged.

`pvr_skin_apply_prepared_palette()` and
`pvr_skin_apply_spans_prepared_palette()` use the same fixed-four and variable-
span streams as the checked APIs. They skip palette component rescans and
per-active-influence KOS-to-SH4ZAM imports. They still validate changing weights,
active indices, source vertices, capacities and overlaps, normalize the blend,
and preserve valid-prefix failure reporting. Output must not overlap the
prepared descriptor or joint array. Exact canonical in-place vertex processing
remains supported. Preparation and application both preserve XMTRX.

Compact skins can pass their canonical source arrays through
`pvr_deform_stream_t` and `pvr_skin_stream_t` (or `pvr_skin_span_stream_t` for
general skins). The `chunk_skin` example demonstrates this after sampling each
pose. No model format or existing `pvr_chunk_skin_apply()` contract changes.
Weight admission/normalization is still repeated; it is not part of palette
preparation. The extra imported storage and preparation have a cost, so reuse
counts and physical-hardware timings must guide performance decisions.

## Execution and ownership

The functions allocate no memory, create no thread, retain no state, and do
not load or alter XMTRX. The Dreamcast implementation uses SH4ZAM one-off
point/vector transforms and reciprocal-square-root normalization. Skeleton
hierarchy traversal, pose evaluation, animation clocks, palette construction,
model binding, and submission remain caller policy.
