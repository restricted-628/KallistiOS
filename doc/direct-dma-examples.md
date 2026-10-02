# Direct DMA examples: targets and scheduling

These are different transfer paths. A pointer accepted by one DMA engine is
not automatically a valid destination for another. CPU/G1 ownership, cache
maintenance, DMA address form, alignment and resource lifetime remain part of
each API's contract.

## Optical-drive (GD/G1) DMA

| Destination | Existing examples / coverage | Remaining teaching gap |
| --- | --- | --- |
| Main RAM | `cdrom/direct-read`, `cdrom/direct-async`, `cdrom/stream`; `cdrom/fiber-read` adds cooperative application-fiber waiting; `cdrom/fiber-disc-contract` covers cooperative streams with simulated transport | Fiber reads and cooperative streams need live-media and hardware validation |
| PVR RAM | `cdrom/fiber-vram`: generated texture, guarded allocation, cooperative DMA wait, readback and render fence; `cdrom/direct-raw-dma` includes simulated transfers/bounds | Physical-hardware execution remains required; ordinary one-shot reads support VRAM, staged streaming remains RAM-only |
| GAPS bridge SRAM | `cdrom/direct-gaps-stage`: explicit SRAM lease, GD-DMA completion, then G2 DMA to main RAM; `cdrom/fiber-disc-contract` checks the lease-based fiber adapter with a submission spy | A live-media fiber/GAPS example and bridge/hardware validation remain teaching gaps |

All example paths above are under `examples/dreamcast/`. The GAPS example is
serialized: it does not overlap GD/G1 and G2 access anywhere in bridge SRAM.
A missing or already-owned bridge is a SKIP, not a passing DMA test.

Ordinary synchronous and queued RAM/VRAM reads accept larger ranges and split
them into commands of at most sixteen sectors. The lease-based GAPS APIs remain
single-command reads, limited to sixteen sectors and the lease's available byte
span. Raw 2352-byte DMA reads require even sector counts. See the
[DMA invariants](direct-gdrom-dma-invariants.md) for deadlines and retirement.

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

[`libfiber_disc`](../addons/libfiber_disc/README.md) is an optional client
adapter; the disc worker executes the I/O without a Service Executor dependency.
`fiber_disc_read_dma` wraps ordinary RAM/VRAM reads, and
`fiber_disc_read_dma_gaps` targets a caller-owned lease. The owner main fiber
calls `fiber_disc_pump` and dispatches ready children with `fiber_switch`;
`fiber_disc_await` parks only the calling child. The pump wakes it after the
underlying request and callback have retired and request destruction succeeds.
Inspect the copied terminal status before consuming data, then destroy the
read handle. Shutdown requests cancellation; continue pumping and dispatching
until retirement before destroying handles or the adapter.

Retain buffers and GAPS leases from submission through await completion,
including queue residence and cancellation. The driver pins a GAPS lease only
during execution; the adapter neither allocates nor releases it. Start a G2
consumer only after retirement and a successful read. Copy-engine claims do
not fence NIC activity; owner-authorized G1 remains allowed under NETWORK as
described in [GAPS ownership](gaps-ownership.md).

`fiber_disc_stream_start`, `fiber_disc_stream_await_ready` and
`fiber_disc_stream_transfer` provide cooperative staged reads into system RAM.
Check for READY before submitting a transfer, then await and retire each read.
After the last transfer or cancellation, `fiber_disc_stream_await` waits for
session retirement and all adapter transfer callbacks; read and stream handles
still need explicit destruction. G1 stays owned across the session, so do not
await unrelated queued disc work while holding it open. GAPS/VRAM streams, PIO,
PVR DMA and G2 DMA are not wrapped.

`fiber-read` uses main RAM; `fiber-vram` uses allocated texture RAM, renders
only after read retirement, and waits for render completion before freeing it.
[`fiber-disc-contract`](../examples/dreamcast/cdrom/fiber-disc-contract/README.md)
checks RAM/GAPS adapter lifetimes and sibling scheduling with a submission spy;
its `stream-contract.c` uses simulated transport with real session/request
workers. These probes do not validate physical lease pinning or DMA.

Do not call blocking device waits from a child fiber and assume they suspend
only that fiber. A future adapter for another engine needs its own completion,
ownership, cancellation and quiescence contract. In particular, never free a
DMA destination merely because cancellation was requested or a callback began.

Software checks, emulator execution, and physical-hardware validation are
separate gates. Historical results in individual READMEs are not new runs of
all these examples, and no cross-target throughput claim is made here.
