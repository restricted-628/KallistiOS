# Caller-owned Compact effects

Build KOS/addons, then `make` here with the KOS environment sourced.
This ten-second example feeds SH4DER-PVR's prepared blade and hologram colors
through the existing Compact renderer and its geometry sink. Geometric bloom
uses caller-owned arrays. The application alone owns scenes, lists and headers;
there are no vendor framebuffer/RTT calls. The two passes deliberately replace
base colors for additive emission, leaving UV and offset color untouched.

Blade/holo callbacks use the reusable `<sh4der_kos.h>` addon adapter. Its admitted
binding also supports AO and posed limb shadows; see the addon coverage document
for those contracts and the still-unfinished screen effects.

The fixed four-vertex model admits its weight sidecar once at load. Real cooked
assets must bind weights to the final vertex order/generation and validate that
all referenced indices are covered on every model rebind. Neither callback
uses invocation order as a vertex index. Effect preparation uses a 30 Hz clock
derived from elapsed time, independent of presentation rate.

The orthographic example keeps all generated geometry on screen; a general
camera must use the existing clipping path and budget its expansion. This is
an integration example, not evidence of hardware visual/performance parity.
No new global shader registry, scheduler or per-vertex admission is necessary.

Credits: SH4DER-PVR by Jilted Valkyrie, SH4DOW-DC by Emerald May, SH4ZAM by
Falco Girgis and contributors. Read the two custom dependency licenses and
SH4ZAM's MIT license via `addons/libsh4der/README.md` before redistribution.
