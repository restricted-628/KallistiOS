# Direct GD-ROM to AICA wave RAM

This retail Dreamcast probe uses the public `gdrom_direct_load_aica` constructor with
seventeen sectors from a low-density data track, verifies its allocated offset,
adjacent guards and bytes, then frees the unplayed allocation. It also tests
the private transport's sound offset and the
uncached wave RAM image address in separate reads. It compares the readback
with the same sector in main RAM and checks both adjacent guards. It also
checks ordinary API rejection, an out-of-bounds AICA span and a firmware
offset. Use a test disc and keep the allocation unplayed throughout.

Build in the configured KOS environment with `make`. The program prints
`GD-AICA PASS` only after matching the transport byte count, payload and guards.
An `EBUSY` result means DMA, an active watchdog or buffered writes prevented
the exclusive window.
The test uses normal SPU reads after the window ends.

The transport accepts a 32-byte-aligned offset in the retail 2 MiB wave RAM
window. The read uses commands of at most sixteen sectors and a whole-read
deadline of at most 500 ms. The CPU polls during each transfer and keeps
interrupt dispatch masked through the final drain. Command recovery can add
time. Hardware that fails to stop or drain takes the direct driver's existing
retained halt policy before any client regains access.

This probe provides a byte-integrity check. Maple, networking and audio
latency during direct AICA transfers require physical measurements before
using the transport for live streaming.
