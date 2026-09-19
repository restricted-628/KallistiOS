# Explicit compact-model skinning

This example binds one canonical four-joint influence record to every vertex
in a prepared compact model. It builds the sparse constant-time pose lookup,
canonical deformation source, and ordinary-strip draw cache once in
caller-owned storage, then samples a moving two-joint palette for 120 frames.

Each frame uses the checked skinning kernel to produce a dense pose. The
draw cache resolves each retained original model index through that pose,
shades from the deformed normal, projects its already assembled PVR-native
vertex run, and emits the triangle through the established PVR list sink.
Neither compact stream is reparsed in the frame loop.

The example allocates no hidden runtime state and starts no worker or service.
It prints `RESULT: PASS (explicit compact skinning)` after checking the
deformation count, render progress, and persistent PVR fault state.
