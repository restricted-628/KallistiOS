# KOSdec

KOSdec groups independently linkable Dreamcast media components:

- `libADX`: ADX codec and Dreamcast middleware for KOS Experimental.
- `libSFD`: preserved bounded video-only prototype and standalone OIX/AMX player.
- `libAHX`: reserved for AHX audio.
- `tools`: host asset compatibility checker; authoring tools remain pending.

The KOS Experimental libADX milestone covers all **142 applicable Sega Library
2.00J Dreamcast middleware entries** through native implementations or accepted
native equivalents. Playback qualification applies to the documented Flycast
profiles. libADX provides incremental float decoding, repeat and release,
buffered file and direct-disc input, ACX/AFS lookup, stream joints, and a
fiber-fed player. See the [completion map](libADX/COMPLETION.md) for the
entry count, known backend failures, and remaining qualifications. Stock KOS
integration and retail device behavior remain unverified.

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

The opt-in [libSFD prototype](libSFD/README.md) builds independently with
`make -C libSFD sh4`; it does not change the ADX-first default targets.
