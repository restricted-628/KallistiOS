# Independent Compact UV source sections

PUV1 stores independent per-reference UV coordinates and associations with PML1
material-layer entries. It complements the existing caller-owned runtime UV
source and ordinary prepared-cache paths; it does not change Compact model
streams, PML1 bytes, or cache packet layouts.

One source describes every ordinary source-strip reference of one ordered
model view. Coordinates are in source order, before reversed-strip winding
correction. Repeated position indices can therefore have different coordinates
at seams. Values are finite IEEE binary32 pairs and may be negative or repeat
outside `[0,1]`.

Each binding selects a source for one PML1 entry ordinal. Several entries can
share one source, and one model can have several sources. A bound entry's affine
rows operate on the selected independent coordinates; an unbound entry retains
canonical coordinates. PML1's own UV-source byte stays zero. PUV1 is the explicit
additional association, not a reinterpretation of standalone PML1.

## Admission and explicit runtime binding

PCM2 section type 17 (`PVR_CHUNK_ASSET_SECTION_UV_SOURCES`) must carry
`PVR_CHUNK_ASSET_SECTION_REQUIRED`. Its feature is
`PVR_CHUNK_ASSET_FEATURE_UV_SOURCES`; assets also containing PML1 require
`PVR_CHUNK_ASSET_FEATURE_MATERIAL_LAYERS`. Consumers must acknowledge both.
Acknowledgment only checks understood section requirements: it does not decode
or validate the UV payload or its model/layer relationships.

Geometry-only and PML1-only scene loaders reject this required
feature with `ENOTSUP`, before geometry decoding. They must not silently render
with canonical UVs instead.

`pvr_chunk_scene_asset_load_layers_uv()` admits scenes with both associations.
It requires exactly one raw, directly readable PML1 and PUV1 section, validates
their framing before invoking a geometry decoder, and validates all UV/model/
layer relationships before publishing the hierarchy. Missing metadata reports
`ENOENT`, duplicates `EILSEQ`, and compressed/non-direct metadata `ENOTSUP`.
The existing scene workspace query is unchanged: metadata views borrow the
immutable asset bytes, while UV expansion remains explicit. Decode only sources
that will be used, and share them among the corresponding layers.

Both metadata outputs are preserved on failure. If a geometry decode or
model-dependent semantic check fails after loading begins, model/node outputs
are cleared and no hierarchy is published; decoder writes to workspace may
remain. Aliased UV metadata outputs are rejected before writing them or decoding
geometry. No textures, passes, runtime UV arrays or scene lifecycle are created
by loading. A caller choosing this API must consume the returned associations
when rendering. Auxiliary glTF material import remains separate work.

For a scene, the new loader performs steps 1-3 below for directly readable
metadata and the existing geometry workspace. For custom section consumers,
the equivalent explicit load-time sequence is:

1. Open the PCM2 container, acknowledge its required features, and decode its
   sections into disjoint caller-owned storage using the section codec APIs.
2. Open the PML1 and PUV1 sections and admit the ordered model views.
3. Call `pvr_chunk_uv_section_validate_layers()` with those views. It rechecks
   both sections, all layer ranges, full-model source coordinate counts, and
   each bound layer's model identity. Texture and recipe validation is separate.
4. For a layer entry, use `pvr_chunk_uv_section_find()`. `ENOENT` selects the
   canonical path. Otherwise query that source's identity/count with
   `pvr_chunk_uv_section_source_get()`, then decode its coordinates with
   `pvr_chunk_uv_section_decode()`.
5. Bind decoded coordinates with `pvr_chunk_uv_source_init()` and use the
   existing UV-aware renderer or prepared-cache builder. Apply the selected
   layer's filtering and affine/material policy through their existing hooks.

Decoded sources can be shared among layers. They cost eight bytes per reference
plus the runtime strip index (twelve bytes per strip on SH-4), all supplied by
the caller. No allocation, texture pin, worker, or manager is created by this
codec. A prepared cache bakes the coordinates and no longer borrows that source.

Admitted section views borrow immutable bytes. Indexed access and decoding do
not repeatedly rescan CRCs; do not fabricate views or mutate their backing
storage. The semantic validator permits unused sources but validates them too.
Writers preflight all inputs before any output write. Decoder failures preserve
the destination. Output aliases with views or encoded bytes are rejected.

## Wire layout

All integer fields and binary32 bit patterns are little endian. There is no
native-struct serialization or trailing padding. Both CRCs use reflected
polynomial `0xedb88320`, initial accumulator `0xffffffff`, and final complement.

| Header offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `PUV1` |
| 4 | 2 | Version 1 |
| 6 | 2 | Header size 40 |
| 8 | 4 | Exact total size |
| 12 | 4 | Nonzero source count S |
| 16 | 4 | Nonzero binding count B |
| 20 | 2 | Source stride 16 |
| 22 | 2 | Binding stride 8 |
| 24 | 2 | UV-pair stride 8 |
| 26 | 2 | Reserved, zero |
| 28 | 4 | Nonzero total UV-pair count U |
| 32 | 4 | CRC32 over bytes 40 through end |
| 36 | 4 | CRC32 over bytes 0 through 35 |

The exact size is `40 + 16*S + 8*B + 8*U`, representable in both uint32 and
`size_t`. Tables occur in that order immediately after the header.

Each 16-byte source entry contains four uint32 fields: model ordinal, first
global UV-pair index, nonzero pair count, and reserved zero. Source ranges must
be contiguous in table order, starting at zero and consuming exactly U pairs.
Overlaps, gaps and reordered payload ranges fail admission.

Each eight-byte binding contains a uint32 PML1 entry ordinal followed by a
uint32 PUV1 source ordinal. Bindings strictly increase by layer ordinal, with
no duplicates. Source ordinals must be in range. Several bindings may name the
same source.

Each eight-byte coordinate is binary32 U followed by binary32 V. NaNs and
infinities fail admission. Binary32 storage preserves the independent UV values
rather than forcing them through the model stream's signed fixed-point grid.

## Validation scope

`utils/pvr-chunk-uv-asset-test` covers an independent fixed-CRC golden, unaligned
input, every truncation and single-byte corruption, repaired-CRC malformed
fields, multiple/shared sources, lookup gaps, output preservation and aliases.
It also validates actual PML1/model relationships, decodes coordinates, and
checks the real renderer's output UVs across reversed strips and shared IDs.

The scene suite checks both rejection by unsupported loaders and coherent
UV-aware publication/rollback, including metadata aliases and a decoder that
writes before failing. These numerical and admission tests are not texture
image, importer-conformance, presort, or physical-hardware proof.
