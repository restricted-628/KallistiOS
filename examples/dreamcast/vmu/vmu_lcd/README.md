# VMU LCD validation

This example validates the KOS VMU LCD descriptor check, byte-per-pixel
grayscale converter, all four horizontal/vertical transform combinations,
relative device orientation, asynchronous completion snapshot, framebuffer
presentation result, and interrupt-context completion handler.

Configure an LCD-capable VMU on the emulated or physical Maple bus before
running it. The program leaves an asymmetric `KOS LCD` pattern on the first
screen and prints `RESULT: PASS` over the serial debug channel after the device
acknowledges the display write.

The completion handler intentionally writes only fixed-size volatile state.
Maple completion handlers run in interrupt context and must not print, block,
allocate, perform filesystem I/O, or wait for another interrupt.
