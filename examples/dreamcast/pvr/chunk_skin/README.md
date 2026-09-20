# Explicit compact-model skinning

This example binds one canonical four-joint influence record to every vertex
in a prepared compact model. It builds the sparse constant-time pose lookup,
canonical deformation source, and ordinary-strip draw cache once in
caller-owned storage, then samples a moving two-joint palette for 120 frames.

Each frame prepares the sampled joint palette into caller-owned SH4ZAM
matrices, then uses `pvr_skin_apply_prepared_palette()` to produce a dense
pose. A prepared palette can be shared by multiple meshes; this small example
has only one. Weights and changing vertex values remain checked. The
draw cache resolves each retained original model index through that pose,
shades from the deformed normal, projects its already assembled PVR-native
vertex run, and emits the triangle through the established PVR list sink.
Neither compact stream is reparsed in the frame loop.

The example allocates no hidden runtime state and starts no worker or service.
It prints `RESULT: PASS (explicit compact skinning)` after checking the
deformation count, render progress, and persistent PVR fault state.
This is correctness coverage, not a hardware throughput benchmark.
