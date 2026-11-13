# Experimental AICA bank authoring

Private format proposal, version 1. This tool builds and validates original
sample/instrument banks. It does not load sound RAM, play music, implement MIDI,
interpret a Konami format, or import Manatee banks. No public format commitment
is implied. Python 3 standard library only; no target compiler is used here.

```sh
python3 fixture.py /tmp/aica-original-fixture
python3 aica_bank.py build /tmp/aica-original-fixture/bank.json /tmp/aica-original-fixture/bank.abk
python3 aica_bank.py check /tmp/aica-original-fixture/bank.abk
make test
```

`build` refuses an existing output. Validation failure creates no output.
WAVs must be mono uncompressed PCM8 or PCM16. PCM8 WAV unsigned samples become
signed AICA samples; PCM16 little-endian bytes remain unchanged. No normalization,
resampling, compression or automatic stereo splitting occurs. Paths in the JSON
resolve relative to that manifest. Inputs are bounded before parsing.

Each sample needs `name` and `wav`. Optional `loop_start` and `loop_end` must
appear together and specify an exclusive interval in frames. Each zone needs a
`sample` name. The fixture illustrates velocity splits and a separate drum map.
Other optional zone fields and defaults are listed in `ZONE_FIELDS` and
`ZONE_DEFAULTS`; they use native KOS linear gain/pan and raw AICA envelope/routing
units, not CRI or Konami units. `root_key` describes the key at the sample's WAV
rate; `fine_cents` is metadata for the future pitch resolver, not applied here.

## Draft wire format

All multibyte integers are little endian, no native structure padding/pointers.
Header is 64 bytes. All offsets are absolute file offsets. Unknown versions,
flags, reserved bytes, noncanonical offsets and trailing data are rejected.

| Header offset | Type | Meaning |
| --- | --- | --- |
| 0 | 8 bytes | `KOSABK` followed by two zero bytes |
| 8, 10 | u16 each | version 1, header size 64 |
| 12 | u32 | exact whole-file size |
| 16, 20 | u32 each | sample count, zone count |
| 24, 28 | u32 each | sample table offset, zone table offset |
| 32, 36 | u32 each | PCM payload offset, padded payload byte count |
| 40 | u32 | IEEE CRC32 of whole file with these four bytes zero |
| 44 | u32 | flags, zero |
| 48 | 16 bytes | reserved, zero |

Sample table starts at 64. Zone table immediately follows the sample table.
Payload starts at the next 32-byte boundary after the zone table. Every sample
begins at the next 32-byte boundary after the preceding sample; gaps and final
padding must be zero. Records cannot alias payload or each other. CRC detects
accidental corruption; it does not authenticate content.

Each sample record is 32 bytes:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0, 4 | u32 each | PCM offset, unpadded PCM bytes |
| 8, 12 | u32 each | frames, base sample rate in Hz |
| 16, 20 | u32 each | loop start, exclusive loop end; both zero if not looping |
| 24 | u16 | encoding: 1 signed PCM8, 2 signed little-endian PCM16 |
| 26 | u16 | flags: bit 0 forward loop; other bits zero |
| 28 | u32 | reserved, zero |

Each zone record is 32 bytes:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u32 | zero-based sample index |
| 4, 5, 6 | u8 each | bank, program, drum (0 melodic, 1 drum) |
| 7, 8 | u8 each | inclusive key low/high |
| 9, 10 | u8 each | inclusive velocity low/high |
| 11 | u8 | root key |
| 12 | i16 | fine tuning, -100 through 100 cents |
| 14, 15 | u8 each | linear volume, pan; 0 through 255 |
| 16–20 | u8 each | attack, decay1, decay2, release, decay level; 0 through 31 |
| 21–24 | u8 each | key-rate scaling, direct level, effect bus, effect send; 0 through 15 |
| 25 | 7 bytes | reserved, zero |

Bank/program/key/root values are 0–127; velocity is 1–127. Velocity zero remains
a future note-off concern. A `(bank, program, drum)` map may contain disjoint
key/velocity zones; intersecting rectangles are rejected. This draft does not
support layered zones or velocity-to-gain curves. Drum selects a distinct map;
actual MIDI channel policy remains a firmware decision.

Limits are 256 samples, 1024 zones, 65534 frames per sample, base rate 1–192000
Hz, 2 MiB per file and 0x1d0000 padded PCM bytes. These are draft authoring limits,
not guaranteed runtime availability. The frame limit excludes AICA's prohibited
0xffff interpolation loop endpoint. A loader must additionally validate the
chosen pitch/loop combination. Rates and key/tuning combinations still need
target pitch-range checks. Each WAV is at most 2 MiB; JSON at most 256 KiB.

## Runtime contract still required

A future loader must validate immutable input again, allocate aligned sound RAM
and reserve resources without colliding with firmware, libADX streams or DSP
work, relocate only PCM offsets, and publish a complete generation only after
all transfers succeed. Failed partial upload must never publish a playable bank.
File validation is not transfer, allocation, relocation or execution validation.
In-use replacement/unload must reject until all voice/queued-command/transfer
references and final device fetches retire; software stop is insufficient.

This draft omits ADPCM, stereo groups, LFO/filter/extended envelopes, sequence
events, DSP programs and a MIDI engine. Those remain native parity work. No
retail hardware, audible fidelity or Konami-driver compatibility is claimed.
