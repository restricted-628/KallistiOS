# Direct DMA examples: targets and scheduling

These are different transfer paths. A pointer accepted by one DMA engine is
not automatically a valid destination for another. CPU/G1 ownership, cache
maintenance, DMA address form, alignment and resource lifetime remain part of
each API's contract.

## Optical-drive (GD/G1) DMA

| Destination | Existing examples / coverage | Remaining teaching gap |
| --- | --- | --- |
| Main RAM | `cdrom/direct-read`, `cdrom/direct-async`, `cdrom/stream`; `cdrom/fiber-read` adds cooperative application-fiber waiting | New fiber example needs live-media and hardware execution |
| PVR RAM | `cdrom/fiber-vram`: generated texture, guarded allocation, cooperative DMA wait, readback and render fence; `cdrom/direct-raw-dma` includes simulated transfers/bounds | Physical-hardware execution remains required; ordinary one-shot reads support VRAM, staged streaming remains RAM-only |
| GAPS bridge SRAM | `cdrom/direct-gaps-stage`: explicit SRAM lease, GD-DMA completion, then G2 DMA to main RAM | No fiber adapter for the lease-based API; bridge/hardware validation remains separate |

All example paths above are under `examples/dreamcast/`. The GAPS example is
serialized: it does not overlap GD/G1 and G2 access anywhere in bridge SRAM.
A missing or already-owned bridge is a SKIP, not a passing DMA test.

## Other engines, not additional GD-DMA destinations

| Path | Existing example | Purpose |
| --- | --- | --- |
| RAM/VRAM via SH-4 DMAC and PVR DMA | `basic/dma/speedtest` | Several memory directions/engines and a store-queue comparison; a benchmark rather than a lifetime tutorial |
| RAM to/from AICA RAM via G2 DMA | `basic/dma/g2-state` | Allocated sound-memory range, round-trip data and terminal channel state |
| Buffered TA registration via PVR DMA | `pvr/multipass_dma` | Per-pass double-buffered vertex staging and completion sequencing |

Disc-to-audio is a staged pipeline (disc to RAM, then audio/G2 submission), not
a supported direct GD-DMA-to-AICA shortcut. Likewise, uploading prepared
texture bytes and registering TA vertices are different operations. Direct
disc-to-VRAM does not decompress, convert, or twiddle a texture automatically.

## Fiber integration

`libfiber_disc` is an optional client adapter; the disc worker still executes
the I/O. Its first API wraps ordinary direct GD-DMA reads, not GAPS leases,
stream sessions, PIO, PVR DMA, or G2 DMA. `fiber-read` uses main RAM;
`fiber-vram` uses allocated texture RAM and only renders after retirement.
`cdrom/fiber-disc-contract` checks software lifetimes and sibling scheduling
with a simulated transport and the real request/callback workers.

Do not call blocking device waits from a child fiber and assume they suspend
only that fiber. A future adapter for another engine needs its own completion,
ownership, cancellation and quiescence contract. In particular, never free a
DMA destination merely because cancellation was requested or a callback began.

Software checks, emulator execution, and physical-hardware validation are
separate gates. Historical results in individual READMEs are not new runs of
all these examples, and no cross-target throughput claim is made here.
