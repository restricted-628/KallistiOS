# Generic direct-read routing regression

Runs production generic sector-format/read/reinitialization wrappers with
link-time spies for direct transport, BIOS mode selection, and queued read
submission. No media command or reset is issued to a drive.

Checks cooked/raw layout selection, automatic detection only during selection,
independent BIOS state, captured submission format, invalid layouts before I/O,
failed query/reset/media outcomes preserving the selected format, direct-only
PIO/DMA dispatch, timeout/error propagation, and no fallback. Generic async
reads require nonzero timeouts; odd raw DMA remains unsupported.

These are routing tests, not physical reset, DMA, media, or queued-transfer
proof. The direct PIO/DMA/queue probes separately exercise production transport
and lifecycle logic with simulated hardware. Real-drive testing remains open.
