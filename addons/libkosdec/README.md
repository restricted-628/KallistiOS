# KOSdec

KOSdec groups independently linkable Dreamcast media components:

- `libADX`: active ADX codec and middleware development.
- `libSFD`: reserved for Sofdec video/container playback.
- `libAHX`: reserved for AHX audio.
- `tools`: host asset compatibility checker; authoring tools remain pending.

The first milestone is full ADX middleware parity with the Dreamcast-era SDK.
The current delivery supports incremental one-pass mono/stereo float decoding at integer rates 1..48000 Hz (cutoff 0..floor(rate/2)),
including validated loop-bearing files and padded end records, plus an optional
resident-input PCM reader and a bounded stream reader with a separate KOS file
adapter, an asynchronous direct-disc source, bounded ACX/AFS member lookup, and
an ordered 16-entry PCM sequence service, supporting repeat, release, pause
and cancellation, plus a bounded producer/consumer PCM pipe and acknowledged producer controls;
an optional fork-native audio player now adds prefill/start, polling, volume/pan
and checked software stop with retained sound allocations. Successive same-format
sources share one running output through explicit source release/rebind or early
cancellation. Bounded stream seeking rebuilds predictor history on the producer,
and checked output-rate changes keep source-format accounting separate. Full middleware parity
and sound-resource reuse remain unfinished. KOS Experimental is the first target. Stock KOS adapters follow later.

## Build the current component

Initialize the fork's pinned SH4ZAM submodule and source the isolated checkout's
KOS build environment, then run from this directory:

```sh
make sh4
make test
make sanitize
make install PREFIX=/path/to/isolated/addons
```

Use `make tools` and `tools/.build/adxcheck FILE...` to validate a port's standalone
ADX assets against the current decoder before target playback. See
[`tools/README.md`](tools/README.md) for JSON results and validation limits.

The SH-4 archive is `libADX/.build/sh4/libADX.a`. Installed consumers include
`<kosdec/adx.h>`, `<kosdec/adx_memory.h>`, `<kosdec/adx_stream.h>` or
`<kosdec/adx_file.h>`; buffered PCM uses `<kosdec/adx_pipe.h>`, cross-thread controls use
`<kosdec/adx_service.h>`, audio playback uses `<kosdec/adx_player.h>`, sequences use `<kosdec/adx_sequence.h>`, direct disc uses `<kosdec/adx_disc.h>` and indexed inputs use `<kosdec/adx_index.h>`. Link `-lADX` using the destination's include/library paths.
The codec/memory/stream objects have no SFD, audio-device, file-service or
scheduler dependency. The optional file and disc objects pull in only their respective transport APIs.
A native fiber/disc decode exerciser is available in `libADX/examples`; it is
separate from the archive and requires the fork's configured fiber provider.
An optional `make -C libADX audio-example` target demonstrates the public player
API with direct-disc input. Read its one-start, retained-sound and stereo-split
limits before integrating a port. The player never initializes global sound.
Host archives are test-only primitive emulation and are never installed.
No aggregate addon build defaults are changed.

See [libADX's contract](libADX/README.md) before using it. The public API is
experimental, namespaced `kosdec_adx_*`, and is not a CRI ABI replacement.
