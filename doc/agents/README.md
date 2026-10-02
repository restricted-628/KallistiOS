# Component task guides — KOS Experimental

Read root `AGENTS.md` first. Choose the smallest relevant guide below, then
read related guides only when the assigned change crosses an interface.
Do not load all 23 guides into every chat or treat a guide as permission to
implement a broad roadmap.

## Choose a component

Counts are unique inventoried paths assigned to a primary area, including
headers, examples, tests and documentation; they are not lines of code or
a correctness score. Cross-area review is still required. Counts retain the
original setup inventory; the two addon lanes added later refine ownership
without claiming a new per-path recount.

| Guide | Coordinating lane | Paths in setup inventory |
| --- | --- | ---: |
| [Integration, builds and upstream maintenance](integration-toolchain.md) | Integration | 82 |
| [Threads, core fibers and exclusive providers](threads-fibers.md) | Fibers/services | 55 |
| [Service Executor, workqueues and deferred events](services-workqueues.md) | Fibers/services | 41 |
| [Cache, MMU, OCRAM and store queues](cache-mmu-sq.md) | Kernel/platform | 78 |
| [Caller-backed heaps and allocation policy](heaps.md) | Kernel/platform | 16 |
| [ASIC events, G2 DMA and shared transfer safety](irq-g2.md) | Kernel/platform | 43 |
| [G1, direct GD-ROM and optical filesystem I/O](disc-g1.md) | Storage/direct I/O | 122 |
| [GAPS SRAM and resident-loader handoff](gaps-loader.md) | Networking/BBA + storage | 13 |
| [Network drivers and NetBSD stack adaptation](networking.md) | Networking/BBA | 30 |
| [VFS, name manager and module lifetimes](vfs-modules.md) | Storage + kernel | 39 |
| [VMU storage, packages and display](vmu-storage.md) | Kernel/platform + storage | 49 |
| [Maple bus and input/capture devices](maple-devices.md) | Kernel/platform | 51 |
| [AICA, SPU transfers, DSP and audio streams](audio.md) | AICA/Manatee audio | 90 |
| [Flash configuration and persistent settings](flash-settings.md) | Kernel/platform + networking | 18 |
| [RTC, timers and VBlank dispatch](clocks-vblank.md) | Kernel/platform + fibers | 69 |
| [Video modes and the low-level PVR pipeline](video-pvr.md) | Graphics | 81 |
| [2D cells, sprites, tilemaps and particles](graphics2d.md) | Graphics | 40 |
| [3D geometry, animation and rendering styles](graphics3d.md) | Graphics | 233 |
| [Assets, textures, compression and host converters](assets-compression.md) | Graphics + storage | 168 |
| [LZ4 Dreamcast addon and decode adapters](lz4-addon.md) | LZ4 addon | Subset of assets above |
| [Nindows2 parity through Kosh and Conio](nindows-kosh.md) | Nindows2/Kosh addon | New scope; not recounted |
| [SH4ZAM dependency and math contracts](sh4zam.md) | Graphics + fibers | 41 |
| [Startup, expansion probes and serial/debug paths](platform-serial.md) | Kernel/platform | 31 |

## Assigning a task

For example:

> Read AGENTS.md, relevant decisions in notes.md, and
> doc/agents/disc-g1.md. In the assigned checkout, inspect the direct-stream
> cancellation/retirement contract. Identify the exact source paths and existing
> tests, then report gaps without implementing changes. Do not change defaults,
> dependencies or other subsystems.

For implementation, also specify the bounded requested change, assigned isolated
checkout/base revision, completion criteria and whether publication is authorized.
A subject such as "work on networking" is not enough to start a wholesale port.
The eight subsystem chats are broad work groups; these guides let any one of
them take a smaller component task without inheriting the entire project chat.

## Audit basis and limits

Inventory date: 2026-10-01.
Integrated master: `ebfee67bccd106ca2478f2b6365b7a8462eaed8b`.
Pinned local upstream: `7d0972e32d2d9e7af8d90f52bf0eb5a4dec1bfd7`.

- 1325 tracked paths differ between those pinned snapshots.
- 35 pre-existing dirty/untracked paths were included separately.
- 40 additional side-branch-only tracked path entries were inspected across
  30 pinned published tips; some overlap other inventory sets.
- The union contains 1390 unique paths; no path is unclassified.
- Source/build aggregates, public-header families, component documentation,
  candidate Makefile test targets and example locations were inspected.
- Existing code edits were preserved; no host suites, target builds, emulator
  runs or hardware tests were performed for this documentation inventory.

This is a **scope/ownership/interface and validation-entry audit**, not a
line-by-line correctness review. Unchanged inherited KOS code was not exhaustively
reaudited. Branch presence does not establish semantic equivalence with master;
especially recheck the alternative fiber providers and older policy snapshots.
The historical commit index and detailed per-path routing manifest remain in
the private control folder supplied by the task launcher.

## Validation discipline

Inspect each selected test Makefile before running it. Most self-contained
host suites use `utils/Makefile.host-test`; the NetBSD pool test has its own
build rules and is not discovered by the aggregate runner's shared-Makefile
criterion. Host tests use portable/shim environments, not the actual hardware.

`utils/run-host-tests.sh` discovers suites and cleans outputs before/after each
run. Do not run it in a checkout another task is editing/building. The target
examples require that checkout's actual KOS environment and dependencies.
Test directories listed in guides are relevant candidates, not an exhaustive
validation matrix or a pre-existing success claim.

## Keeping this map current

When adding a public API, source family, addon, test suite or branch-only
component, update the appropriate guide and routing inventory. If a task spans
areas, record one primary owner and required reviewers before editing. Keep the
accepted decision in root `notes.md`, the shipped iteration in
`CHANGELOG.fork.md`, and private evidence/paths in the external control folder.

Do not create nested AGENTS files for every source directory merely to duplicate
these guides. The root routing rule deliberately makes instruction loading
selective while leaving the source tree intact.
