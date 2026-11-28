# libAHX

Copyright (C) 2026 Kyle Mayer

libAHX decodes the unencrypted mono AHX type 0x10, version 6. It exposes
incremental framing, PCM16 decoding and a serviced reader for finite
sources. Callers own storage, input and output scheduling. The library
does not allocate a sound device or require libADX.

## Build

`make sh4` builds the default FIPR synthesis. It uses the
pinned SH4ZAM SH-4 dot-product helper. `make sh4 AHX_SYNTH=fmac` builds
ordered FMAC synthesis. The two modes have separate object
and archive directories. Choose the same mode when installing with
`make install PREFIX=/path/to/addons`.

`make test` and `make sanitize` exercise the parser, decoder and stream
on the host. Run each with `AHX_SYNTH=fmac` to cover the other mode. The
decoder test also takes pairs of AHX and matching little-endian PCM16 files.
The oracle should contain every coded sample, including the final padding.
`python3 tools/generate_tables.py` reproduces `src/tables.h` from MPEG
equations and the MIT-licensed PL_MPEG window in libSFD.

## Decode

- For push input, use `<kosdec/ahx.h>`. The caller can feed fragmented
  input and provide any output capacity. Repeat the unconsumed suffix of
  the input and keep `final_input` asserted throughout the final span.
  `MORE` returns PCM progress. `NEED_INPUT` asks for more coded bytes.
  `NEED_OUTPUT` asks for PCM space. Only `DONE` validates the footer and
  physical EOF.
- For file, memory or archive-member input, use `<kosdec/ahx_stream.h>`.
  Bind a synchronous, finite `read_at` provider and call
  `kosdec_ahx_stream_service` with a PCM buffer. Each call admits at most
  one provider read and one granule. Pass zero capacity to prime format
  metadata. Stop or successfully rebind before releasing the source.
- Each granule contains 96 mono PCM16 samples. The final frame can produce
  coded padding. Use `valid_samples` when forwarding PCM to a playback
  queue and continue servicing until `DONE` to validate the file. Keep
  decoded, submitted and played frame counts separate.

The decoder uses the file header's sample rate and duration. It computes
the variable-length payload from the 30-band allocation, SCFSI, scale
and sample fields. Both observed private-bit frame markers are accepted.
Frames are structurally validated before synthesis. The stream rejects
invalid grouped samples, an inconsistent frame count, a malformed AHXE
footer and trailing bytes. The shared requantizer uses fused multiply-add
in both builds. Synthesis restores the calling thread's FPSCR at each
granule boundary.

`BASELINE_API.csv` lists the supported decoder behaviors. The native
interface uses caller-owned state and explicit errors in place of global
pools and implicit stream ownership. The explicit `kosdec_ahx_scan_sync` helper locates legacy markers in
a bounded span. Normal playback uses strict frame validation. Header
info preserves the optional header records and their count, with no
interpretation of title-specific controls. File headers are bounded at
512 bytes, covering both observed Dreamcast layouts.
Player dispatch, device playback and encoding are separate
from the decoder library. No game assets or extracted coefficient tables
are part of the archive.
