# Upstream integration: September 12, 2026

Merged official master from `caf8fbfa` through `b470ef09` into the cumulative
graphics/sound/driver branch. Ten commits were outstanding, not only the newest
G2 DMA cleanup. The incoming series contains SPU RAM-mode/size corrections,
HTTP example lifecycle and directory-listing fixes, alternative ports include
locations, and G1/G2/PVR DMA blocking-flag cleanup.

## Conflict decisions

- Retained G1's explicit blocking state. In this branch it decides whether
  the IRQ releases G1 ownership or leaves the blocking caller to acknowledge
  device status under the bus lock. It is not merely a wakeup hint.
- Retained G2's blocking and waiter state. They reserve completion for one
  caller, reject competing `g2_dma_wait()` admission, and participate in
  cancellation/lifecycle handling. Semaphore count is not a substitute for
  operation ownership before the caller sleeps. No G1/G2 behavior was changed.
- Applied PVR's semaphore-count cleanup and removed the additional stale
  assignment in our shutdown path. IRQ exclusion spans starting the transfer
  through entering `sem_wait()`, whose negative count identifies the waiter.
- Combined upstream's RAM-mode helper and public size macros with our exact
  byte transfers and request-system initialization/shutdown. Preserved both
  contributors' attribution. Existing retail/expanded-hardware distinctions
  are upstream behavior, not a new platform-development tranche.
- Accepted ARM byte-width writes to the master-volume register, preserving
  upper RAM-mode/mixer bits. Rebuilt our extended firmware from merged source;
  did not replace it with the smaller stock firmware. The prebuilt fallback
  is 7,536 bytes and byte-identical to the new ARM build.

Firmware SHA-256:
`2483b899a6279153f8fc5732befbb8138dff6383495dd324b9bae66a48dbebe7`.

## Validation

- GCC 16.2.0 incremental KOS build, ARM firmware rebuild, DSP-control example
  and HTTP example links passed. ARM's existing RWX LOAD-segment linker warning
  remains; no toolchain update was made.
- G2 DMA and exact stream-tail tests passed GCC 14 GNU17/strict C23 and Apple
  Clang GNU17/strict C2x (eight runs). AICA command-layout and DSP-program tests
  passed GCC 14 and Clang GNU17 (four additional runs).
- Flycast dynarec passed checked DSP validation, status, output and clear with
  the rebuilt firmware.
- A temporary SH-4 smoke fixture passed sixteen alternating blocking and
  callback PVR DMA transfers with byte-for-byte VRAM readback under dynarec.
  It uses explicit source-cache writeback and checks completion callbacks.

These checks do not certify physical expanded sound RAM, real G1/G2 devices,
HTTP client shutdown under adverse network conditions, or all optional ports
examples. Existing graphics image-validation gates remain unchanged.
