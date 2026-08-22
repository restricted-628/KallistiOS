# Direct CDDA validation

This example validates the direct SPI CDDA and subcode surface without using
the BIOS GD-ROM command server for the tested operations. Package it with at
least one audio track. It discovers the first audio track in the low-density
TOC, then exercises queued track playback, raw and typed Q subcode, pause,
resume, scan, FAD-range playback, stop, callbacks, and BIOS reuse afterward.

The direct operations are bounded and report ordinary KOS request state with
`CDROM_REQUEST_BACKEND_DIRECT`. Playback sound, physical pickup behavior, and
timing still require a real Dreamcast/CD-R validation pass.
