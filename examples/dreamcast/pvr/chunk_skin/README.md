# Explicit compact-model skinning

This example binds one canonical four-joint influence record to every vertex
in a prepared compact model. It builds the sparse constant-time pose lookup and
canonical deformation source once in caller-owned storage, then samples a
moving two-joint palette for 120 frames.

Each frame uses the checked skinning kernel to produce a dense pose. The
compact renderer resolves the original model index through that pose in its
vertex-policy callback, replaces the position, shades from the deformed
normal, and emits the resulting triangle through the established PVR list
sink. Compact records are never reparsed by the deformation step.

The example allocates no hidden runtime state and starts no worker or service.
It prints `RESULT: PASS (explicit compact skinning)` after checking the
deformation count, render progress, and persistent PVR fault state.
