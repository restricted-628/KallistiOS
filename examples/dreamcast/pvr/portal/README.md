# Rectangular portal composition

Source the KOS environment, run `make`, then load `portal.elf`. No assets or
optional ports are required. The example expects 640x480 RGB565. Enable
`rend.EmulateFramebuffer=yes` and serial output in Flycast for numeric checks;
otherwise framebuffer reads may see stale memory. Interpreter and dynarec
should be run separately with one emulator instance at a time.

The red main-view wall surrounds a rectangular opening. A green distant view
and a nearer magenta object are projected through an explicit camera matrix
and clipped to that opening by `pvr_frustum_clip_triangle()`. The magenta
object extends beyond the opening before clipping. A cyan main-view bar must
occlude both remote objects. An intentionally farther yellow panel in the
last pass must remain invisible.

The opening's edges are deliberately not aligned to 32-pixel tiles. Adjacent
inside/outside pixel samples distinguish exact geometry clipping from merely
enabling the tiles that contain the opening.

Two explicitly different composition routes produce the same intended image:

1. **Disjoint coverage:** four wall rectangles leave an actual hole. No wall
   depth was written there. All later boundaries preserve depth, including
   the cyan bar's depth. This is useful for authored, non-overlapping rooms.
2. **Depth clear:** the wall initially covers the opening too. Clearing depth
   before the remote view restarts every tile from the background plane.
   Following Daniel Fairchild's console finding in KallistiOS #1594, the
   example redraws the wall around the aperture and the cyan foreground
   bar before the remote view, restoring both their color and their depth.
   The last boundary preserves the newly populated depth.

There is no automatic switch between these routes and no pixel expectation
change for emulator compatibility. The disjoint route cannot be substituted
blindly into arbitrary overlapping scenes.

The fixture runs both routes through direct, buffered DMA and hybrid list
submission. It prepares geometry once per case in caller-owned memory, then
reuses it for twelve identical frames through checked material and canonical
geometry sinks. All operational checks remain active with `NDEBUG`. It adds
no renderer, service thread, scene owner, allocator, or public API. Storage is
6 KiB of vertex capacity plus small counters and 12 KiB of example-only DMA
staging, whose two frame halves are checked before admission.

## What is checked

- The shared host/target geometry builder uses the existing clipper; SH-4
  transforms therefore use the existing SH4ZAM integration. A nonidentity
  XMTRX is compared byte-for-byte before and after geometry preparation.
- Nine RGB565 samples per case check background, retained main-view color,
  both remote objects, the foreground occluder, and all opening boundaries.
- Completion waits and pipeline fault checks precede framebuffer reads.
- `utils/pvr-portal-test` rasterizes the same prepared geometry on the host.
  An independent analytic rectangle oracle checks all 307,200 pixels for
  each route. UV/depth checks cover new clipped vertices. Negative controls
  deliberately omit the bar replay and the depth clear to prove the oracle
  catches those composition mistakes.

## Validation limits

In earlier Flycast Vulkan tests, disjoint coverage passed all 27 samples.
The previous depth-clear route failed six of 27: the old wall remained visible
at the two remote-object samples in each submission mode. The known
[depth-clear limitation](../../../../doc/pvr-multipass-design.md#depth-boundary-validation)
is not counted as a PASS. A fresh Flycast dynarec run of the revised wall
replay again passed all 27 disjoint samples and failed six of 27 CLEAR
samples. The example reports the two totals separately and exits with
failure unless both are zero.

This is an application composition fixture, not a general portal engine or
mirror implementation. The opening is one fixed, axis-aligned rectangle; it
does not implement recursive portals, arbitrary polygon apertures, oblique
clip planes, reflected winding, moving cameras, or scene traversal. After a
full-tile depth clear, later main-view rendering must not assume old depth
outside the opening still exists. Physical-console images, edge precision
and timing remain validation gates. Host software coverage is not a PVR
rasterizer emulator or a hardware performance measurement.
