# Scrolling tile map

Build with the KOS environment loaded:

```sh
make
make run
```

The example generates its texture and map in C. No external assets or host
image tools are required. A nonsquare 11-by-7 map uses a padded row stride,
six tile definitions, and an empty cell. An asymmetric L/dot texture pattern
makes horizontal and vertical flips visible.

The 360-frame sequence rotates, scales, and scrolls through four policies:
finite clipping, wrap, clamp, and wrap-X/clip-Y. Geometry must stay inside the
rectangle from (40, 64) to (600, 416). There should be no diagonal holes in
tiles, cracks at adjacent tile edges, or geometry outside that rectangle.
Transparent holes in the green/cyan cells and the one empty map cell are
intentional, as are half-alpha blue/magenta cells.

Draw descriptors are sorted by caller-owned priority, then routed through
opaque, punch-through, and translucent polygon lists using existing material
and canonical vertex-sink APIs. List boundaries take precedence over numeric
priority; the compiler does not invent cross-list depth or sorting policy.

The example reserves 256 KiB of vertex storage and 512 draw descriptors for
its bounded workload. Those are example choices, not library allocations.
Compilation checks exact required capacity before changing either buffer.
See [the API guide](../../../../doc/pvr-tilemaps.md) for smaller storage sizing.

After the sequence, serial output reports `RESULT: PASS` if API assertions
and PVR fault checks succeeded. The final image remains for ten seconds
before texture release and shutdown. A PASS marker alone does not replace
visual inspection or physical-console validation.
