# Compact PVR Model Converter

`pvr-model-convert` converts a deliberately bounded Wavefront OBJ or glTF 2.0
source into the little-endian streams consumed by KOS's compact-model runtime.
The generated model and every optional section are admitted by the exact
runtime validators before temporary output is published.

```text
pvr-model-convert [--flip-winding] [--flip-v] [--join-strips] \
    [--texture-id ID | --material NAME=ID ...] \
    [--material-library FILE ...] \
    [--emit-c SYMBOL | --emit-asset [--section-directory \
     [--scene-root] [--rigid-skin] [--morph-target DX DY DZ] \
     [--animation-offset DX DY DZ]] \
     [--lz4-vertices]] [--] \
    INPUT.{obj,gltf,glb} \
    {VERTICES.bin POLYGONS.bin | MODEL.c | MODEL.pcm}
```

glTF and GLB input require `--emit-asset --section-directory`. The host-only
importer uses cgltf and adds no parser, allocation, or code to a Dreamcast
program. It accepts one selected scene and any nonzero set of unique
meshes, including repeated instances, triangle primitives, indexed or non-
indexed positions, optional normals and UV set zero, stable base-color texture
ordinals, parent-before-child node transforms, and one STEP/LINEAR translation,
rotation, or scale animation. Each unique mesh becomes one PCM2 model ordinal
with exact local bounds and its own resource manifest, general-N skin,
inverse-bind skeleton, and sparse position/normal morph sections when authored.
Different meshes may use different skins; repeated instances of one mesh must
agree on that mesh's skin. General-N weights are normalized exactly to 65535;
no four-influence reduction is performed.

Unsupported authored meaning is rejected rather than dropped. Current explicit
boundaries include required extensions, GPU instancing, non-triangle primitives,
tangent/color/custom vertex attributes, texture
coordinate sets other than zero, texture transforms, transparent or advanced
materials, detached skin joints, conflicting skins on repeated mesh instances,
multiple animations, morph-weight
animation, matrix-authored nodes in an animated scene, and cubic-spline tracks.
The latter use Hermite tangents and are not silently mapped to the runtime's
Catmull-Rom interpolation. PBR base color, metallic, and roughness are converted
deterministically into compact diffuse, ambient, specular, and exponent state;
texture images and VRAM placement remain application resource policy.

The admitted source subset is:

- finite `v X Y Z` positions;
- finite `vt U V` coordinates;
- finite, nonzero `vn X Y Z` normals;
- triangulated faces using `v`, `v/vt`, `v//vn`, or `v/vt/vn`; and
- positive or relative-negative OBJ indices.

Every corner of a triangle must use the same attribute form. Object, group,
material-library, and smoothing declarations are accepted because they do not
alter this geometry stream. Faces with more or fewer than three vertices are
rejected; source triangulation policy stays in the content tool that owns the
original mesh.

UVs use the smallest admitted representation that preserves the compact
format's range policy. The converter first chooses signed 6.10 fixed point
when every corner fits its approximately `[-32, 32)` range, then signed 8.8
fixed point when every corner fits approximately `[-128, 128)`. Coordinates
outside both fixed-point ranges use the finite 32-bit floating-point escape
record rather than being clamped or rejected. The escape doubles the UV words
per texture set, so ordinary content retains the denser fixed-point path.
Normals are normalized and quantized to signed 16-bit components. `--flip-v`
applies `V = 1 - V`, while
`--flip-winding` exchanges the second and third corner of every triangle. No
coordinate-system transform is implicit. Models with UV-bearing faces require
resolved 13-bit texture identifiers, and every face must then carry UVs. A
single-texture model uses `--texture-id ID`. Multi-material input instead uses
one `--material NAME=ID` for every `usemtl NAME` selected by a face. Names are
single OBJ tokens. Bindings that resolve different names to the same identifier
are coalesced, and the polygon stream emits a texture record only when the
resolved identifier actually changes. `--texture-id` and `--material` are
mutually exclusive.

One or more explicit `--material-library FILE` options add persistent color and
specular state. Files use a strict material-library subset:

- `newmtl NAME` begins one uniquely named definition;
- required `Kd R G B` supplies diffuse color;
- optional `Ka R G B` supplies ambient color; and
- `Ks R G B` and `Ns VALUE` must either both be present or both be absent.

Color components must be finite values in `[0, 1]` and are rounded to 8-bit
channels. `Ns` must be in `[0, 1000]` and is rounded linearly into the compact
runtime's `[0, 16]` specular-exponent field. Duplicate properties, duplicate
names across supplied files, and every unsupported directive are rejected.
Opacity, illumination-model selection, texture-map paths, blending, and render
list choice therefore remain explicit application/content-pipeline policy.

Supplying a material library requires every face to follow a `usemtl` selecting
a complete loaded definition. Untextured models need no `--material` bindings;
textured models compose the library with the existing name-to-ID bindings.
Material records are emitted only when the selected definition changes. OBJ
`mtllib` declarations are never opened implicitly: the command line is the
sole authority over host file access and library ordering.

By default, arbitrary triangle topology is represented as three-vertex strips,
preserving the converter's original byte stream. `--join-strips` performs a
bounded, order-preserving optimization over adjacent source faces. A face joins
only when its complete position/UV/normal corner identities match the next edge
required by alternating triangle-strip winding. The optimizer never reorders
faces and never crosses an attribute form, selected material, or resolved
texture-state boundary, so transparent draw order remains source order. This
deliberately excludes global graph stripification and topology guessing.

Joined strips split before either the 15-bit vertex count or the enclosing
16-bit record payload would overflow. Consecutive strips with compatible state
then share bounded strip records. The report includes `strips_before`,
`strips_after`, and `triangles_joined` so a build can measure the actual
reduction. Calculated center/radius metadata and stream sizes are also emitted
as deterministic `key=value` records.

Without `--emit-c`, the two positional outputs remain raw little-endian streams.
`--emit-c SYMBOL` instead accepts one output path and writes a C11 translation
unit containing naturally aligned private stream arrays plus one externally
visible immutable `pvr_chunk_model_t SYMBOL`. The symbol must begin with a
letter, contain only letters, digits, and underscores, avoid C keywords, and be
at most 31 characters. The generated source contains no input paths and uses
exact hexadecimal floating constants for center/radius metadata. Declare it in
consumer code with, for example:

```c
#include <dc/pvr_chunk_model.h>

extern const pvr_chunk_model_t ship_model;
```

`--emit-asset` writes the small PCM1 two-stream container by default.
`--lz4-vertices` independently compresses its vertex stream with a checksummed
LZ4 Frame. `--section-directory` instead writes PCM2, retaining the same two
required streams in an extensible checksummed directory. A two-stream PCM2 is
larger than PCM1 and is intended as the host-pipeline base for optional
resource, deformation, hierarchy, animation, collision, cache, or application
sections; models which need none of those should keep PCM1.

Every converter-produced PCM2 also carries one pointer-free `PMT1` model
table. Each record associates a model ordinal with its exact local bounding
sphere and the typed optional-section ordinals owned by that model. The
converter reopens and cross-validates this table against the complete PCM2
directory before publishing the asset. Optional resource, cooked-cache,
general-skin, skeleton, and morph sections are resolved independently per
model; section ordinals remain correct when only some models own a given
section type. Target-side table query and load calls use caller-owned workspace
and allocate nothing.

`--cooked-cache` adds a pointer-free `PCC1` ordinary prepared-cache section to
PCM2. The converter builds it through the target cache planner, serializes it,
reopens it, and materializes it before publishing the asset. It is opt-in
because it trades additional file size for avoiding compact-stream traversal
and indexed vertex assembly after loading. Runtime storage remains caller-owned
and texture identifiers are rebound normally; no host pointer, `size_t`, or
VRAM address is stored. The option requires both `--emit-asset` and
`--section-directory`.

If an admitted polygon stream contains compact volume records, PCM2 also
receives a pointer-free `PVL1` volume-data section. The host tool preserves the
original triangle, quad, strip, winding, and user-word representation, reloads
it through the target parser, and verifies every index against the model. No
section is emitted when the model has no volumes.

When a polygon stream references textures, PCM2 automatically receives a
pointer-free `PRT1` resource manifest. It contains the unique sorted 13-bit
identifiers and whether each appears in primary or secondary texture state.
The converter reloads the section and proves it exactly matches the admitted
model. Paths, material names, surfaces, and VRAM addresses remain outside the
runtime asset, and an untextured model receives no resource section.

`--scene-root` adds one admitted hierarchy section to PCM2. Its root has an
identity local transform and references model ordinal zero, proving the same
host-IR, serialization, generic section-loading, and target binding path that
multi-node scene importers use without inventing a second runtime hierarchy.
The option deliberately requires `--section-directory`; it is a pipeline
integration primitive, not an implicit scene graph wrapped around ordinary
PCM1 output.

`--rigid-skin` adds one admitted `PSG1` general-skin section to PCM2. Every
admitted model vertex receives one full-weight influence from joint zero. This
is an integration fixture for the host serializer, generic section loader,
target parser/materializer, and existing variable-influence runtime binder. It
does not infer an authored skeleton from OBJ and is not a replacement for a
scene importer that owns real joint and weight data. The option likewise
requires `--section-directory`.

When that fixture is paired with `--scene-root`, PCM2 also receives a `PSK1`
skeleton section mapping joint zero to the hierarchy root with an identity
inverse bind. Authored glTF skins use the same section for every joint. The
runtime materializes those bindings into caller-owned records and builds the
existing position/normal skin palette from completed hierarchy world matrices;
the asset format owns no pose, allocator, clock, or hidden matrix storage.

`--morph-target DX DY DZ` adds one admitted `PMS1` sparse morph-target
section to PCM2. The target applies the supplied finite position delta to
model vertex zero and leaves its normal unchanged. This is a narrow authoring
and integration primitive for the host serializer, generic section loader,
target parser/materializer, and existing model-aware shape binder; OBJ has no
morph-target grammar, so the option does not infer or import wider shape data.
Scene importers supply any number of sparse authored targets to the same
serializer. The option requires `--section-directory`.

`--animation-offset DX DY DZ` adds one admitted `PAT1` animation section to
PCM2. A two-key linear vector track moves the explicit scene root from zero to
the supplied finite offset over the clip interval `[0, 1]`. The target parser
materializes the clip into the existing animation track, transform, visibility,
sampling, blending, and playback runtime; the container adds no second clock
or interpolation engine. This bounded fixture requires `--scene-root`, while
scene importers supply wider authored clips to the same serializer.

Compile the generated file normally in the application build to produce the
target object; the converter does not invoke or choose a compiler. Each output
is replaced by an atomic rename after complete write and validation. A C output
is one atomic artifact, while the two raw files cannot form one
filesystem-atomic transaction.
