# Retained frame regression

`make test` exercises the actual adapter with mocked address admission and ticket
queries. It covers padded rows, subregions, clamped neighbors, output capacity,
failure preservation, stale generations and metadata slot eviction. Hardware
address checks, completion and pixel visibility are not host-test evidence.

Source this checkout's KOS environment, build `addons/libsh4der`, then use
`make dreamcast` to link `sh4der-frame-test.elf`. The native fixture requires an
RGB565 video mode and owns PVR for ten bounded RTT renders into caller-retained
storage. It checks red/green and blue samples, dark-neighbor minima, padded pitch,
exact render identity and metadata reset. CHECK expressions still run with
NDEBUG. Success prints `RESULT: PASS`; link success alone is not execution.

For Flycast, enable `rend.RenderToTextureBuffer=yes` in the isolated test config
or pass `-config config:rend.RenderToTextureBuffer=yes`. CPU readback needs RTT
pixels written into emulated VRAM; the GPU texture-cache path alone does not
provide them. Keep the exact color assertions enabled. This emulator setting is
not a Dreamcast runtime requirement or a hardware-visibility result.

The fixture deliberately has no GPU sampling readers. General callers must
retain every texture through those readers independently of producer completion.
It neither captures scanout nor measures rendering performance. Native emulator
and physical-hardware results must be recorded separately.
