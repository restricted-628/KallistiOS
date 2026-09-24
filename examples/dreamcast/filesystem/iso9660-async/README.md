# ISO9660 asynchronous integration validation

This is a multi-feature validation program, not a minimal one-call tutorial. It
exercises directory prefetch, metadata-cache use, pickup preseek, aligned sector
DMA, an unaligned byte read, progress reporting, callbacks, and a staged stream.

The default image must contain `/async.bin` with at least 4096 zero bytes. A
different path can be supplied by a loader that provides `argv`; data guard and
lifecycle checks still run, while the known-zero comparison is skipped.

Every request uses a finite deadline and the same ownership rule: reach a
terminal state, wait for the callback dispatcher, then destroy the request. On
a wait failure the example cancels and performs a bounded drain before freeing
anything referenced by the operation.
