# Host tools

`adxcheck` validates assets against the current libADX admission rules before a
port attempts playback. It reads files without modifying them, decodes/discards
PCM through the existing validation model and checks the complete end record and
actual EOF. A valid header or complete sample count alone is not a passing result.

From the addon directory:

```sh
make tools
tools/.build/adxcheck music.adx voice.adx
make tools-test tools-sanitize
```

A host C compiler and the fork's pinned SH4ZAM headers are required. Python 3 is
needed only for tests. The tool does not require a Dreamcast toolchain or sound
driver. It uses a 4096-byte input buffer and one 32-frame PCM scratch block;
validation time scales with the input and declared sample count.

One JSON object is emitted per input, in command-line order. Fields include:

- `path_bytes`: OS path bytes represented as JSON characters U+0000..U+00FF;
  encode the parsed string as Latin-1 to recover those bytes, then decode with
  the OS filesystem encoding if desired. This also preserves invalid UTF-8 names.
- `status`: `compatible`, `unsupported`, `invalid`, `truncated`, `io_error`, or
  `checker_error` for an unexpected decoder result.
- `compatible`: true only after the full admitted file and EOF pass.
- `declared`: raw first-20-byte fields, or null for shorter input. These fields
  may describe malformed or unsupported input; they are not validated metadata.
- `validated_header`: admitted metadata and optional loop start/end frames and
  byte offsets, or null. End frames are exclusive. A validated header can still
  accompany a failed body/footer, so always check `compatible`.
- `decoded_frames`, `bytes_consumed`, `io_error`: progress and host I/O diagnosis.

Exit status is 0 if all inputs pass, 1 if any input is rejected or unreadable,
and 2 for usage or detected output-write errors. Later inputs are still checked
after a rejection. `--` permits a sole file named `--help`; `-` is an ordinary
filename, not stdin. Shell wildcards can supply an asset batch. Nothing is written
except stdout/stderr; direct JSON output to a different file from the inputs.

Admission currently includes encoding 3, versions 3/5, mono/stereo, 18-byte channel
blocks, 4-bit residuals and integer rates 1..48000 Hz with cutoff 0..floor(rate/2).
48 kHz is our extension beyond the baseline ADX Talk limit. See libADX's README
for extension, loop, length and footer restrictions. Unsupported does not mean an
asset is corrupt; it means this library does not yet admit its profile/layout.

This is a host compatibility checker, not a released portable audio decoder,
encoder, converter or proprietary-decoder equivalence test. Its explicit host
math model does not certify SH-4 arithmetic, playback timing, live rate changes
or physical AICA behavior. Archive extraction, WAV export and authoring tools
remain separate work. No proprietary SDK executable or media is distributed.

`default_player_rate_supported` is null until header validation; afterwards it
reports whether the source rate falls within the native player's 8000..48000 Hz
default-output range. It is independent of full-file `compatible` and is not an
audibility or hardware certification. Lower-rate sources require an explicit
in-range output override, which changes pitch and duration.

The separate opt-in `prepare-sfd-test.py` prepares a caller-supplied 360-frame
movie and FFmpeg references for the preserved [libSFD prototype](../libSFD/README.md).
It writes generated media/header files only to the explicit output directory;
keep that directory outside published source. It does not encode SFD or ADX.
