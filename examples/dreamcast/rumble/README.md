# Vibration validation

This example validates the KOS vibration driver's typed effect codec, device
descriptor, per-unit metadata, relative orientation, hardware auto-stop,
asynchronous completion snapshot, and interrupt-context completion handler.

Configure a vibration pack on the emulated or physical Maple bus before
running it. The program prints `RESULT: PASS` over the serial debug channel
after a short effect and explicit stop complete.

The completion handler intentionally writes only fixed-size volatile state.
Maple completion handlers run in interrupt context and must not print, block,
allocate, perform filesystem I/O, or wait for another interrupt.
