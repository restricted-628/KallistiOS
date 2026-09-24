# Compound material recipes

The checked material API can compile small, caller-owned bundles of ordinary
polygon headers. It does not own textures, transform geometry, allocate an
intermediate VRAM surface, select a scene, or schedule render passes. A recipe
step means **header followed by geometry**, not another frame/render pass.

The trilinear, bump, lightmap and emissive recipe compilers add the
relationships that individually valid material headers cannot express:
complementary filtering phases, initialization of intermediates, modulation,
final blending, matching depth tests, and required vertex interpretation.

## Supported profiles

| Surface | Steps | Composition |
| --- | --- | --- |
| Opaque trilinear | 2 | Opaque phase A; equal-depth translucent additive phase B |
| Translucent trilinear | 3 | Overwrite secondary with A; add B there; resolve to primary |
| Opaque bump | 2 | Opaque bump-light seed; equal-depth surface-color modulation |
| Translucent bump | 3 | Bump-light seed in secondary; surface modulation there; resolve |
| Opaque lightmap | 2 | Surface seed; equal-depth unlit lightmap multiplication |
| Translucent lightmap | 3 | Surface in secondary; lightmap multiplication there; resolve |
| Opaque emissive | 2 | Surface seed; equal-depth unlit emission addition |
| Translucent emissive | 3 | Surface in secondary; emission addition there; resolve |

Inputs currently require packed vertex colors, float UVs, ordinary OP/TR
polygons, no modifiers, no fog or color clamp, and default buffer selectors.
Trilinear needs compatible mipmaps. Bump textures must be nonmipmapped, and
their surface cannot itself request trilinear filtering. Unsupported combined
profiles are rejected rather than silently approximated. Sprites, two-volume
recipes, combined bump-plus-trilinear, and fog-aware compound shading are not
covered by these initial profiles.

Lightmap/emissive inputs may be ordinary textures or vertex-colored layers,
but neither input may request bump sampling or trilinear phases. They use the
same OP/TR list. Each input has its own checked supersampling mask, so resolve
both through `pvr_chunk_material_resolve_context()` and pass each context and
mask directly. No combined lightmap-plus-emission four-step recipe is implied.

Both inputs are validated before publication. Errors preserve the complete
output, and output/input overlap is rejected. Texture lifetime and retained
geometry remain the caller's responsibility. These functions are optional
library code; no startup work, service, heap allocation, or global state is added.

## RGBA accounting

Let A and B be the already shaded complementary trilinear RGBA samples.
Accumulating `S = A + B` before applying the surface's final blend factors
avoids blending two partially weighted alphas independently over the scene.
The first step overwrites S: its previous contents are never assumed zero.
The resolve reads S, not the resolve polygon's dummy vertex color.

Bump seed vertices instead use **black base RGB** and the existing packed
light coefficients in `oargb`. Decal shading plus forced seed alpha one gives
`S = (h, h, h, 1)`. Multiplying a shaded surface `(C.r, C.g, C.b, a)` by that
vector produces `(h*C.r, h*C.g, h*C.b, a)`. This is why a naive alpha-weighted
bump seed is not interchangeable: it can unintentionally attenuate alpha and
then apply the lighting factor again during final transparency blending.

`pvr_material_compile_lightmap()` seeds the shaded surface `(C,a)`, then
multiplies by `(L,1)`. The LIGHTMAP role requires **unlit RGB tint and vertex
alpha 255**, with independent layer UVs and zero offset color. Texture alpha
is disabled and MODULATEALPHA shading retains the vertex alpha, giving
`(C*L,a)`. White RGB tint uses the lightmap without additional attenuation.

`pvr_material_compile_emissive()` seeds the shaded surface and adds `(E,0)`.
The EMISSIVE role requires **unlit RGB tint and vertex alpha zero**, layer UVs
and zero offset color. The result is `(clamp(C+E,0,1),a)` before the final
surface blend. Texture alpha must not change the original opacity. This is
bounded-color emission, not HDR, bloom, or glow that ignores surface opacity.
The surface may already contain lit/specular RGB; the auxiliary step never
evaluates lighting or adds a second specular contribution.

Both layer compilers inherit the surface's geometry/depth/flat-or-Gouraud
policy and retain only the auxiliary texture description. No texture identity
is assigned a global role: the application chooses which draw input is a
lightmap or emission layer. Existing Compact models, prepared geometry,
caller-owned texture tables and UV callbacks provide those inputs. This does
not yet admit glTF emissive texture roles or add auxiliary texture-role metadata
to asset files. Authored unlit base-color materials are supported separately by
the Compact converter and standard policy bindings; they do not implicitly
create a multipass recipe.

Surface and bump steps may use different UVs and colors but must reproduce
the same positions, depth, winding and clipped coverage. Resolve vertices can
reuse either canonical vertex buffer. Existing cell, tile-map, geometry and
Compact emitters can provide that geometry; the recipe does not introduce a
second transform or deformation implementation. Prepare deformed/clipped
geometry once where possible, then replay it with the required attributes.

## Submission and depth

Use presort and preserve recipe order for each surface. Never batch all
objects' secondary seeds followed by all objects' modulation steps: another
object would overwrite a live intermediate. Do not interleave unrelated
secondary-buffer users. Surfaces that overlap within one recipe's geometry
also require decomposition into correctly ordered non-overlapping groups.

For opaque recipes, submit seeds on OP, then their completions on TR before
unrelated transparency. The seed writes depth. The completion uses EQUAL and
disables depth writes, so a closer opaque object remains an occluder.
Coplanar, independent surfaces with identical depth need caller
disambiguation. Translucent recipes keep the source comparison but disable
all depth writes. List boundaries and these ordering rules remain explicit.

The legacy context field `depth.write` actually encodes a write-**disable**
bit; use `PVR_DEPTHWRITE_ENABLE`/`DISABLE`. Likewise `blend.src_enable` and
`dst_enable` are secondary-buffer selectors, not generic blend enables.
Their descriptions were clarified without changing the ABI or encoding.

## Validation and current limitation

`utils/pvr-material-recipe-test` checks the eight profiles, shared validation,
preserved inputs, failure atomicity, alias rejection, buffer-selection and
depth bits, complementary filters, and an independent RGBA blend model.
Its target build uses the actual KOS packet compiler; host builds capture
the necessary fields through a test double.
Layer checks include independent sampling flags, neutral-alpha header state,
zero/partial/full opacity, zero/full layer intensity, saturation, colored-only
inputs and rejection/alias/output-preservation checks for both inputs.

The [procedural example](../examples/dreamcast/pvr/material_recipes/) also
offers numeric RGB565 framebuffer checks against expected surface colors and
opaque occluder bars. This is stronger than accepting a submission PASS marker.

Flycast's Vulkan per-pixel renderer does not apply its secondary-buffer
resolver to presorted TR geometry: the [draw routing](https://github.com/flyinghead/flycast/blob/0abac3465dc9547dca5f30f3352fee10b67e34b2/core/rend/vulkan/oit/oit_drawer.cpp#L434)
chooses the ordinary color path for that case. Its
[secondary-buffer resolver](https://github.com/flyinghead/flycast/blob/0abac3465dc9547dca5f30f3352fee10b67e34b2/core/rend/gl4/abuffer.cpp#L87)
also provides a useful independent check of selector/blend semantics, not a
substitute for physical hardware evidence. In the current presort fixture,
the translucent resolve can therefore draw its dummy white vertex color.
This is recorded as a failed image check, not a successful rendering test.

An explicitly labeled autosort diagnostic exercises the emulator's secondary
resolver on separated, equal-depth quads. It is not proof that arbitrary
recipe geometry preserves ordering on hardware, and it does not change the
production presort contract. Physical-console validation and broader compound
profiles remain open; this document does not claim full graphics closure.
