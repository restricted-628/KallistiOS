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

## Prepared light snapshots

When several samples or draw callbacks share unchanged lights, call
`pvr_lighting_prepare()` once at the light-update or batch boundary and reuse
the result with `pvr_lighting_apply_prepared()`. The extended equivalents are
`pvr_lighting_prepare_extended()` and `pvr_lighting_apply_extended_prepared()`.
The original checked APIs retain their signatures and context layouts.

```c
pvr_light_t light_storage[LIGHT_CAPACITY];
pvr_lighting_prepared_t prepared;

if(pvr_lighting_prepare(&prepared, light_storage, LIGHT_CAPACITY, &context) < 0)
    return -1;
/* Reuse prepared for each stream sharing this lighting state. */
if(pvr_lighting_apply_prepared(colors, color_capacity, &stream,
                               &prepared, &result) < 0)
    return -1;
```

Preparation validates the complete context, copies its values and light array,
and normalizes each directional vector using the same scalar expression and
public SH4ZAM primitive as the checked path. Each prepared apply retains
sample/output checks, point-light direction and attenuation, ordered light
accumulation, packed-color rounding, and valid-prefix error reporting. Extended
lighting retains signed intensities, specular approximation, and depth cueing.
It skips the repeated context scan and directional normalization.

The caller owns both the snapshot and backing array. Keep them immutable and
alive until all synchronous applies finish, separate from sample/output/result
storage. Source lights and context may change or expire after preparation;
reprepare from ordinary source data to adopt changes. Preparation and shading
must use the same floating-point mode. A failed preparation leaves both
destinations unchanged. Overlapping destinations/source data are rejected;
zero lights permit NULL backing storage. There is no allocation, global cache,
XMTRX change, or new scheduling/ownership requirement.

Backing storage costs `light_count * sizeof(pvr_light_t)` plus one descriptor
(`pvr_lighting_prepared_t` or `pvr_lighting_extended_prepared_t`). The full array
and context are copied once per preparation; stack use does not grow with the
light count unless the caller places its array on the stack. This is native
runtime state, not a serialized asset format or a portable host-prepared blob.
Dynamic light changes still need preparation on the target; stable scene state
can reuse a snapshot across frames.

Preparation has a cost, so reuse matters. The optional host diagnostic includes
validation, normalization, snapshot copies, and shading for 1, 4, 16, 64, and
256 references per snapshot with 1, 4, and 9 directional lights:

```sh
make -C utils/pvr-lighting-test bench
```

It checks exact packed-color parity and alternates path order. Host timings do
not establish Dreamcast throughput or a universal break-even point. The
`chunk_scene` workload demonstrates reuse at the real cached-reference callback
boundary and provides a complete-frame comparison variant.
