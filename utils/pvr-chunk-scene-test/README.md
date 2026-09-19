# Compact scene admission tests

`make test` uses the shared GNU17 host policy. Strict GCC C23 and Clang C2x lanes
are selectable with `HOST_CSTD` and `HOST_PEDANTIC=-pedantic`. After sourcing the
KOS environment, `make dreamcast` builds these assertions plus the real KOS
texture/material integration checks for SH-4.

The suite covers hierarchy/model-table serialization, scene materialization,
shared decoded geometry, deferred-control rejection and required material/UV
metadata. The UV-aware cases use a two-model PCM2 scene containing PML1 and PUV1,
then select, decode and bind a returned UV source to the loaded model. Geometry
sharing must remain intact and workspace requirements must not grow to reserve
UV arrays automatically. The same PCM2 fixture packages two RGB565 images in
PTX1, with a shared base/layer image and a distinct auxiliary image. Host checks
admit the complete image payloads and verify missing-image, corruption, shared
identifier and unused-image behavior before any GPU allocation.

Failure cases cover inner/outer checksums, missing/duplicate/compressed metadata,
wrong UV/model/layer identities, coordinate counts, short workspace, metadata
output aliases, null output and a decoder which writes before returning EIO.
Preflight failures preserve model/node arrays and never invoke that decoder;
late failures clear those arrays and publish no hierarchy. Metadata outputs and
input asset bytes must remain untouched on every failure.

On Dreamcast, the test explicitly initializes PVR, allocates and uploads those
images, builds the texture table, resolves lightmap/emissive recipes, and uses
the actual UV renderer to write a memory sink. Literal UV/color expectations
and base-versus-layer geometry/depth checks precede direct/cache packet
comparison. Cache values must survive changes to the decoded UV source.
Missing bindings preserve the recipe, and explicit surface release must restore
the free VRAM count measured after the allocator's one-time aligned arena setup.
No allocator reset is used to hide leaks. All calls use the built KOS library,
not host binding mocks. PVR is shut down afterward.

The test decoder deliberately copies bytes marked as compressed to exercise
workspace sharing and decoder failure handling. It is not an LZ4 conformance
test. Host hardware-submit stubs fail if scene admission attempts to render.
The target fixture uploads textures but submits no geometry to the GPU: it
checks numeric packets and resource cleanup, not framebuffer composition,
presort behavior, import fidelity or physical-hardware behavior.
