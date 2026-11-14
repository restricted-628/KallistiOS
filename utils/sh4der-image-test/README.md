# Admitted image and focus regression

`make test` checks the real addon with mocked PVR admission, context, transfer
and ticket boundaries. An independent 3x3 reference checks every blur pixel and
all edge padding. All three mask shapes compare packed colors with pinned public
math and check transformed positions, UVs, EOLs, capacity and failure preservation.
Image tests check once-only admission/compilation, four complete header states,
stride UVs, unsupported layouts and pending/error/retry upload paths.

Source the assigned KOS environment, build `addons/libsh4der`, then use
`make dreamcast`. The native fixture owns two RGB565 surfaces. Three scenes
upload with CPU/SQ/DMA, render disc/band/rect focus through one list sink, and
check white blurred corners against the preserved red sharp center. Four more
scenes exercise COPY, zero-alpha, additive and multiply image packets. The last
texture reader is retained independently of the output surface producer.
Assertions remain active with NDEBUG. Successful execution prints:
`RESULT: PASS (SH4DER image and focus)`.

Flycast requires `rend.RenderToTextureBuffer=yes` for these CPU readbacks;
use the shared emulator lease. Host mocks and native links are not execution.
These RGB565 linear scenes do not prove physical hardware visibility, timing,
or every supported texture format/layout. Bindings borrow and never track
external producers/readers: caller lifetime and submission ordering are required.

`sh4der-warp-test.elf` additionally exercises a caller-owned 320x256 stride
texture (320x240 content) and an8x8 twiddled ARGB4444 dither tile. The scene owner
sets the shared stride once. Seven scenes cover identity, ring, haze, wobble,
point pixelation, four-copy radial blur and repeating multiply dither; RGB565
readback checks changed pixels and retained channel separation. Host tests
compare every warp vertex with public source math, check non-divisor grid
positions, copy weights summing255, pixel UVs, row boundaries and failure paths.
The prepared grid removes per-frame integer grid division; scratch is caller-owned.
Native warp success prints `RESULT: PASS (SH4DER warp stride and dither)`.

`sh4der-ca-shafts-test.elf` checks chromatic aberration and independent shadow
shafts. Zero-strength three-channel reconstruction equals the ordinary image
quad exactly; active CA produces856 fringed sample pixels in the original
stripe fixture. Shadow shafts sample a fresh pre-effect black scene, upload a
caller shade map, and multiply it over white. Two history steps darken the
center progressively; reset plus facing away restores exact white everywhere.
Source-producer and map-reader IDs are separate. Host tests compare full CA
geometry and shaft map/mesh arithmetic against pinned public helpers, including
history in-place reuse, stale IDs and reset. Success prints
`RESULT: PASS (SH4DER CA and shadow shafts)`.
