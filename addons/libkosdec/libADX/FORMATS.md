# Decoder format boundary

Read the encoding, version, block dimensions and channel count together. A readable header alone does not establish that an input is decodable.

The native ADX path admits encoding3,18-byte blocks,4-bit samples, mono/stereo,
versions3/5 with zero extensions or the documented v3 single-loop layout.
Encoding 3 reads a 13-bit scale. The high bit identifies a control word; bits
13 and 14 do not contribute to the scale.
The native reader uses the rounded setup through 44100 Hz for either admitted version.
The baseline setup initializes predictor histories. Other encoding, history
and encryption layouts remain unsupported.

An SFD C0 audio stream can contain a finite ADX record with its own header, declared length and endcode.
Bytes after that endcode belong to the mux. The closing C0 payloads may contain
mixed bytes as well as stuffing. A demux that presents a random-access C0 payload
can supply its whole extent to `kosdec_adx_stream_init` with repeat disabled.
Enable `kosdec_adx_stream_set_linked` and call `kosdec_adx_stream_release_link`
before the first read. The stream then validates one record and stops at its
endcode. Alternatively, the demux can supply the exact record extent to the
default strict reader. Both compositions produced identical PCM on the supplied
corpus. Standalone decoding continues to require strict EOF. Headerless or
late-count SFA uses the separate paired writer and joint reader.

On both Flycast BIOS CPU modes, linear decoding of 37 complete records matched at one-group boundaries (74,472,221 PCM values). This does not qualify playback timing or physical output.

The native WAV reader admits tag1 unsigned8/signed16 PCM and packed4-bit
ADPCM, mono/stereo, with strict RIFF/chunk bounds and pipe/service/player bindings.
The native reader reconstructs signed16 PCM using the AICA predictor/quantizer model. Stereo channel
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
