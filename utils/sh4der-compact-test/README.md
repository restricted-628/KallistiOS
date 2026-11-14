# SH4DER Compact and SH4DOW integration regression

`make test` exercises indexed callbacks, repeated/reversed strips, capacity
failures, rebind, AO/color/shadow composition, XMTRX preservation, bloom and foot
geometry. `--cook-foot` emits the original64x64 alpha mask; test byte-compares
the checked-in header. Run normal and sanitizer builds sequentially because
they share SH4ZAM host object paths.

With KOS environment, `make dreamcast` builds two native fixtures.
`native.c` uses sparse model indices4..7 and actual prepared root/child skin
palettes, posed limb space, self exclusion and moved poses. Host-cooked AO,
near torch, blade/holo composition and geometric bloom use the same Compact
callback/geometry sink. All fixed binding/weight work is done before rendering.

`foot-native.c` uploads the host-cooked ARGB4444 mask, constructs current posed
foot/receiver geometry and uses depth-tested alpha blending. A nearer red
occluder stays untouched, excessive heel height removes the shadow, and a
moved foot moves the shadow. Source and reader retirement precede release.
CHECKs survive NDEBUG; errors shut PVR down before resource abandonment.
Flycast readback requires RenderToTextureBuffer=yes. This proves native emulator
execution, not physical-hardware visibility or performance.
