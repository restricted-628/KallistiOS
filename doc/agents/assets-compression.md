# Assets, textures, compression and host converters

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Graphics + storage**.

## Scope and entry points

Compact/cell asset formats, resource bindings, texture layout/residency and
conversion/inspection tools. The dedicated [LZ4 addon lane](lz4-addon.md) owns
the compressor and decode/service adapters; this lane owns their asset-format
and rendering integration contracts.

- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_asset.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_asset_io.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_resource_asset.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_chunk_texture_asset.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_texture_residency.c`
- `addons/liblz4`
- `utils/pvr-model-convert`
- `utils/pvr-model-inspect`
- `utils/pvr-cell-convert`

Relevant existing documentation (dated claims must be rechecked):

- `doc/pvr-chunk-resources.md`
- `doc/pvr-texture-residency.md`
- `doc/pvr-vq-compact.md`
- `doc/pvr-vq-palettes.md`
- `doc/pvr-chunk-uv-sources.md`
- `doc/pvr-chunk-layers.md`

## Preserve these boundaries

- Treat file formats as interfaces: version/endian, bounded offsets/counts, integer overflow, overlap and partial input all need explicit behavior.
- Keep encoded assets, CPU staging, decoded workspace, resident VRAM and in-flight DMA/render references distinct.
- Check converter/runtime parity and round trips; never silently reinterpret existing versioned content.
- Compression and service adapters are optional. Preserve input/output budgets, progress/cancel and upstream licenses; do not import proprietary assets into fixtures.

## Coordinate before changing

- Storage for streaming
- Graphics2D/3D for formats/materials
- PVR/cache for texture lifetime
- Services for optional adapters
- LZ4 addon for codec, scratch budgets and decode/service contracts

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/pvr-chunk-asset-test` — candidate `make -C utils/pvr-chunk-asset-test test` in an assigned checkout.
- `utils/pvr-chunk-resource-asset-test` — candidate `make -C utils/pvr-chunk-resource-asset-test test` in an assigned checkout.
- `utils/pvr-chunk-texture-asset-test` — candidate `make -C utils/pvr-chunk-texture-asset-test test` in an assigned checkout.
- `utils/pvr-chunk-uv-asset-test` — candidate `make -C utils/pvr-chunk-uv-asset-test test` in an assigned checkout.
- `utils/pvr-texture-layout-test` — candidate `make -C utils/pvr-texture-layout-test test` in an assigned checkout.
- `utils/pvr-residency-test` — candidate `make -C utils/pvr-residency-test test` in an assigned checkout.
- `utils/lz4-adapter-test` — candidate `make -C utils/lz4-adapter-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/pvr/chunk_asset`
- `examples/dreamcast/pvr/chunk_asset_disc`
- `examples/dreamcast/pvr/texture_residency`
- `examples/dreamcast/pvr/vq_compact`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

A successful decoder test does not validate residency or physical uploads.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
