# PVR hardware multipass

This example registers three independent opaque geometry passes with one tile
accelerator session and releases one renderer submission after the final pass.
The expected image contains blue, green, and red vertical panels from left to
right. Seeing only one panel indicates that pass accumulation or continuation
failed.

The example also checks invalid pass transitions and the terminal PVR fault
record. It prints `RESULT: PASS (PVR hardware multipass)` after 120 frames.

Multipass currently uses direct store-queue list submission. The established
one-pass initializer and its DMA mode are unchanged.
