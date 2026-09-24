# Checked PVR material state

This example exercises the native global and material controls that complement
KOS's existing polygon contexts:

- checked color-clamp endpoints and readback;
- the punch-through alpha threshold and readback;
- checked contiguous palette writes;
- texture supersampling in polygon, sprite, and two-volume headers;
- checked buffered-list allocation with active-scene protection.

The expected image is a rectangle whose red channel is limited by the global
clamp endpoint. The example also verifies register values, packed header bits,
invalid arguments, and the zero-fault terminal pipeline state. It aborts on a
failed check and prints `RESULT: PASS (checked PVR material state)` after 120
frames.
