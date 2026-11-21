# Decoder format boundary

Sega Library 2.00J is the fixed baseline. Its active ADX playback dispatcher
calls the floating mono/stereo decoder. An older integer decoder in the archive
does not establish another active format. Header field extraction is permissive;
a parser ignoring an encoding/version byte is not evidence that it implements
the algorithm that byte designates.

The native ADX path admits encoding3,18-byte blocks,4-bit samples, mono/stereo,
versions3/5 with zero extensions or the documented v3 single-loop layout.
Encoding 3 reads a 13-bit scale. The high bit identifies a control word; bits
13 and 14 do not contribute to the scale.
Version5 remains a native extension, not a format established by the baseline
sample corpus. Other encoding/history/encryption layouts remain unsupported.
The baseline setup initializes predictor histories rather than loading a v4
history extension. Exact numerical and batching equivalence remains a separate
gate from recognizing header fields.

All11 standalone ADX files in the baseline sample tree were decoded completely
through the native host checker, including strict body/endcode/EOF validation.
They use version3,flags0,44100Hz/500Hz, mono/stereo, with ordinary and looped
headers. All ten ADX members in the supplied ACX sample also passed strict
full-member decoding at 22050 Hz with a 500 Hz cutoff. This covers those assets: it does not prove every
archive member, vendor-output PCM equivalence, repeat playback or audible output.
Private asset hashes and source identities are retained outside the repository.

All 16 SFD samples in the SDK's Sofdec sample directory contain a finite ADX
record in their C0 audio stream, including a 288-byte header and an endcode at
the declared body boundary. Every record decodes to its declared frame count.
Bytes after that endcode belong to the mux. The closing C0 payloads may contain
mixed bytes as well as stuffing. A demux that presents a random-access C0 payload
can supply its whole extent to `kosdec_adx_stream_init` with repeat disabled.
Enable `kosdec_adx_stream_set_linked` and call `kosdec_adx_stream_release_link`
before the first read. The stream then validates one record and stops at its
endcode. Alternatively, the demux can supply the exact record extent to the
default strict reader. Both compositions produced identical PCM on the supplied
corpus. Standalone decoding continues to require strict EOF. Headerless or
late-count SFA uses the separate paired writer and joint reader.

The player manual explicitly supports WAV16-bit PCM and4-bit ADPCM at runtime.
Its WAV/AIFF authoring-input description alone does not establish runtime AIFF;
the binary additionally contains distinct WAV/AIFF/AU/SPSD header/decoder paths.
The native WAV reader admits tag1 unsigned8/signed16 PCM and CRI packed4-bit
ADPCM, mono/stereo, with strict RIFF/chunk bounds and pipe/service/player bindings.
The binary establishes unsigned8 conversion in addition to the manual's PCM16
promise. The SDK repacks4-bit input for hardware ADPCM; the native reader instead
reconstructs signed16 PCM using the AICA predictor/quantizer model. Stereo channel
bytes interleave and nibbles decode low first. Whole four-frame groups are required;
nonzero seeks rebuild histories in bounded steps. Hardware equivalence remains
unqualified; this is not generic Yamaha WAVE tag0x20 support.
A separate AU reader admits signedPCM8, big-endian signedPCM16 and mu-law8
mono/stereo, with bounded reads, metadata, seek, filters and pipe/service/player bindings.
AIFF signed8/BE16 mono/stereo reading, seek and filters are implemented with
strict chunk bounds and integral80-bit-rate admission. AIFF pipe/service/player bindings are included.
The SPSD reader/player supports 64-byte versions 01010004 and 00010004, codec 0
and index 0. The baseline ADX routine uses a length divided by two for its frame
count and copies two successive 16-bit samples per stereo frame. The bounded
native stereo path therefore requires sample-interleaved PCM16 and a payload
exactly twice the even length field. This subset was validated with synthetic
records. A shipped stereo SPSD asset remains unverified. Mono PCM16 retains its
contiguous layout and trailing-data behavior. Packed encodings, aggregate-length
stereo and block-interleaved layouts remain unsupported. SPSD loop/config fields
are ignored.
