# BIOS read and sector-mode contract regression

This probe uses production BIOS wrappers, with firmware/G1/request-submission
spies. It checks the explicit BIOS API independently of generic direct routing,
automatic and explicit sector modes, failed-mode state preservation, failed
automatic track detection, reinitialization, and raw 2352-byte / cooked
2048-byte async byte accounting for requests larger than 16 sectors.

It makes no real disc transfer and does not test DMA completion, queued mode
changes, scheduling, live media, or physical hardware. Applications must still
serialize BIOS sector-mode changes against outstanding BIOS reads/streams.
