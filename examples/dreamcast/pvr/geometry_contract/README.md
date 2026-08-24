# Caller-owned PVR geometry contract

This example creates a caller-owned screen-space frustum from an explicit
matrix, clips a triangle that crosses the left plane, and triangulates the
result into canonical `pvr_vertex_t` storage. It then sends the prepared
vertices through both a caller-owned memory sink and the currently open PVR
list sink. The latter follows the scene's established direct or buffered
submission mode; the geometry API does not begin, finish, flush, or retain the
scene.

The polygon context is compiled through the checked material API. Malformed
context fields therefore report an error before a submission-ready packet is
published instead of reaching assertion-only header compilation.

The example renders the clipped colored triangle for 120 frames, shuts PVR
down cleanly, leaves a PASS marker on the framebuffer, and prints
`RESULT: PASS (checked PVR material, frustum, and geometry contract)` after all
checked operations succeed.

The geometry contract allocates no memory and creates no worker or persistent
renderer state. Explicit buffered-list sinks are covered by the host test and
are available to applications already using PVR vertex DMA.
