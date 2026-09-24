# Bundled LZ4

KallistiOS builds the upstream LZ4 1.10.0 library as the optional static
archive `addons/lib/dreamcast/liblz4.a`. The public LZ4 block, high-compression,
Frame, and xxHash headers are available from `addons/include`.

`<kos/pvr_chunk_asset_lz4.h>` supplies the optional LZ4 Frame decoder callback
for versioned compact-model assets. It accepts one exact frame, checks its
declared content size and dictionary identifier, leaves frame checksums on,
rejects trailing bytes, and exposes a manually stepped decoder with a caller-
selected output budget. Dictionaries remain caller-owned and optional.
State/job creation prepares the upstream context and scratch buffers without
decoding payload or writing output. Allocation failure is reported as `ENOMEM`
during setup; subsequent decode steps allocate nothing. This moves allocation
earlier, not into less memory: each queued job owns its own prepared decoder.
For upstream 1.10.0, scratch requests are `2*block_size+4` bytes for independent
frames, with another 128 KiB for linked frames, plus context and allocator
overhead. The original entry points still accept all upstream block sizes.

Use `pvr_chunk_asset_lz4_get_requirements()` to inspect a header's advertised
block size, block independence, scratch request, and borrowed buffer sizes.
It allocates only a temporary upstream context, not frame scratch, and does
not validate payload data. State/job `*_create_with_limits()` entry points
enforce a maximum block size, maximum scratch bytes, and optional independent-
block requirement before allocating scratch. `EFBIG` means a header exceeds
the selected policy; `EILSEQ` remains a malformed/inconsistent-header error.
No prior query is required: each constructor checks the header itself.

For converter-produced Compact assets, use this initializer (also used by
the `pvr/chunk_asset` example):

```c
const pvr_chunk_asset_lz4_limits_t limits =
    PVR_CHUNK_ASSET_LZ4_COMPACT_LIMITS;
/* 64 KiB blocks, independent only, at most 131076 bytes of frame scratch. */
```

NULL limits or zero byte caps keep the corresponding compatibility behavior.
These caps are per decoder and exclude context, wrapper, job, allocator, and
service overhead. Borrowed input/output/dictionary storage also remains the
application's responsibility; requirements do not reserve or sum a global
budget. Queued jobs still multiply scratch residency. Shared worker scratch
and caller-owned bounded arenas remain follow-up work.

`<kos/pvr_chunk_asset_lz4_service.h>` is a separately compiled, opt-in adapter
for the shared KOS fiber-service executor. One fixed FIFO can process reusable
decode jobs while yielding between bounded output steps. It owns no executor
thread or stack: the application chooses the shared executor, borrowed fiber
stack, queue capacity, and output budget. Executor shutdown cancels and
finalizes every queued job before the adapter is released.
Normal execution yields after a terminal job as well as after partial decode,
including when a completion callback submits another job. Shutdown drains
cancellations without yielding. Output budgets are not CPU-time guarantees:
an upstream call can decode a whole block for a small output buffer, or multiple
blocks for a large buffer. Callbacks must remain short and nonblocking.

The vendored library files are byte-identical to the upstream `v1.10.0`
release (`ebb370ca83af193212df4dcbadcc5d87bc0de2f0`) from
<https://github.com/lz4/lz4> and remain under their BSD 2-Clause license.
KOS-specific build policy is kept in the surrounding Makefile instead of
patching those sources.

The archive is not linked into applications that do not request it. Frame
compression uses heap-backed context storage on Dreamcast so a one-shot
encoder does not place its large state on a normal thread or fiber stack.
