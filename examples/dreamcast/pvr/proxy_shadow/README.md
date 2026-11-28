# Closed directional proxy shadows

A small convex box casts a finite shadow onto a white receiver. The development
machine cooks the face normals and opposite-edge neighbors. At runtime the
example classifies six faces against a unit light direction, caps the lit
patch at both ends, and joins the silhouette edges to their extruded copies.
A short output buffer leaves the previous volume intact.

Run `gmake -C examples/dreamcast/pvr/proxy_shadow host-test all` with the KOS
environment active. The host checks 26 light directions, opposite edge winding,
signed volume and exact capacity. Launch `proxy-shadow.elf` with RGB565
render-to-texture readback enabled in Flycast. The guest compares all 4,096
pixels against a separately authored cuboid, first with the caster in front
of the near plane, then with the caster straddling it. It checks the unshadowed
receiver and reverses an oblique light to verify the shadow moves.

The example uses the existing near-plane modifier projection, which moves
vertices onto the near W plane and preserves shared edges. Its mask comparison
covers this warp. After the first screen render the serial console prints
`CLOSED PROXY SHADOW RTT PASS`; the on-screen light alternates directions.
