# Direct GD-ROM lifecycle diagnostic

This example validates the direct-drive lifecycle layer without using
compatibility symbols or the BIOS GD command server for the tested operations.
It:

1. queues and decodes `REQ_MODE`;
2. queues `SET_MODE` with the exact settings just read, exercising the PIO
   host-to-drive phase without changing the effective configuration;
3. verifies the mode page synchronously;
4. queues the bounded soft-reset/readiness sequence;
5. reads the reset-default mode page and then proves the BIOS-backed TOC path
   can still reuse G1.

The reset operation is post-boot transport reinitialization only. It does not
claim to replace the Dreamcast boot ROM's power-on setup or GD authorization.

Run this against a disposable emulator instance first. A `PASS` line includes
the drive identification, pre/post-reset mode values, probe result, and three
callback completions. Physical-drive timing remains a separate hardware
validation gate.

Current Flycast builds may repeat the packet-out interrupt reason instead of
entering the expected host-to-drive data phase for `SET_MODE`. The example
reports a `PARTIAL` result only for that exact protocol mismatch, after
verifying bounded soft-reset recovery and completing every remaining check. It
does not count the write command itself as validated.
