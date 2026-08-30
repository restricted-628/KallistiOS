# Compact PVR Model Converter

`pvr-model-convert` converts a deliberately bounded Wavefront OBJ subset into
the two little-endian streams consumed by KOS's compact-model runtime. The
generated model is admitted by the exact runtime validator before either
temporary output is published.

```text
pvr-model-convert [--flip-winding] [--flip-v] [--join-strips] \
    [--texture-id ID | --material NAME=ID ...] \
    [--material-library FILE ...] \
    [--emit-c SYMBOL | --emit-asset [--section-directory] \
     [--lz4-vertices]] [--] \
    INPUT.obj {VERTICES.bin POLYGONS.bin | MODEL.c | MODEL.pcm}
```

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

Compile the generated file normally in the application build to produce the
target object; the converter does not invoke or choose a compiler. Each output
is replaced by an atomic rename after complete write and validation. A C output
is one atomic artifact, while the two raw files cannot form one
filesystem-atomic transaction.
