# Per-scene PVR background plane

This example replaces the default solid background with a checked,
untextured triangle whose three vertices are red, green, and blue. The expected
image is a full-screen interpolated color gradient with no foreground geometry.

It verifies scene-lifetime rules, copy-out, finite positive depth validation,
RGB888 color bounds, and rejection after direct TA registration has started.
The program aborts on failure and prints
`RESULT: PASS (PVR background plane)` on success.

Emulation validates descriptor transport and normal render flow. Physical color
interpolation and edge coverage remain hardware validation items.
