# Bounded scrolling tile maps

`dc/pvr_tilemap.h` compiles a visible region of a caller-owned map into
ordinary colored PVR vertices and draw descriptors. It builds on the existing
cell compiler, homogeneous clipper, material system, and geometry sinks.
It is not a retained scene graph, editor file format, or automatic renderer.

## Storage and addressing

- A row-strided `uint32_t` index array selects reusable tile definitions.
  `PVR_TILEMAP_EMPTY` leaves a hole. Row padding is never interpreted as tiles.
- Definitions select atlas UV rectangles, flips, four corner colors, offset
  colors, and application material/list/priority metadata. The grid supplies
  dimensions; atlas-cell dimensions and pivots do not move tile edges.
- Each axis independently clips to the finite map, wraps (including negative
  coordinates), or repeats the nearest edge tile outside the map.
- The view applies scroll, positive nonuniform scale, rotation in radians,
  and a screen anchor, then clips to a rectangular viewport. Layers/parallax
  are separate calls with different scroll, anchor, depth, or material policy.

The transform is `anchor + rotation(scale * (map_position - scroll))`.
Depth is a positive PVR reciprocal-W value, not a cell-priority conversion.
UV flips preserve the geometric winding and the authored color-corner order.

## Bounded work and memory

`max_candidates` limits the inverse-viewport tile window before reading any
indices. Its count includes a one-tile rounding margin and may include tiles
later rejected by exact clipping. A rotated viewport's bounding window can
be larger than its final visible footprint. An over-budget window returns
`E2BIG`; range overflow returns `ERANGE` instead of wrapping coordinates.

`pvr_tilemap_measure()` returns exact candidate, visible-tile, and vertex
counts for a stable map/view. `pvr_tilemap_compile()` performs its own complete
preflight, then writes only when both caller-owned buffers are sufficient.
Thus callers with reusable capacity can skip the separate measurement call.
Sources must remain unchanged for the entire compile call, including across
thread scheduling. Failure leaves geometry, descriptors, and result unchanged.
Source/output aliasing is rejected.

Interior tiles need four vertices. Edge tiles are clipped as two triangles
with interpolated UV/base/offset colors; their output is independent triangle
triplets. One descriptor identifies each visible tile's vertex span. Measure
for exact capacity rather than assuming four vertices for every tile.

There is no heap allocation, thread, cache, global state, or cost at KOS
startup. Fixed stack scratch holds at most two clipped triangle fans. Only
candidate references are admitted, so malformed data elsewhere in a large
map is not an error until that data enters the candidate window. This is a
runtime selection API, not a whole-file integrity validator.

## Existing rendering integration

Descriptors start in logical row-major order and retain pre-wrap coordinates.
An application can sort them without moving vertex storage. It selects the
material, opens the appropriate PVR polygon list, and submits each span with
the existing canonical geometry sink. Opaque, punch-through, and translucent
lists are admitted; modifier lists are not tile surface lists. Numeric
priority is not automatically translated into depth or cross-list ordering.

The cell compiler supplies established A/B/D/C strip ordering, UVs and corner
colors. Fully visible tiles avoid clipping. Edge tiles reuse the frustum
clipper with an identity screen transform, then restore the caller's depth.
Degenerate tangent triangles are excluded from submission and capacity counts.
SH4ZAM supplies target trigonometry and the existing clipper's target math.
Inverse selection and large-coordinate cancellation use double arithmetic;
no performance gain over hand-specialized tile loops is claimed.

The [procedural example](../examples/dreamcast/pvr/tilemap/) demonstrates
addressing, nonsquare tiles, padded stride, colors, flips, and list routing.
Existing texture residency and upload APIs remain responsible for texture
ownership and transfer completion. This compiler performs neither operation.

## Validation

`utils/pvr-tilemap-test` is discovered automatically by the host-test runner.
It checks software-rasterized coverage (not just valid vertex encodings),
negative scroll, all axis-policy combinations, rotation/nonuniform scale,
atlas flips, UV/color interpolation, padded rows, hidden/empty cells, exact
capacity, work limits, arithmetic overflow, alias rejection, and unchanged
outputs on a late malformed reference.

Host tests are separate from the Dreamcast test ELF and visual example.
Physical PVR rasterization, texture filtering, and throughput still need
console measurements; host and emulator success cannot establish those.
