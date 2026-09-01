# Cell-sprite assets

`PCA1` is the pointer-free storage form for the cell-sprite runtime. It stores
complete base cells and any number of independently timed key streams. Atlas
geometry, texture residency, whole-sprite placement, clocks, events, and PVR
submission remain application-owned.

The format does not create a second animation system:

1. `pvr_cell_asset_open()` validates the complete immutable byte image.
2. `pvr_cell_asset_materialize()` decodes into caller-owned base-cell, key, and
   stream-view arrays.
3. The resulting `pvr_cell_asset_runtime_t` feeds
   `pvr_cell_stream_list_sample()` and the established resolve/compile path.

No operation allocates memory, spawns a thread, retains an output pointer, or
performs PVR work.

## Wire layout

Every integer and floating-point word is little-endian. The 64-byte header is
followed by three contiguous fixed-record arrays:

| Array | Record bytes | Contents |
| --- | ---: | --- |
| Base cells | 72 | atlas index, local transform, priority, flags, material, four diffuse colors, four offset colors |
| Streams | 20 | first key, key count, time offset, time maximum, repeat policy |
| Keys | 88 | timestamp, slot, field mask, and one canonical partial cell state |

The header records every count and byte span, a payload CRC-32, and a separate
header CRC-32. Stream key spans must be gapless and cover the key array exactly.
Keys must be ordered within each stream and obey that stream's repeat/clamp
interval. Fields not selected by a key mask are encoded as zero; this makes one
authored asset byte-stable and lets admission reject hidden data or accidental
uninitialized storage.

## Authoring and loading

Host tools can construct ordinary `pvr_cell_state_t` and `pvr_cell_stream_t`
arrays, call `pvr_cell_asset_measure()`, allocate exactly that many bytes, and
call `pvr_cell_asset_encode()`. The same encoder is available on target, but
unused code remains removable by section garbage collection.

After opening an asset, allocate arrays using `view.cell_count`,
`view.key_count`, and `view.stream_count`. Materialization checks all capacity,
alignment, overlap, and source-lifetime rules before publishing the runtime
descriptor. The byte image may be released after materialization because the
runtime views borrow the decoded arrays rather than serialized storage.

Atlas-cell indices and material identifiers are deliberately opaque. They are
resolved by the same application atlas and pass-routing policy used for cells
authored directly in C.
