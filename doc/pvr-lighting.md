# PVR CPU lighting contract

`dc/pvr_lighting.h` supplies optional geometry preparation. It does not own a
scene, material, light list, model, texture, or output allocation, and it does
not run unless an application calls it.

## Normal transforms

Normals cannot generally use the object transform directly when nonuniform
scale is present. `pvr_normal_matrix_build()` checks the complete source matrix
and publishes the inverse-transpose upper 3x3 only when that transform is
finite and nonsingular. Its relative determinant test is independent of the
application's world-unit scale.

`pvr_normal_transform()` consumes a bounded, strided stream beginning with
`vector_t`, transforms and normalizes XYZ, and writes W=0. Exact canonical
in-place operation is supported. Shifted overlap is rejected before output is
changed because it could overwrite a later source element.

## Lights and colors

`pvr_lighting_apply()` consumes world-space position, unit normal, and linear
RGBA samples. A directional-light vector points from the surface toward the
light. Point lights use a caller-selected finite range and conventional
constant, linear, and quadratic attenuation. Zero range means unbounded.

The full lighting context is checked before the first packed color is written.
Per-sample failures leave a valid prefix reported in
`pvr_lighting_result_t`. Ambient and positive Lambert contributions accumulate
in linear RGB, multiply the source color, saturate, and are rounded to
`0xAARRGGBB`; alpha is copied from the source color and is not lit.

On Dreamcast, the batch uses SH4ZAM vector dot, normalization, and reciprocal
square-root primitives without changing XMTRX. Host tests exercise a scalar
implementation of the same public contract.
