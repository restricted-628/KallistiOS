# Direct CDDA validation

This example validates the direct SPI CDDA and subcode surface without using
the BIOS GD-ROM command server for the tested operations. Package it with at
least one audio track. It discovers the first audio track in the low-density
TOC, then exercises track playback, raw and typed Q subcode, pause, resume,
scan, FAD-range playback, stop, and BIOS reuse afterward.

The direct operations are synchronously bounded by required timeouts. Playback
sound, physical pickup behavior, and timing still require a real Dreamcast/CD-R
validation pass.
