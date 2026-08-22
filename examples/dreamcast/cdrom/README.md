# Dreamcast CD/GD-ROM examples and diagnostics

These programs are executable documentation, but they do not all have the same
risk or media requirements.

## General API demonstrations

- `request` demonstrates request status, cancellation, callback dispatch, and
  the required wait/callback-wait/destroy lifetime sequence.
- `sector-range` demonstrates bounded FAD ranges over BIOS and direct backends.
- `media-recognition` demonstrates BootROM recognition and disc identity.
- `cdda-status` demonstrates typed synchronous/asynchronous CDDA status and an
  optional media-swap exercise. It requires audio track 2.

## Direct-transport validation

The `direct-*` programs validate the opt-in post-boot SPI transport. Most need
a purpose-built data or mixed-mode image. They are integration tests rather
than minimal application templates; read each directory's README before use.

- `direct-status` and `direct-geometry` are read-only diagnostics.
- `direct-read`, `direct-async`, and `direct-iso9660` compare direct data with
  the BIOS-backed path and verify DMA guards/progress.
- `direct-cdda` actively controls audio playback.
- `direct-lifecycle` sends `SET_MODE` and performs an SPI soft reset.
- `direct-recovery` deliberately aborts DMA and excludes its destination from
  the Holly protection window. Run it alone on a disposable emulator instance
  before considering physical-hardware validation.

All asynchronous examples must leave a request in a terminal state, wait for
its application callback, and only then destroy it. A failed wait first requests
cancellation and performs the same bounded drain sequence. Do not copy only the
success half of an example into application code.
