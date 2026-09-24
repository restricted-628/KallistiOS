# Checked PVR texture surfaces

This example allocates a caller-owned RGB565 texture surface, fills it from a
linear source while converting to twiddled storage, and repeatedly replaces a
small rectangle without rebuilding the complete texture.

It also exercises exact mip and VQ layout queries, overflow rejection, invalid
level handling, codebook type checking, an interrupt-completed asynchronous DMA
upload, encoded full/partial/level readback, checked render-target binding, and
a 16x16 YUV420-to-linear-YUV422 conversion. The asynchronous API admits work
immediately or reports `EBUSY`; it does not create a queue, worker thread, or
permanent buffer. Corrupted caller-owned storage bindings are rejected again at
operation time. The program aborts on a failed invariant and prints a final
`RESULT: PASS (PVR texture surface)` marker after 120 rendered frames.

Emulation validates software layout, ordinary and converter interrupt flow, and
visible rectangle placement. It also validates exact CPU readback after an
upload and the render target's accepted ticket geometry. Physical
DMA/converter ordering and timing, CPU visibility of newly rendered texture
bytes, and updates concurrent with texture sampling remain hardware validation
items.
