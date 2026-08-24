# Collision geometry

This example validates the renderer-independent collision layer with a moving
sphere, a capsule, closest-segment points, and caller-owned bounds. The
queries allocate no memory and require no PVR scene, background worker, fiber,
or global collision world.

Touching shapes count as intersecting. Equal capsule endpoints are valid and
reduce to a sphere. Points published by the API consistently use W one while
plane normals use W zero.

Successful completion prints and displays
`RESULT: PASS (collision geometry)`, then leaves the result visible.
