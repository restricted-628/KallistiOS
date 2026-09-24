# What this fork changes

This is Joseph Black's experimental integrated fork of
[KallistiOS](https://github.com/KallistiOS/KallistiOS), published as
`restricted-628/KallistiOS`. It builds on the work of KOS and its contributors;
it is not the official KOS release or a drop-in compatibility promise.

This overview describes committed `master` at the 2026-09-24 documentation
snapshot. The reviewed implementation revision is recorded in [BRANCH.md](BRANCH.md).
For the complete checkout and contribution-topic map, see [BRANCHES.md](BRANCHES.md).

## Upstream foundation versus fork additions

Upstream KOS already supplies threads, VFS, networking, hardware drivers, PVR
submission, math, DMA, cache/store queues and MMU mechanisms. Those are not
new inventions of this fork. The additions and changed policies below extend
that foundation; some are candidates for upstream review and others belong
in addons.

| Area | What this integrated fork adds or changes | Current boundary |
| --- | --- | --- |
| Fibers and services | Cooperative continuations within a KOS thread, synchronization, optional XMTRX preservation and integrated Fiber Service Executor support | Fibers share their carrier thread's TLS, errno and address space; they are not isolated processes |
| Disc access | Direct SPI/Holly paths are the default for /cd and the migrated generic disc APIs; explicit BIOS APIs remain | No silent BIOS fallback; legacy BIOS stream callers must migrate; automatic mount/CDDA selection stays low-density |
| DMA and GAPS | G1/G2 ownership, request retirement, SRAM owner/lease/transfer lifetimes and BBA/resident-loader protection | Not a complete dynamic BBA/dcload driver-switching system or hardware DMA proof |
| Memory and time | Checked cache/SQ/MMU lifetimes, caller-backed heaps, exclusive TMU1 use, workqueue/filesystem lifetime fixes and clock argument corrections | Each mechanism has caller-ownership requirements; no blanket constant-time or memory-protection claim |
| Startup policy | Dreamcast INIT_DEFAULT_ARCH includes INIT_MMU | This is a fork default, not part of the narrow MMU contribution; applications using explicit flags must choose deliberately |
| 2D/3D graphics | Compact asset/model/scene pipeline, animation, skinning/morphing, lighting, toon/wire paths, particles, cells and tilemaps, with host asset tools | Much of this high-level code is still integrated into libkallisti; addon extraction is unfinished |
| Graphics math | Direct SH4ZAM integration, including affine 3x4 hierarchy/skeleton work and fast-power specular lighting | Not a global removal of legacy KOS math or a measured hardware speed/precision guarantee |
| Compression/audio | Optional LZ4 archive, bounded Compact Frame adapters and an experimental bounded ADX profile/PCM bridge | Memory budgets and format limits apply; not a general ADX compatibility claim |
| Devices and low-level APIs | Additional PVR, Maple, VMU, sound, storage and lifecycle work | Several areas still need focused extraction and hardware validation |

Inherited upstream changes, including the merged NAOMI 2 support, remain credited
to upstream. Their presence does not mean this fork has been validated on a
physical NAOMI 2.

## Choose the right checkout

- Use `master` for combined experimental development.
- Use `pr/core-fibers` for a complete SDK with only the core fiber provider and
  no Service Executor.
- Use `addon/fiber-service-sh4zam` for a complete SDK with the sole SH4ZAM fiber
  provider and Service Executor.
- Use `pr/core-fibers-submission` for narrow upstream fiber review.

The two complete fiber bundles are alternatives, not layers. Do not link both
providers or mix archives/installed headers from different checkouts. SH4ZAM
can still be required by unrelated graphics in the core-fiber bundle.

Clone integrated checkouts with `--recurse-submodules`, and run
`git submodule update --init --recursive` after revision changes.
Point `KOS_BASE` at that checkout and rebuild when changing providers.
[SH4ZAM's adapter](addons/libsh4zam/README.md) consumes a pinned upstream
submodule; its source, authorship, license and history remain upstream-owned.

## Read before migrating

- [Fiber/thread ownership](doc/fiber-runtime.md)
- [Direct-default APIs and explicit BIOS migration](doc/disc-backend-defaults.md)
- [Direct DMA examples and remaining adapters](doc/direct-dma-examples.md)
- [GAPS/BBA/resident-loader ownership](doc/gaps-ownership.md)
- [Direct graphics math](doc/sh4zam-direct-graphics-math.md)
- [Affine skeleton integration](doc/sh4zam-affine-skeleton.md)
- [LZ4 memory and stepping contracts](addons/liblz4/README.md)
- [Experimental ADX compatibility profile](doc/adx-decoder.md)

## Validation and contribution status

This remains work in progress. Host tests establish software properties, target
builds establish compilation/linking, and emulator runs establish behavior in
that emulator configuration. None alone proves physical-console timing,
appearance, electrical DMA behavior, 32 MiB RAM-mod compatibility or performance.

No blanket test pass is implied by this description pass. Follow the test
instructions and evidence limits attached to each component. Hardware gates
remain open. APIs and defaults can change while the fork is being separated.

The `pr/*` topics are review candidates, not statements of upstream acceptance;
`pr/core-fibers` is the explicitly documented complete-bundle exception.
Fork-specific default choices and high-level graphics/services must not be
submitted wholesale as kernel fixes. Documentation/navigation commits can be
left out when preparing the final focused upstream patch series.

Licenses and original attribution remain in place. Private research and
uncommitted experiments are outside the scope of these public descriptions.
