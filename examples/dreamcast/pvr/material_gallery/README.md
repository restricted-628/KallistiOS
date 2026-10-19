# 3D material gallery

`make` builds `material-gallery.elf`; `make run` uses the configured loader.
Procedural textures and geometry require no external assets or ports.

Build with assertions enabled; `NDEBUG` is rejected because assertions check
rendering calls as well as their results.

The four panels demonstrate:

- **Environment map:** view-normal UV generation on a rotating ellipsoid,
  sampling a painted studio texture. This is not a live scene reflection.
- **Bump lighting:** a projected planar brick panel and a procedural tangent-
  space normal-angle texture. Light azimuth rotates in the panel's tangent
  frame. Bump lighting changes shading, not geometry or the silhouette.
- **Lightmap:** a UV-space illumination texture multiplies a lit sphere.
  The texture is authored procedurally, not baked global illumination.
- **Emission:** an unlit circuit pattern adds to the shaded surface, including
  its dark side. This is bounded-color addition, not bloom or HDR.

Projection, inverse-transpose normals and lighting use the existing geometry
APIs and SH4ZAM-backed target implementation. Direct trigonometry and bump-light
packing use SH4ZAM. Opaque material recipes seed OP and complete on presorted TR
with exact-depth matching; each layer reuses bit-identical projected positions.
These are the real recipe compilers, not independently approximated effects.
This example deliberately does not exercise translucent secondary-buffer
recipes, whose presorted Flycast limitation is documented in `material_recipes`.

A toggles environment mapping; B toggles bump lighting; X toggles both auxiliary
layers; Y pauses rotation; Start exits. Disabled panel labels are dimmed.
The meshes remain inside their panels, so this is not a general clipping demo.
All buffers and resources are application-owned with no per-frame allocation.

`make smoke` builds `material-gallery-smoke.elf`: 48 frames exercise all effects
and their bypasses, verify projection counts, identical layer geometry, and a
clean PVR fault record. It is not a pixel oracle, controller test, physical-
console certification or benchmark. Screenshot review is a separate check.
