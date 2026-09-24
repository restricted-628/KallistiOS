# Experimental bounded ADX memory decoder

`<dc/sound/adx.h>` provides a native, caller-owned codec core. It does not play
audio, own a thread, access files, allocate memory, or submit DMA. It is kept in
the fork during bring-up; this is not an addon split or a complete Sofdec player.

## Supported profile

- Unencrypted ADX encoding 3, version/revision 0x0300 or 0x0500.
- One or two channels, 18-byte channel blocks, signed 4-bit residuals.
- Sample rates through 48 kHz, positive cutoff strictly below Nyquist, and
  declared PCM frame counts no greater than INT32_MAX.
- Zero initial prediction histories. Interleaved signed PCM16 output.
- Linear playback of the declared sample count. Loop metadata is skipped,
  not implemented. v4 histories, encryption, alternate coding, seek, looping,
  and early EOF compatibility behavior are not supported.

This is an explicit compatibility profile, not a promise that every file called
ADX is supported. A high-bit block scale before the declared end is an error.
The final compressed group is required in full even if fewer than 32 PCM
frames remain. DONE leaves trailing EOF/padding bytes unconsumed and unvalidated.

## Ownership and stepping

1. Allocate `snd_adx_decoder_t` and call `snd_adx_init`.
2. Call `snd_adx_decode`, advancing input by `consumed` and output by `frames`
   (multiply by channels for the int16_t element count).
3. Refill on NEED_INPUT; provide at least min(32, remaining) output frames on
   NEED_OUTPUT. MORE is a cooperative scheduling opportunity, not an error.
4. Mark the last supplied input span with `final_input=true`; repeat this flag
   while draining that span. Stop at DONE or an error. Cancel between calls.

Each call examines at most 256 header bytes or decodes one group of at most
32 frames. The state retains only 20 fixed-header bytes, a six-byte marker,
up to 36 compressed bytes, metadata, and predictor history. It never buffers
an entire header or movie. No hidden PCM queue exists. Calls with insufficient
output capacity do not consume compressed blocks. Input, state, and output
must not overlap and must remain valid during the call; only the documented
small compressed fragment is retained afterward. State fields are read-only
to callers after initialization. Instances have no shared mutable state.

Initial coefficient setup uses ordinary libm once after the header; decoding
uses bounded int32_t integer prediction with defined negative rounding and
PCM16 saturation. There are no FPU mode changes or yields inside the codec.
Do not compile coefficient setup under unsafe approximate-math assumptions.
SH4ZAM acceleration belongs in later video-transform work, not approximate
per-sample ADX prediction.

## Public interoperability references

This core was written independently using public format/algorithm behavior,
not by copying middleware or another decoder's source into KOS. It is not a
claim of a formally separated clean-room process.

The numerical contract follows the public vgmstream ADX implementation:
stored scale plus one, truncated Q12 coefficients, separate predictor-term
rounding for v3 and combined rounding for v5. Header version cannot identify
every historical player's arithmetic policy, so this choice is explicit.
See the pinned public [decoder](https://github.com/vgmstream/vgmstream/blob/764c84c5048932054356f2ea67a71ea7673abc83/src/coding/adx_decoder.c)
and [format reader](https://github.com/vgmstream/vgmstream/blob/764c84c5048932054356f2ea67a71ea7673abc83/src/meta/adx.c).
FFmpeg and libADX use differing arithmetic conventions; their PCM is not
treated as a bit-exact oracle for this profile. No upstream source or binaries
are bundled by the tests. Follow each project's license if separately building it.

## Verification and integration boundary

```sh
make -C utils/adx-test test adx-decode
python3 -B utils/adx-test/compare-vgmstream.py /path/to/vgmstream-cli
```

Tests generate synthetic input, require no commercial assets or SDK, and check
fragmentation, capacity, truncation, clipping, cancellation and instance
isolation. The optional comparison uses an independently built vgmstream CLI
and temporary synthetic files, never downloads a decoder automatically.

The core performs no pointer masking/alias conversion or fixed-RAM placement:
caller-owned canonical KOS buffers can live in either RAM bank. This is not
32 MiB hardware validation. The pending player still needs nonblocking audio
handoff, bounded input/PCM queues, starvation/stop/drain contracts, A/V timing,
and hardware validation of lower/upper-bank DMA. No physical playback or
performance result is implied by host tests or an SH-4 build.

Follow-up: the [bounded PCM bridge](adx-pipe.md) now supplies the
single-producer/single-consumer handoff and a synthetic fiber/audio integration
probe. It keeps existing blocking audio controls off the shared executor;
actual target playback and external input streaming remain validation/work gates.

### Bring-up results (2026-09-22)

- Host suite passed GCC 14.4 GNU17/C23 and Apple Clang 16 GNU17/C2x, including
  AddressSanitizer/UndefinedBehaviorSanitizer on the GNU17 Clang lane.
- Fragmentation matrix: both versions, mono/stereo, four header sizes, six
  frame counts and chunk sizes 1 through 39. Additional checks cover the
  maximum 65,539-byte header, all truncations of a stereo fixture, atomic
  rejection of a malformed second channel, output sentinels, and 10,000
  deterministic mutated/truncated inputs.
- 120 synthetic files matched PCM byte-for-byte against a separately built
  vgmstream CLI at the pinned revision above (GCC 14, GNU23). Comparison covers
  six rate/cutoff pairs, both versions/channel counts, five frame counts, and
  header offsets through 65,539. This does not establish every encoder/player's
  behavior or bit-identical target libm coefficient setup.
- SH-4 GCC 16.2 compiled the core with warnings as errors; the full KOS build
  passed and the test program linked against libkallisti. The core object has
  no mutable global data/BSS and no allocator dependencies. The target test
  was not executed on a console or emulator.
