# Buffered microphone capture

This example records approximately five seconds from the first attached Maple
microphone into a caller-owned 16 KiB ring. It consumes samples through an
independent stream reader, reports peak amplitude and overrun accounting, then
stops cleanly while leaving the final retained samples readable.

The buffered API performs no eager allocation during normal Maple startup. A
small capture object and stream object are allocated only when requested; the
application owns the sample storage.

This example requires a compatible physical microphone. An emulator without a
microphone reports that no device is present and exits without modifying other
Maple devices.
