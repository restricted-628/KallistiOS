# Serialized Compact material layers

PML1 associates an existing lightmap/emission layer with a contiguous range of
source strips in one model. A shared texture can have different roles, sampling,
UV transforms and tints in different draws. Texture identity is not material
identity. The codec, model-range admission, explicit PCM2 scene loading and
texture residency preparation share the existing Compact pipeline. Auxiliary
glTF import remains rejected until independent UV-set storage and renderer
selection are integrated. Required material meaning must not be silently
ignored by a generic loader.

## PCM2 admission

Section type 16 (`PVR_CHUNK_ASSET_SECTION_MATERIAL_LAYERS`) carries PML1 and
must set descriptor flag bit 0 (`PVR_CHUNK_ASSET_SECTION_REQUIRED`). Encoding
this section with zero flags is malformed. Unknown flag bits are malformed;
unknown required section types are unsupported. Older PCM2 parsers required
all descriptor flags to be zero, so they reject these assets rather than
silently dropping the layer. Existing assets and optional sections are unchanged.

`pvr_chunk_asset_open()` is structural inspection, not rendering admission.
Custom loaders call `pvr_chunk_asset_requirements_check()` and acknowledge only
features they actually consume. Ordinary model/pair loading and ordinary scene
loading reject required layers with `ENOTSUP` before decoding model streams.
Section loading remains available for custom consumers and tools.

`pvr_chunk_scene_asset_load_layers()` is the explicit scene entry point. It
requires exactly one raw/directly readable PML1 section, checks both container
and PML1 checksums, loads the existing shared geometry workspace, and validates
all model/strip ranges before publishing the hierarchy and borrowed layer view.
Missing, duplicate or compressed layer metadata is rejected. Invalid ranges
after geometry decode clear model/node outputs as in ordinary scene loading;
the layer output is preserved on every failure. There is no extra persistent
workspace, allocation or scene manager. The caller still chooses recipes and
pins auxiliary resources; accepting the layer-aware API promises to use them.

## Load and preparation

1. Open immutable decoded bytes with `pvr_chunk_layer_section_open()`. It
   verifies framing, CRCs, ordering, nonoverlapping ranges, reserved fields,
   finite affine UV rows, tint, sampler limits and texture-ID range.
2. Call `validate_models()` against the loader's ordered model-view array
   (PMT1 order for a multi-model asset, not polygon-section order). Referenced
   models are reopened once each; source strip bounds and unresolved execution
   requirements are checked before preparation.
3. Use `pvr_chunk_layer_section_validate_table()` for a fixed texture table, or
   `pvr_chunk_layer_section_prepare_residency()` to pin auxiliary identifiers
   through the existing adapter before starting a PVR list. Repeated identifiers
   reuse pins; on partial failure successful pins remain tracked for the normal
   binding release operation. PRT1 still describes only direct stream usage.
4. Find a layer by model/source-strip ordinal, copy it to caller-owned
   preparation data and use `pvr_chunk_material_resolve_layer()`. That helper
   checks actual surfaces and recipe/profile admission.
5. Replay identical canonical geometry through the recipe. For the layer step
   use `pvr_chunk_material_layer_prepare_vertex()`, preserving clipping,
   coverage, depth, material order and resource lifetime through completion.

Source strips are numbered before filtering, clipping or topology expansion.
Ordinary prepared caches preserve that order. Do not use a frame-dependent
emitted-strip counter as the association key. Bounds validation is not itself
proof of support for a two-volume, modifier or compound render profile.

Views and source bytes remain immutable. Indexed decode takes constant work;
range lookup is logarithmic and does not repeat CRC scans. Model validation is
a load-time scan. Missing associations report `ENOENT` without changing output.
Outputs may not overlap source bytes or views. Serialization validates the
whole input, ordering, capacity and aliases before writing. Nothing allocates,
starts a worker, owns a scene or implicitly pins a resource. Existing model and
cache structures do not grow.

Version 1 admits one textured lightmap **or** emission layer per source strip.
It uses canonical draw UVs plus an affine transform. Independent UV selectors
and stacked layer combinations are rejected, not approximated. Future changes
must be explicit; reserved bytes cannot silently change old content's meaning.
If conversion already baked a base-texture transform into canonical UVs, an
importer must derive the auxiliary mapping relative to those coordinates. It
cannot blindly copy an authored transform. Noninvertible mappings or different
UV sets need explicit attribute preservation, not a guessed inverse.

## Wire layout

All integers are unsigned little endian. UVs are IEEE binary32 bit patterns,
not native struct bytes. NaNs and infinities are invalid. Extra trailing bytes
fail. Both CRCs use reflected polynomial `0xedb88320`, initial accumulator
`0xffffffff` and final complement.

| Header offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `PML1` |
| 4 | 2 | Version 1 |
| 6 | 2 | Header size 32 |
| 8 | 4 | Total encoded size |
| 12 | 4 | Nonzero entry count |
| 16 | 2 | Entry stride 64 |
| 18 | 2 | Reserved, zero |
| 20 | 4 | CRC32 over entry payload |
| 24 | 4 | Reserved, zero |
| 28 | 4 | CRC32 over header bytes 0–27 |

The exact size is `32 + count * 64`, representable in `size_t` and uint32.

| Entry offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Model ordinal |
| 4 | 4 | First source-strip ordinal |
| 8 | 4 | Nonzero strip count |
| 12 | 2 | Wire role: 1 lightmap, 2 emission |
| 14 | 2 | 13-bit Compact texture identifier |
| 16 | 1 | Nearest/bilinear filter |
| 17 | 1 | Supersampling boolean |
| 18 | 1 | UV flip |
| 19 | 1 | UV clamp |
| 20 | 1 | Mip bias |
| 21 | 1 | UV source: zero (canonical) only |
| 22 | 2 | Reserved, zero |
| 24 | 4 | Unlit tint `0x00RRGGBB` |
| 28 | 24 | Six floats: two affine UV rows |
| 52 | 12 | Reserved, zero |

Wire role values are explicitly translated to runtime recipe roles. Entries
sort by model, then first strip. Same-model ranges cannot overlap. Inclusive
last-strip calculation cannot wrap, including at `UINT32_MAX`. Adjacent ranges
are allowed and need not be merged if material intent differs.

## Fixtures

`utils/pvr-chunk-layer-test` checks an independent 96-byte golden with fixed
CRCs, unaligned input, every truncation, every single-byte corruption,
CRC-repaired malformed fields, lookup gaps/boundaries, overflow, aliases,
unchanged outputs and concrete model ranges. The normal host-test runner
discovers it, and it has a `dreamcast` build target.

The layered material example serializes four procedural associations and uses
the decoded descriptors for recipe/vertex preparation. Its reference contexts
and expected UV/position/alpha checks remain independent. This is not an
imported glTF asset or a general scene loader. Presort/physical image gates
remain separate.
