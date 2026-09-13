# Compact scene admission tests

`make test` uses the shared GNU17 host policy. Strict GCC C23 and Clang C2x lanes
are selectable with `HOST_CSTD` and `HOST_PEDANTIC=-pedantic`. After sourcing the
KOS environment, `make dreamcast` builds the same assertions for SH-4.

The suite covers hierarchy/model-table serialization, scene materialization,
shared decoded geometry, deferred-control rejection and required material/UV
metadata. The UV-aware cases use a two-model PCM2 scene containing PML1 and PUV1,
then select, decode and bind a returned UV source to the loaded model. Geometry
sharing must remain intact and workspace requirements must not grow to reserve
UV arrays automatically.

Failure cases cover inner/outer checksums, missing/duplicate/compressed metadata,
wrong UV/model/layer identities, coordinate counts, short workspace, metadata
output aliases, null output and a decoder which writes before returning EIO.
Preflight failures preserve model/node arrays and never invoke that decoder;
late failures clear those arrays and publish no hierarchy. Metadata outputs and
input asset bytes must remain untouched on every failure.

The test decoder deliberately copies bytes marked as compressed to exercise
workspace sharing and decoder failure handling. It is not an LZ4 conformance
test. Host hardware-submit stubs fail if scene admission attempts to render.
Emulator assertions establish target numerical/admission behavior, not texture
images, import fidelity or physical-hardware behavior.
