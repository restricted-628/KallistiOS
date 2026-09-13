# Material composition fixture

Build with the KOS environment loaded:

```sh
make
make run
```

No external assets are needed. The program creates a constant-color ARGB4444
mip chain and constant-angle bump texture through checked texture-surface
APIs. Four panels demonstrate opaque/translucent trilinear (top row) and
opaque/translucent bump shading (bottom row). Two dark vertical bars cover
the opaque panels; their color must not change during recipe completion.

For lightmap (top) and emissive (bottom) composition instead:

```sh
make clean
make LAYERS=1 VERIFY_PIXELS=1
make run
```

This mode uses the same Compact table/resolver and geometry. Identifier 19
selects an ARGB4444 layer with RGB (6/15,5/15,4/15) and alpha 1/15. Layer alpha
must be ignored: the lightmap uses vertex alpha 255 and the emission uses
vertex alpha zero. Both use unlit white vertex RGB and zero offset color.
Expected opaque/translucent RGB8 centers are (82,34,14)/(55,42,43) for the
lightmap and (255,187,119)/(148,124,99) for emission. Emission saturates before
the final surface-alpha blend. This is not HDR or opacity-independent glow.
Rebuild with `make clean` when changing mode variables; the default remains
the trilinear/bump fixture. Presort, pixel verification and emulator diagnostic
caveats below apply to both modes.

Validation: interpreter and dynarec both pass exact packet comparisons and
five of seven presorted pixel checks in layered mode. Both translucent panels
remain white in that path. The separate `LAYERS=1 AUTOSORT_DIAGNOSTIC=1`
Vulkan per-pixel diagnostic passes all seven samples; it does not establish
general ordering or physical hardware behavior.

The application binds the color surface to Compact texture identifier 7 and
the bump surface to identifier 19. It resolves their contexts through
`pvr_chunk_material_resolve_context()` and feeds those into the existing
recipe compilers. Before rendering, all four recipes' TA headers, roles and
list metadata must match an independent explicit-context construction. The
packet-comparison PASS marker is separate from submission and image checks.
In layered mode, caller-owned `pvr_chunk_material_layer_t` descriptors instead
carry identifier 19, independent sampling, the lightmap/emission role and tint.
Four procedural model/source-strip associations are serialized into PML1,
opened and found by ordinal before copying each decoded layer for preparation.
This exercises the real codec on target, not automatic PCM2/glTF ingestion.
`pvr_chunk_material_resolve_layer()` compiles the recipes, and
`pvr_chunk_material_layer_prepare_vertex()` maps auxiliary UVs with
`u'=2u+.25`, `v'=.5v-.25` while preserving command/position/depth bit-for-bit.
Assertions check these values and the required alpha against the explicit
fixture expectations. Constant textures isolate composition; this does not
test a varying-texture seam or interpolation image.

Both texture allocations remain alive until the final render completes.
This demonstrates caller-selected material inputs, not a new asset format or
automatic shader-role selection.

The backdrop is RGB (.1, .2, .3), surface RGB is (.8, .4, .2), surface alpha
is 8/15, and the bump light factor is 128/255. Expected RGB8 panel centers:

| | Opaque | Translucent |
| --- | --- | --- |
| Trilinear | 204,102,51 | 121,78,63 |
| Bump | 102,51,26 | 67,51,49 |

Equal-color mip levels intentionally isolate complementary phase accumulation
from LOD selection. This is not a test of fractional LOD selection accuracy or
varying normal-map interpolation. The final image remains for 30 seconds.

For numeric framebuffer verification (RGB565 with an eight-unit RGB8 tolerance):

```sh
make clean
make VERIFY_PIXELS=1
make run
```

The checker reports all seven samples, then asserts on any mismatch. It uses
the displayed framebuffer query, not assumptions about a PVR buffer address.
Emulators must actually update emulated framebuffer memory for this mode.
Flycast requires `rend.EmulateFramebuffer=yes`; Vulkan per-pixel rendering
is selected by `pvr.rend=5`. The CPU interpreter/dynarec choice is independent.

**Known emulator limitation:** Flycast bypasses its secondary-buffer resolver
for presorted transparency. The production-order fixture consequently fails
its translucent color checks there, even with the per-pixel renderer selected.
Do not interpret its separate submission/PVR-fault PASS marker as image proof.
See the [recipe guide](../../../../doc/pvr-material-recipes.md).

The September 10 Vulkan regression run also measured opaque trilinear red at
213 rather than 204, just outside the unchanged tolerance of eight. The prior
explicit-context example produced the identical seven samples under the same
settings. This image discrepancy remains open independently of the new
resource-resolution packet comparisons.

For an emulator-only comparison of secondary-buffer algebra:

```sh
make clean
make VERIFY_PIXELS=1 AUTOSORT_DIAGNOSTIC=1
```

That mode enables autosort and prints a diagnostic warning. The separated
quads let it test numeric composition, but it does **not** validate general
ordering, intersecting surfaces, or physical-console behavior. The ordinary
build remains presorted; no production contract is changed to accommodate
the emulator.
