# Checked PVR clipping

This example combines both clipping layers:

- a per-scene pixel rectangle limits framebuffer writes;
- a tile-granular user-clip command limits an opaque polygon list whose header
  enables inside clipping.

The expected image is a green rectangle from tiles `(6,4)` through `(13,10)`
on a black background. It should remain inside the larger pixel rectangle.

The example also verifies offline command packing, invalid coordinate rejection,
disabled-list rejection, active-scene requirements, and the rule that pixel
clipping cannot change after direct TA registration starts. It aborts on failure
and prints `RESULT: PASS (checked PVR clipping)` on success.
