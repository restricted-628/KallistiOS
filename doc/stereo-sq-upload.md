# Stereo PCM16 SQ uploads

`snd_pcm16_split_sq` now lives in its own source unit so the actual upload
routine can be tested independently of streaming-channel lifecycle code.
Its existing public symbol and signature are unchanged.

The corrected path:

- uses the SQ pointer returned for each channel, rather than an MMU-off mask;
- preserves chronological sample order, including negative PCM16 samples;
- advances tails by mono-channel bytes, not interleaved stereo bytes;
- finishes SQ transfers before releasing mapping ownership;
- limits each G2-locked channel batch to 4 KiB with no heap staging buffer;
- validates both complete destinations before writing, and stops on a failed
  acquisition without releasing a lock it did not acquire.

Each batch makes two bounded passes over the interleaved source block. This
keeps independently located channels valid even on systems with 8 MiB AICA
RAM. The batch bound is a resource/latency limit, not a measured performance
claim. No per-sample validation, memory clobbers, or floating-point math are
introduced. Cache mode and MMU startup policy are unchanged.

Destinations accept AICA offsets or direct physical/P1/P2 AICA addresses.
They must be disjoint, aligned, in range, and owned by the caller. Source
bytes are a multiple of 32; each output consumes half that count. A 16-byte
output tail uses PIO. Zero bytes do nothing. Argument errors set EINVAL;
IRQ calls set EPERM; acquisition failures preserve the SQ error. The legacy
void signature cannot return status directly, and a later failure may leave
a prefix or one channel uploaded. Playback synchronization is the caller's
responsibility: stereo publication is not atomic.

## Tests

`utils/stereo-sq-test` compiles the production routine against checked fake
SQ/G2 mappings and AICA memory. It covers 2,058 cases, sample values with sign
bits set, every 32-byte length through 16,416, reversed channel placement,
both MMU mapping models, direct aliases, 8 MiB sound RAM, input rejection,
guard bytes, bounded batches, and injected acquisition failures. It is not
a hardware timing, cache, or SH-4 instruction model.

`examples/dreamcast/sound/stereo-sq` runs 32 AICA round trips under MMU-off
and MMU-on, including cross-page destinations, tails, and restoration of an
application-owned outer SQ mapping. It uses generated samples, not private
media or middleware. The README identifies the scratch AICA ranges it writes.

Physical hardware testing remains necessary for FIFO timing, audio playback,
and throughput. The host 8 MiB AICA model is not a 32 MiB Dreamcast RAM-mod
test; those are different memory systems.

Recorded validation (2026-09-24): 9,979,196 host checks pass under GCC 14
GNU17/strict C23, Clang GNU17/C2x, and Clang ASan/UBSan. Separate negative
controls restoring reversed sample pairs and doubled tail offsets fail the
sample checks. SH-4 GCC 16.2 builds and Flycast interpreter/dynarec runs pass
on the narrow and integrated trees. No physical-hardware result is claimed.
