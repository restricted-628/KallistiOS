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
invalid skinning input therefore leaves all output untouched.

## Execution and ownership

The functions allocate no memory, create no thread, retain no state, and do
not load or alter XMTRX. The Dreamcast implementation uses SH4ZAM one-off
point/vector transforms and reciprocal-square-root normalization. Skeleton
hierarchy traversal, pose evaluation, animation clocks, palette construction,
model binding, and submission remain caller policy.
