# Caller-owned lens regression

`make test` checks overlay contexts, deterministic grain/mean, cached offset
color and compensation headers, grain UV phases, hit lifetime/geometry, flash
packing, vignette mesh/EOL output and failure preservation. PVR boundaries are
mocked; no mock proves device execution.

Source the assigned environment and build the addon, then `make dreamcast`.
The native fixture owns an ARGB4444 grain tile and RGB565 RTT surface. Seven
scenes check vignette center/corners, two changing grain phases and approximate
mean compensation, a directional hit wedge, additive/alpha flashes and inversion.
All headers/vertices use the ordinary list sink; texture release follows its
last reader's COMPLETE ticket. Persistent checks also build under NDEBUG.
Successful execution prints `RESULT: PASS (SH4DER lens overlays)`.

Flycast needs `rend.RenderToTextureBuffer=yes` for CPU readback and the ordinary
shared lease. Emulator results do not establish hardware timing, exact general
blend quantization or physical cache visibility. Native link is not execution.

`sh4der-visor-test.elf` exercises OFF/NIGHT/THERMAL/AMBER and a faded-off NIGHT
mode over an original retained RGB565 source. CPU/SQ/DMA update caller-owned
thermal/noise/scan textures after the last reader; complete ordered packets and
prepared vignette use one TR sink. Active modes check expected channel dominance;
OFF and amount0 reproduce every baseline sample exactly. Host cases check LUT
mapping/padding, all preset packet counts/depths, cached-header reuse and failure
preservation. Success prints `RESULT: PASS (SH4DER visor modes)`.

Torch host checks compare every prepared output vertex/color to the public
CPU mesh, check transactional failures and129-entry fog arithmetic.
`torch-native.c` runs neutral/dark/daylight cones and caller-applied black fog
through retained RTT and frame readback. All CHECKs survive NDEBUG. The target
is released only after its final producer completes; failures shut PVR down.
