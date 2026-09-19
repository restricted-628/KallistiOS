# Collision geometry

This example validates the renderer-independent collision layer with a moving
sphere, a capsule, closest-segment points, a ray/triangle hit, a rotated
oriented box, and caller-owned bounds. The queries allocate no memory and
require no PVR scene, background worker, fiber, or global collision world.

Touching shapes count as intersecting. Equal capsule endpoints are valid and
reduce to a sphere. Points published by the API consistently use W one while
plane normals use W zero.

Ray directions need not be normalized. Published ray distances are measured in
world units. Oriented boxes use caller-owned orthonormal axes and half extents;
their overlap queries do not register objects or retain a collision world.

Successful completion prints and displays
`RESULT: PASS (collision geometry)`, then leaves the result visible.
