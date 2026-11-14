# Decoder format boundary

Sega Library 2.00J is the fixed baseline. Its active ADX playback dispatcher
calls the floating mono/stereo decoder. An older integer decoder in the archive
does not establish another active format. Header field extraction is permissive;
a parser ignoring an encoding/version byte is not evidence that it implements
the algorithm that byte designates.

The native ADX path admits encoding3,18-byte blocks,4-bit samples, mono/stereo,
versions3/5 with zero extensions or the documented v3 single-loop layout.
Version5 remains a native extension, not a format established by the baseline
sample corpus. Other encoding/history/encryption layouts remain unsupported.
The baseline setup initializes predictor histories rather than loading a v4
history extension. Exact numerical and batching equivalence remains a separate
gate from recognizing header fields.

All11 standalone ADX files in the baseline sample tree were decoded completely
through the native host checker, including strict body/endcode/EOF validation.
They use version3,flags0,44100Hz/500Hz, mono/stereo, with ordinary and looped
headers. This establishes coverage of that corpus only: it does not prove every
archive member, vendor-output PCM equivalence, repeat playback or audible output.
Private asset hashes and source identities are retained outside the repository.

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
A separate SPSD reader/player admits the confirmed monoPCM16 contiguous subset:
64-byte versions01010004/00010004, codec0 and interleave index0. The baseline ADX
path unconditionally forces16-bit copying despite parsing other widths; its
stereo copy also differs from common block-interleaved SPSD. Other SPSD
codecs/channels/layouts remain unsupported pending that contract/corpus gap.
The native parser does not reinterpret unsupported packed audio as PCM. SPSD
loop/config fields are ignored. Complete container/player conformance remains open. These are
middleware container obligations, not new ADX encoding modes.
