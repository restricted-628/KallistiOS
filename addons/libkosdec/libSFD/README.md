# libSFD

This preserves the original KOSdec video-only player as an independently
linkable, experimental `libSFD.a` and a standalone Dreamcast example. ADX
middleware completion remains the first milestone; full Sofdec playback follows.
This is a bounded prototype, not a complete SFD decoder or CRI ABI replacement.

## Current profile

- Complete resident MPEG-1 program stream with the Sofdec marker, E0 video,
  optional C0 audio packets (counted and skipped), and sector-padded program end.
- MPEG-1 video: 320×240, 30 fps, default quantization matrices, I/P/B pictures,
  at most 4 MiB and 360 pictures. The caller supplies the expected picture count.
  The standalone example requires exactly 360 pictures. Other profiles reject.
- Float32 coefficient production, inverse quantization, SH4ZAM IDCT, motion
  interpolation, reconstruction and SH4ZAM RGB565 conversion. Syntax, indexing,
  counters and final pixel storage retain their integer representations.
- Synchronous callbacks and caller-backed workspace; no streaming, audio, A/V
  synchronization, fibers, seeking or concurrent decoder instances. Normal KOS
  FPU mode is required. IDCT and color conversion preserve caller XMTRX.

The decoder uses a modified, pinned MIT PL_MPEG snapshot; see
[src/reference/PROVENANCE.md](src/reference/PROVENANCE.md). Original KOSdec
adapters/math and the diagnostic font accompany it. No CRI implementations or
SDK/game assets are included. This is not wholly original bitstream-parser code.

## Build and use

Initialize this fork's pinned SH4ZAM dependency and its documented alignment
patch, then source this isolated checkout's KOS environment. From this directory:

```sh
make sh4
make test
make sanitize
make install PREFIX=/path/to/isolated/addons
```

Installed headers are `<kosdec/sfd.h>` (container extraction),
`<kosdec/sfd_mpeg.h>` (bounded video decoding), and `<kosdec/sfd_video.h>`
(RGB565 conversion and diagnostic overlay). Link `-lSFD -lsh4zam -lm`.
No ADX archive is required. These APIs are experimental and namespaced
`kosdec_sfd_*`; internal PL_MPEG symbols have a private KOSdec prefix.
The archive does not initialize sound, video, cache modes or the MMU.

Keep input immutable through return. Callbacks borrow a frame only until they
return; copy it to retain it. Input, output, statistics, workspace and callback
storage must obey the disjointness/alignment requirements in the headers.
Serialize all decoder calls across threads: a BUSY return detects reentry but
is not a concurrency lock. Callbacks may not escape with a foreign longjmp.
The normal decode return releases all decoder borrows, including errors.

## Standalone video player

Supply your own permitted test movie; no media is downloaded or bundled:

```sh
python3 ../tools/prepare-sfd-test.py /path/to/test.sfd /path/to/fixtures
make fixture-test FIXTURES=/path/to/fixtures
make fixture-sanitize FIXTURES=/path/to/fixtures
make video-example FIXTURES=/path/to/fixtures
```

The preparation tool requires FFmpeg for independent reference decoding. It
expects exactly 360 frames and produces private fixtures plus `movie_data.h`.
The ELF and map appear under `.build/sh4/`. To make a bootable test disc with
an already installed mkdcdisc:

```sh
mkdcdisc -e .build/sh4/sfd-video.elf -o /path/to/sfd-video.cdi -n 'SFD Video Test' -N -m
```

The example owns startup/shutdown, video mode and workspace mapping. It enables
OIX before devices/threads start and retains it until late shutdown. MCR.AMX
selects untranslated A25 on AMX2/4 and translated virtual A25 with canonical
physical backing on AMX3. It validates 64-bit SDRAM, BCR1.DRAMTP and RAM profile;
AMX4 requires 16 MiB, AMX2/3 accept 16/32 MiB. Unsupported profiles or inherited
MMU/cache ownership refuse playback; there is no OIX-off fallback. No memory
controller registers are changed. These are implementation constraints, not an
SFD file-format requirement. Do not transplant the standalone startup hooks into
an application with existing cache/MMU ownership.

Only the selected CPU workspace view is used until retirement. The direct alias
is purged before freeing canonical backing; translated mappings/context are
retired before freeing backing. Never give either CPU alias to DMA/SQ/device
APIs as a physical address. Physical cache correctness on modified boards, upper
RAM backing and late shutdown remain hardware validation gates.

The HUD reports decoded frames, measured presentation FPS and measured
decode-only FPS. The 30 fps target is a separate label, not a measured result.
It retains the last image; START exits. Optional `CAPTURE_HOLD_MS` pauses selected
frames for diagnostics and invalidates normal timing measurements.

## Validation boundaries and next work

Host tests cover all 16,777,216 color inputs, XMTRX restoration and extracted AMX
selectors. Optional movie tests cover float helper arithmetic, packet boundaries,
full-frame reference comparison and workspace reuse/canaries. Host SH4ZAM is a
software model; a target link is not native numerical or playback validation.
The original checkpoint displayed 360 frames in BIOS Flycast; that historical
result does not qualify this migrated build, real hardware or phone performance.

Remaining work includes general format admission, incremental input, bounded
scheduling, audio/clock integration, malformed-stream hardening, application-safe
OIX/MMU integration, native performance and physical hardware qualification.
Stock KOS support is later work. No real-time throughput guarantee is made.
