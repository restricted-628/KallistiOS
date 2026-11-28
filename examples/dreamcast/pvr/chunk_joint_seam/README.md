# Position-only joint seam

This example bends an original two-mesh elbow across seven poses. A seam table
names explicit pairs of original Compact vertex indices. At load time, the
example resolves them into dense skin rows and rejects missing indices or
multiple assignments to one follower row. Rebuild the map when replacing or
reordering either mesh.

Prepare each mesh's weights once and a common palette once per pose. Skin both
meshes into distinct buffers in the same coordinate space. After both succeed,
copy each anchor's posed position to its follower. Apply the seam before drawing
either mesh. UVs, hard normals and unpaired vertices stay with their original
part. This helper stays private to the example and does not change PCM2.

With the KOS environment sourced, build with `make` and load
`chunk-joint-seam.elf`. Each tested pose emits both independent textured
Compact strips into an RGB565 render target. After the render ticket reaches
COMPLETE, the example checks that green and blue meet without showing the red
clear color along the sampled interior of the seam. It also checks the actual
UVs and normals delivered to the Compact callback. Expected console marker:
`CHUNK JOINT SEAM RTT PASS poses=7`. Enable RTT CPU readback if using Flycast.

`make -C utils/pvr-chunk-skin-test test` also runs the CPU fixture with the
software math backend. It checks that the two bones would separate the mapped
vertices without the seam, that their final positions match exactly, and that
the independently posed normals, authored UVs, source bytes, and guards survive.
