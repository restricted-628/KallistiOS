# Caller-owned glow regression

`make test` runs real CPU preparation and packet construction with mocked PVR
admission/compiler/upload/ticket boundaries. It checks weighted source maps,
block means, padding, in-place temporal easing, history generation, pending
readers, second-upload failure, retry, packet order/state/UVs and capacity.
Ray and shaft cases check all strip boundaries, interior fade, scaled UVs,
offscreen no-ops and failure preservation.
Mocks do not prove device transfers or hardware sampling.

Source this checkout's environment, build `addons/libsh4der`, then use
`make dreamcast`. The original native fixture owns PVR and three caller surfaces:
source/output RTT plus large/small glow maps. It samples a pre-glow scene,
prepares/uploads maps, renders complete halo packets through the ordinary list
sink, and verifies RGB565 readback. Three cycles cover CPU, SQ and blocking DMA
uploads, large/small halo selection, and all three layers together. Mesh scenes check a centered ray and scaled shaft, including exact comparison
with an independent half-weight halo reference. The last
reader of the maps is retained separately from the sampled source's producer.

For Flycast use an isolated config with `rend.RenderToTextureBuffer=yes` (or
`-config config:rend.RenderToTextureBuffer=yes`) and the ordinary shared lease.
This CPU-readback test needs emulated VRAM populated by RTT rendering. Exact
assertions run with NDEBUG; native link is not execution. A successful run prints
`RESULT: PASS (SH4DER glow maps, halo, ray and shaft packets)`.
Emulator results do not establish hardware visibility, performance, general
blend quantization or the separate `sh4der_shafts` backend.
