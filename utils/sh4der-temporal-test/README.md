# SH4DER retained temporal regression

`make test` covers pending producer publication, exact identity/extent admission,
stale/cut/reset/resize, output preservation, per-frame header/admission counts
and public camera reset. Run with ASan/UBSan using CFLAGS as usual.

With KOS environment, `make dreamcast` builds an original ping-pong RTT fixture.
Two retained RGB565 images produce red, blue/red TAA, recursive green blending,
three shifted trail copies, a cut and changed-generation resized history.
All CHECKs survive NDEBUG. The final producer also retires its source reader;
release occurs only after COMPLETE. Failure shuts PVR down without freeing
live buffers. Flycast CPU readback needs RenderToTextureBuffer=yes. Emulator
results do not establish physical hardware visibility or performance.

`look-native.c` exercises cached grade multiply/boost/subtract, adaptive exposure,
exact-frame halo feedback removal, map replacement rejection and reuse of the
corrected sample. Host tests compare delayed metadata eye behavior against the
public reference, exercise eviction and verify summed multi-pass correction.
