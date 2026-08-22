# Checked PVR texture surfaces

This example allocates a caller-owned RGB565 texture surface, fills it from a
linear source while converting to twiddled storage, and repeatedly replaces a
small rectangle without rebuilding the complete texture.

It also exercises exact mip and VQ layout queries, overflow rejection, invalid
level handling, codebook type checking, an interrupt-completed asynchronous DMA
upload, and a 16x16 YUV420-to-linear-YUV422 conversion. The asynchronous API
admits work immediately or reports `EBUSY`; it does not create a queue, worker
thread, or permanent buffer. The program aborts on a failed invariant and prints
`RESULT: PASS (PVR texture surface)` after 120 rendered frames.

Emulation validates software layout, ordinary and converter interrupt flow, and
visible rectangle placement. Physical DMA/converter ordering and timing, cache
visibility, and updates concurrent with texture sampling remain hardware
validation items.
