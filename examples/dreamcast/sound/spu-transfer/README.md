# Asynchronous sound-RAM transfers

This example validates the queued sound-RAM request API without loading the
high-level sound firmware. It uploads and reads back a 64 KiB aligned pattern
with G2 DMA, then repeats an odd-address, odd-length transfer with exact-byte
PIO. Both paths use terminal callbacks dispatched from ordinary thread context.

The example verifies returned request status, callback completion, byte-for-byte
readback, and caller-owned request destruction. Its offsets stay clear of the
small idle program installed by normal KOS hardware initialization. A green
PASS or red FAIL screen remains visible long enough for emulator and hardware
testing without a serial console.
