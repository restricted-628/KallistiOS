# PVR multipass hybrid flushing

This example begins hybrid flushing in pass zero and keeps construction aligned
with hardware continuation through three registration passes. In the first two
passes, the opaque list is flushed explicitly and the remaining translucent
list is transferred at the pass boundary. The final pass is queued through the
normal asynchronous DMA chain.

The expected image contains three pairs of opaque and translucent panels. The
example verifies duplicate-flush rejection, DMA completion accounting, and a
fault-free final pipeline state. It prints
`RESULT: PASS (PVR multipass hybrid flushing)` after 120 frames.
