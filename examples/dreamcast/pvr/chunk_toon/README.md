# Compact-model surface showcase

`make` builds `chunk-toon.elf`, an interactive side-by-side showcase. Load the
ELF in Flycast or with the usual Dreamcast loader. It runs until Start is
pressed; without a controller, model and light motion continue automatically.

Build with assertions enabled; `NDEBUG` is rejected because assertions check
rendering calls as well as their results.

In toon mode, the left panel uses two bands. The right panel selects two through five
bands. Both use the same model, pose, light, palette, equation and outline width
so the effect of adding thresholds is visible directly. Shading boundaries
split triangle interiors through the real prepared Compact toon emitter, not
a texture lookup or a mock renderer.

| Control | Action |
| --- | --- |
| L/R triggers | Previous/next rendering style |
| A | Cycle 2–5 toon bands and specular exponents 8, 16, 32, 64 |
| B | Cycle sea-green, warm, and grayscale palettes |
| X | Switch between a closed torus and sphere |
| Y | Cycle dot, inverted-dot, and half-Lambert shading |
| D-pad up/down | Adjust outline width (zero disables it) and wire pixel width |
| D-pad left | Pause/resume model rotation |
| D-pad right | Pause/resume the moving directional light |
| Start | Exit and release resources |

`make styles` builds `chunk-styles.elf`, starting in diffuse mode. Both entry
points share the same viewer and can cycle through all five styles:

| Mode | Left panel | Right panel |
| --- | --- | --- |
| Toon | Two bands | Selected band count |
| Diffuse | Unlit material color | Ambient + directional diffuse |
| Specular | Diffuse only | Diffuse + additive specular |
| Wireframe | All triangle edges | Diffuse surface with depth-tested edges |
| Outlined | Diffuse only | Same diffuse surface + expanded-shell outline |

Smooth shading uses the existing Compact render-policy binding, the extended
vertex-lighting API, and PVR Gouraud interpolation. Specular is vertex-based
Blinn-Phong using SH4ZAM's approximate `shz_powf`, not a per-pixel shader or a
libm accuracy comparison. Both panels share material color and lighting. A
small white texture makes the PVR textured/offset-color path display the
highlight without adding a surface pattern; an untextured header would not.

Wireframe uses the prepared-cache wire emitter with unculled screen-space
line quads. The meshes consist of independent three-vertex strips, so shared
edges are submitted twice; this is not a silhouette or mesh-global unique-edge
extractor. The right panel has a small homogeneous projection scale bias to
bring wires ahead of their coplanar surface while retaining depth occlusion.
This demonstration bias is not a general hidden-line robustness guarantee.

The torus has 288 vertices / 576 triangles; the sphere has 266 vertices / 528
triangles. Geometry and smooth normals are generated with SH4ZAM at startup.
The admitted draw caches and their storage persist throughout the demo. Each
frame uses SH4ZAM rotation, a matching normal transform and homogeneous perspective
projection. Toon uses geometric band splitting; outlines use oppositely culled
expanded shells. No per-frame heap allocation is performed. Shell width is in model
units, not constant screen pixels. This example does not demonstrate textured artwork,
skinning, or two-volume shading.

The HUD uses a BIOS-font texture, refreshed only when settings change and after
the previous render has completed. Store-queue ownership, scene submission and
resource lifetime remain with KOS; there is no custom math fallback or runtime
backend switching.

## Automated and focused tests

`make styles-smoke` builds `chunk-styles-smoke.elf`. It draws each style with
each mesh for 12 frames (120 total), checks submission, nonzero generated
specular colors, and a clean pipeline
status, and prints `RESULT: PASS (styles showcase; ...)`. The smooth-shading
path relies on these fixed-size demo meshes remaining inside the frustum;
it is not an arbitrary-model clipping example. The test does not assert
pixel colors, performance, controller input, or physical Dreamcast correctness.

`make smoke` builds `chunk-toon-smoke.elf`. It runs 480 frames through both
meshes, all band counts, all palettes/equations, and outline-disabled/enabled
states. It asserts source-triangle counts, generated band vertices and a clean
pipeline status, then prints `RESULT: PASS (toon showcase; ...)`. This checks
execution and geometry submission, not framebuffer correctness or hardware
performance. Controller-button behavior still needs testing with a controller.

`make triangle` preserves the original focused example as
`chunk-toon-triangle.elf` from `chunk-toon.c`. It runs 240 frames and then shows
the original green PASS screen. Its smaller test case is described below.

## Original triangle fixture

This example builds an ordinary compact-model draw cache and admits it once
with `pvr_chunk_model_cache_draw_prepare()`. Each frame uses
`pvr_chunk_model_cache_draw_emit_toon()` and
`pvr_chunk_model_cache_draw_emit_outline()` without rescanning immutable cache
data or copying unchanged deformations. Dynamic lighting and generated
geometry remain checked. The source triangle has
smooth normals on opposite sides of one threshold. Its moving directional
light therefore creates a hard color boundary that crosses the triangle
interior instead of merely changing the three original vertex colors.

Before the band pass, the example emits the same prepared cache as an expanded
dark shell. This single open triangle deliberately keeps culling disabled so
both sides demonstrate the geometry. A closed model should submit an outline
header with the ordinary surface pass's opposite culling mode, leaving only
the enlarged back faces visible around the final surface.

The model contains no renderer-specific record. A caller-owned profile selects
the scalar equation, threshold, and two packed color modulations. Caller-owned
work arrays retain the assembled positions, transformed normals, scalar shades,
at most three band triangles, and the established frustum-clipping workspace.
The prepared cache, material header, scene, list, and all memory remain under
application control; no per-frame allocation or global matrix state is used.
The deformation scratch is reserved for callers that supply a resolver; this
example borrows its immutable base deformations directly from the cache.
