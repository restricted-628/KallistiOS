# Branch guide

Documentation snapshot: 2026-09-24. This covers all 30 published branches
in this fork at that snapshot, not private local worktrees or upstream's branches.
See [fork differences](FORK.md) for what integrated master adds to upstream KOS.

## Choose by purpose

- `master`: combined experimental SDK development.
- Complete fiber bundles: one checkout with exactly one provider. They are alternatives, not branches to combine.
- Narrow `pr/*` topics: focused review candidates, sometimes stacked on prerequisites. The `pr/core-fibers` bundle is the naming exception.
- `addon/*` topics: addon integration/extraction work, not automatically standalone installable ports.
- `fork/*` topics: integrated policy snapshots, not clean upstream submissions. Prefer master or a complete bundle for current combined development.

## Published branches

Each name opens that branch's own scope, dependencies and limits.

| Branch | Role | Description |
| --- | --- | --- |
| [`addon/fiber-disc`](https://github.com/restricted-628/KallistiOS/blob/addon/fiber-disc/BRANCH.md) | Integrated addon topic | Optional libfiber_disc adapter over the public fiber/event and asynchronous direct-disc request APIs, carried on an integrated fork snapshot. |
| [`addon/fiber-service-sh4zam`](https://github.com/restricted-628/KallistiOS/blob/addon/fiber-service-sh4zam/BRANCH.md) | Complete SDK bundle | One integrated checkout whose sole fiber provider is libfiber_sh4zam, including the Fiber Service Executor. |
| [`addon/graphics-extraction`](https://github.com/restricted-628/KallistiOS/blob/addon/graphics-extraction/BRANCH.md) | Extraction in progress | Ownership manifest and audit for a future shared 2D/3D graphics addon, not a finished independent library. |
| [`fork/direct-disc-convenience`](https://github.com/restricted-628/KallistiOS/blob/fork/direct-disc-convenience/BRANCH.md) | Fork-policy snapshot | Integrated direct-access routing and migration work, including explicit BIOS APIs and GAPS/BBA/loader ownership. |
| [`fork/direct-disc-default`](https://github.com/restricted-628/KallistiOS/blob/fork/direct-disc-default/BRANCH.md) | Fork-policy snapshot | Initial integrated-fork change selecting direct access for /cd and range/session constructors; retained as a policy topic. |
| [`fork/sh4zam-specular`](https://github.com/restricted-628/KallistiOS/blob/fork/sh4zam-specular/BRANCH.md) | Fork-policy snapshot | Uses SH4ZAM fast power in the fork's specular-lighting path; retained as an integrated graphics policy topic. |
| [`master`](https://github.com/restricted-628/KallistiOS/blob/master/BRANCH.md) | Integrated SDK | Development home for the combined OS, direct-I/O, graphics and service work. This is not the official KallistiOS master branch or a single upstream pull request. |
| [`pr/asic-event-ownership`](https://github.com/restricted-628/KallistiOS/blob/pr/asic-event-ownership/BRANCH.md) | Narrow upstream candidate | Adds explicit ownership to ASIC events and protects threaded-handler lifetime. |
| [`pr/cache-mode-transition`](https://github.com/restricted-628/KallistiOS/blob/pr/cache-mode-transition/BRANCH.md) | Stacked upstream candidate | Protects CCR read/modify/write and retires cache state according to the old mode before applying the new one. |
| [`pr/cache-range-safety`](https://github.com/restricted-628/KallistiOS/blob/pr/cache-range-safety/BRANCH.md) | Narrow upstream candidate | Corrects alias normalization and range admission for existing cache APIs. |
| [`pr/cache-whole-safety`](https://github.com/restricted-628/KallistiOS/blob/pr/cache-whole-safety/BRANCH.md) | Stacked upstream candidate | Uses non-associative indexed maintenance for whole-cache writeback/purge and respects active OCRAM storage. |
| [`pr/core-fibers`](https://github.com/restricted-628/KallistiOS/blob/pr/core-fibers/BRANCH.md) | Complete SDK bundle | One integrated checkout with the KOS core fiber provider and the surrounding SDK prerequisites. Despite its pr/ prefix, this is not a narrow upstream submission. |
| [`pr/core-fibers-submission`](https://github.com/restricted-628/KallistiOS/blob/pr/core-fibers-submission/BRANCH.md) | Stacked upstream candidate | Narrow core-fiber runtime proposal, distinct from the complete pr/core-fibers SDK bundle. |
| [`pr/fs-object-lifetime`](https://github.com/restricted-628/KallistiOS/blob/pr/fs-object-lifetime/BRANCH.md) | Narrow upstream candidate | Retains filesystem handlers and open-file state while operations and teardown overlap. |
| [`pr/g1-bus-ownership`](https://github.com/restricted-628/KallistiOS/blob/pr/g1-bus-ownership/BRANCH.md) | Stacked upstream candidate | Centralizes ownership of the G1 controller shared by disc and ATA paths. |
| [`pr/g2-dma-safety`](https://github.com/restricted-628/KallistiOS/blob/pr/g2-dma-safety/BRANCH.md) | Stacked upstream candidate | Strengthens existing G2 DMA channel ownership, validation, waiting, cancellation and PIO coexistence. |
| [`pr/icache-iix-safety`](https://github.com/restricted-628/KallistiOS/blob/pr/icache-iix-safety/BRANCH.md) | Stacked upstream candidate | Selects the correct instruction-cache array indices when CCR.IIX is active. |
| [`pr/independent-heaps`](https://github.com/restricted-628/KallistiOS/blob/pr/independent-heaps/BRANCH.md) | Narrow upstream candidate | Adds opt-in allocator arenas over caller-provided storage. |
| [`pr/itlb-reset-stride`](https://github.com/restricted-628/KallistiOS/blob/pr/itlb-reset-stride/BRANCH.md) | Narrow upstream candidate | Corrects the ITLB reset stride so all four entries are visited. |
| [`pr/keyboard-attach-clear`](https://github.com/restricted-628/KallistiOS/blob/pr/keyboard-attach-clear/BRANCH.md) | Narrow upstream candidate | Fixes typed-pointer arithmetic when clearing private keyboard state on attach. |
| [`pr/maple-capability-matching`](https://github.com/restricted-628/KallistiOS/blob/pr/maple-capability-matching/BRANCH.md) | Narrow upstream candidate | Corrects extended device matching against advertised function descriptors. |
| [`pr/mmu-page-lifetime`](https://github.com/restricted-628/KallistiOS/blob/pr/mmu-page-lifetime/BRANCH.md) | Stacked upstream candidate | Adds checked mapping/unmapping and cache/TLB retirement before backing pages are reused. |
| [`pr/posix-clock-correctness`](https://github.com/restricted-628/KallistiOS/blob/pr/posix-clock-correctness/BRANCH.md) | Narrow upstream candidate | Corrects process-clock PID selection, null clock-resolution queries and invalid nanosecond admission. |
| [`pr/sh4-logical-stack`](https://github.com/restricted-628/KallistiOS/blob/pr/sh4-logical-stack/BRANCH.md) | Narrow upstream candidate | Uses the logical saved stack address when GCC soft-gUSA atomics temporarily store a restart marker in r15. |
| [`pr/stereo-sq-mapping`](https://github.com/restricted-628/KallistiOS/blob/pr/stereo-sq-mapping/BRANCH.md) | Stacked upstream candidate | Corrects per-channel SQ mappings, PCM sample order and partial tails in stereo uploads. |
| [`pr/store-queue-safety`](https://github.com/restricted-628/KallistiOS/blob/pr/store-queue-safety/BRANCH.md) | Narrow upstream candidate | Strengthens recursive SQ ownership, mapping restoration and burst admission. |
| [`pr/tmu-channel-ownership`](https://github.com/restricted-628/KallistiOS/blob/pr/tmu-channel-ownership/BRANCH.md) | Narrow upstream candidate | Adds typed ownership/configuration for optional TMU1 use while preserving TMU0 scheduling and TMU2 uptime. |
| [`pr/vblank-priority-safety`](https://github.com/restricted-628/KallistiOS/blob/pr/vblank-priority-safety/BRANCH.md) | Narrow upstream candidate | Adds explicit-priority callback registration and safe removal while retaining legacy registration order. |
| [`pr/vmu-timestamps`](https://github.com/restricted-628/KallistiOS/blob/pr/vmu-timestamps/BRANCH.md) | Narrow upstream candidate | Corrects BCD year/century encoding and Sunday weekday conversion for VMU timestamps. |
| [`pr/workqueue-safety`](https://github.com/restricted-628/KallistiOS/blob/pr/workqueue-safety/BRANCH.md) | Narrow upstream candidate | Makes queued/running job ownership, cancellation barriers and worker shutdown explicit. |

## Dependency and review order

Dependencies refer to the implementation series recorded in each BRANCH.md;
this documentation pass does not rebase branches or merge newer code between them.
A child checkout already contains its recorded prerequisites. New documentation
commits on a prerequisite do not require another code merge into every child.
When merging shared fixes, retain the destination branch's identity in README.md,
BRANCH.md and FIBER-BUNDLE.md.

| Prerequisite | Follow-on topics |
| --- | --- |
| `pr/sh4-logical-stack` | `pr/core-fibers-submission` |
| `pr/asic-event-ownership` | `pr/g1-bus-ownership`, `pr/g2-dma-safety` |
| `pr/cache-range-safety` | `pr/cache-whole-safety`, `pr/icache-iix-safety` |
| `pr/cache-whole-safety` | `pr/cache-mode-transition`, `pr/mmu-page-lifetime` |
| `pr/store-queue-safety` | `pr/stereo-sq-mapping` |
| `pr/workqueue-safety` | Planned shared software timer-event extraction; no separate published topic yet |

The ITLB correction also appears in `pr/mmu-page-lifetime`; reconcile duplicate
patches and base revisions before upstream submission. Complete bundles and
fork-policy snapshots contain much more than these minimal prerequisites and
must not be submitted wholesale. A `pr/` name does not mean a PR has been opened,
accepted or merged. Fork-navigation documentation may be excluded from the final
upstream code series.

## Status boundaries

Graphics extraction currently supplies an ownership manifest/audit, not a finished
standalone archive. Direct-access/GAPS work has implementation in integrated trees,
but its remaining clean upstream slices are not all published. Some old policy
snapshots deliberately predate current master and are retained for focused history.

Use the component documents and tests in the selected checkout. Build, host-model,
emulator and physical-hardware evidence are different; this description pass
reruns none of those code suites and adds no physical-hardware validation claims.
The code revision recorded per branch is the pre-documentation snapshot, not a
permanent assertion that the branch cannot receive later fixes.
