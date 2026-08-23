# Caller-owned PVR geometry contract

This example projects an application-owned triangle through an explicit matrix
into canonical `pvr_vertex_t` storage. It then sends the prepared vertices
through both a caller-owned memory sink and the currently open PVR list sink.
The latter follows the scene's established direct or buffered submission mode;
the geometry API does not begin, finish, flush, or retain the scene.

The example renders a colored triangle on a dark background and prints
`RESULT: PASS (caller-owned PVR geometry contract)` after all checked memory
operations succeed.

The geometry contract allocates no memory and creates no worker or persistent
renderer state. Explicit buffered-list sinks are covered by the host test and
are available to applications already using PVR vertex DMA.
