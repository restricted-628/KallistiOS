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
These are open middleware container obligations. They must be classified and
implemented separately instead of treating them as new ADX encoding modes or
silently removing them from the full-parity goal.
