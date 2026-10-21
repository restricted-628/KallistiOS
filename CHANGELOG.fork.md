# KOS Experimental iteration changelog

This complements, and does not replace, upstream `doc/CHANGELOG.md` or Git
history. Start of maintained iteration records: 2026-10-01. Earlier chat
iterations cannot be reconstructed completely from commit messages. The local
control folder contains a generated index of pinned published-branch history;
it is a historical index, not evidence that those changes passed tests.

## 2026-10-20 — upstream reconciliation with GCC retained

- Integrate upstream through `5f834c134`, including the merged VMU timestamp
  and soft-gUSA PRs. Preserve the fork's existing timestamp implementation and
  logical-stack/fiber bounds checking with debug-I/O reporting unchanged.
- Adopt the upstream `env/` layout and optional experimental LLVM/LLD files,
  while retaining GCC as the default and preserving old environment entry
  points. Initialize architecture settings before composing final compiler
  and linker flags; keep SH4ZAM in the link group.
- Preserve GNU17/GNU++17 defaults. Environment-equivalence checks cover old/new
  entry points, implicit/explicit GCC and Dreamcast/NAOMI selection; logical-SP
  host regressions pass in GNU17 and C23. LLVM is not a validated fork toolchain.
- Fresh GCC 16.2.0/ARM GCC 8.5.0 full build and C++ chrono, VM4 direct-disc and
  IRQ-stack target links pass. No new emulator or hardware execution is claimed.

## 2026-10-20 — explicit read-only file residency

- Add file-owned virtual reservations with synchronous page prefetch, bounded
  resident backing, eviction and reload. Publish only fully initialized
  read-only pages; reuse per-file staging and preserve independent pin lifetimes.
- Keep source immutability and final consumer retirement explicit. Cancellation
  is not I/O abort, prefetch is not a lease, and close rejects active loads/pins.
  No automatic page-fault I/O, writeback, compressed paging or overlays are added.
- Add production-code host coverage and an ISO9660 /cd example selecting direct
  GD-ROM. Sanitizers pass 2,615,842 checks; GCC 16.2.0 build/link and recorded
  core/exclusive-addon interpreter/dynarec direct-disc probes pass. Provider
  probes reuse accepted archives with the fresh VM object.
- Physical media/cache/DMA, loader return, hardware timing and ISO mount-drain
  remain unverified. Existing unrelated drivers, providers and defaults are
  unchanged; the example does not use an embedded RAM filesystem.

## 2026-10-18 — resident page pinning and physical byte runs

- Add independent pin tokens for committed ordinary-data regions, with exact
  physical byte-run enumeration. Reject overlapping mapping/backing mutations
  until every covering token is released; preserve unrelated-page progress.
- Keep final ownership explicit: callers retire all handle users and consumers
  before release. Pinning does not submit DMA, synchronize caches, resolve
  faults, select an address space or automatically drain cancelled work.
- Production-code host/sanitizer tests, GCC 16.2.0 build/link and recorded
  core/exclusive-addon interpreter/dynarec pin and carrier-blocking probes pass.
  Provider links reuse accepted provider archives with the new VM object.
  Hardware DMA/cache, loader and emulator-alignment limits remain unverified;
  no VM-buffer driver adapter, file-backed paging or overlay loader is added.

## 2026-10-18 — prepared resident fault recovery

- Add explicit one-page zero-commit and guard tickets with thread-context
  preparation, resident-only fault resolution and separate ticket/page lifetime.
  Preserve fatal handling for unarmed or ineligible accesses; no exception-time
  allocation, I/O, blocking or provider switch is introduced.
- Add production-code host coverage and a full-state translated recovery probe.
  Host checks, GCC 16.2.0 builds/links and recorded interpreter/dynarec recovery
  and complete-provider tests pass. Real-loader return, physical teardown,
  dynarec alignment and target raw-gUSA/nested behavior are not certified.
- This is not DMA pinning, file-backed paging, managed stacks or overlays.

## 2026-10-18 — exception register dumps bypass stdio

- Send generic exception dumps directly to the selected debug device, retaining
  severity filtering, register values, text and stack/unwind calls. A bounded
  private integer converter avoids stdio and shared formatting storage.
- Sanitized host regressions and GCC 16.2.0 emitted-code checks pass. The generic
  fault reproducer now reaches its dump, panic and abort output in the tested
  interpreter; static dump stack use is unchanged. Backend progress, nested
  failures and existing stack formatter limitations remain outside this fix.

## 2026-10-18 — panic output bypasses filesystem locking

- Route panic and abort messages through debug string output so exception
  reporting does not re-enter the mutex-protected filesystem path. Preserve
  abort-message filtering and the existing teardown sequence.
- Actual-function host regressions, GCC 16.2.0 builds and recorded fatal-output
  emulator checks pass. Printed abort output is not proof of completed shutdown
  or loader return; arbitrary debug-backend progress is not guaranteed.

## 2026-10-18 — keep logical-stack recovery visible to the compiler

- Use `__is_defined()` in the saved-stack accessor so disabled soft-gUSA code
  remains compiler-checked. Preserve saved-r1 recovery, marker bounds and the
  existing fiber-aware scheduler checks; do not adopt upstream's deferred check.
- Route the host regression to the real KOS configuration macros. Host checks
  and GCC 16.2.0 consumer compiles pass; helper assembly is identical before and
  after at both O0 and O2 for soft-gUSA and non-atomic modes. No new hardware
  or interrupt-runtime validation is claimed.

## 2026-10-18 — opt-in resident virtual data regions

- Add explicit reserve/commit/protect/decommit/release/query APIs above the
  checked MMU helpers, with zeroed backing and rollback/retirement before reuse.
  Exclude OCRAM; retain caller-owned context/ASID selection and quiescence.
  Ordinary allocation, P1 access, scheduler and memory defaults are unchanged.
- Add focused production-code host regressions and a translated-data probe
  spanning page tables, core fibers and carrier preemption. Current-base host
  and sanitizer checks, GCC 16.2.0 builds/links and both private strict-MMU
  Flycast modes pass. A separate emulator alignment-test failure remains open.
- This is resident CPU data only, not paging, DMA pinning, managed stacks,
  fault recovery or executable loading. No hardware/cache/performance claim.

## 2026-10-17 — upstream empty command-line correction

- Merge upstream `8e704086ecd4990627b85fb4905770dfffe29c00`, including
  `310899d0`: pass a writable, null-terminated argument-pointer array to
  `main()` when `argc` is zero. Preserve fork MMU and shutdown sequencing.
- GCC 16.2.0 kernel rebuild and strict probe links pass. The startup argument
  probe and SH4ZAM integration probe pass in both Flycast modes. No physical
  hardware validation or unrelated startup-policy change is claimed.

## 2026-10-17 — SH4ZAM blend and software-backend update

- Advance the official SH4ZAM pin from `7747e5d` to `c18faab`, preserving
  licenses and the explicit exact-base matrix-alignment correction.
- Add weighted 3x4/4x4 accumulation regression coverage and route API guidance
  to upstream's new documentation site. Existing graphics algorithms remain
  unchanged; the update also fixes software-backend inverse-trig endpoints.
- Clang/GCC host consumers, sanitized release fixtures, fresh GCC 16.2.0
  kernel/addon builds and affected example links pass. The full integration
  probe passes in both Flycast modes. Runtime host endpoint checks reproduce
  the old fast-math NaNs and pass on the new pin. No hardware speed claim.

## 2026-10-14 — LZ4 frame and cancellation regressions

- Add a deterministic 4,693-case resident-frame corpus covering truncation,
  checksums, lengths, dictionaries, multiple blocks and buffer offsets. Each
  case exercises synchronous decode and three stepped output budgets.
- Cover cancellation after partial output, callback-time destruction refusal,
  and job/output reclamation before service continuation. Production decoder,
  service, executor and upstream LZ4 sources remain unchanged.
- All five host lanes pass: Clang GNU17/C2x, GCC GNU17/C23 and Clang ASan/UBSan.
  The GCC 16.2.0 Compact asset consumer builds/links against current core/addon
  archives. Scheduler/IRQ doubles do not prove real executor races; no new
  target execution, DMA/cache, hardware or performance validation is claimed.

## 2026-10-14 — direct-stream cancellation regression

- Extend the no-media fiber/disc probe to cancel queued transfers, active
  transfers and their parent sessions while holding transfer callbacks past
  parent destruction. Verify detached requests cannot be destroyed until their
  callbacks retire. Existing request-engine retirement guards are unchanged.
- GCC 16.2.0 probe build/link and strict warning check pass. Flycast interpreter
  and dynarec runs pass the stream suite (230 checks, six cleanups) and enclosing
  RAM/GAPS routing suite (six sibling steps, fourteen callbacks).
- Transport spies exercise real request/fiber/callback workers, not physical
  disc commands or DMA. Hardware/cache/media and global teardown remain separate.

## 2026-10-12 — exception-return guard and core fiber diagnostics

- Set BL together with RB before restoring saved SPC/SSR on both normal and
  forced exception return. Preserve saved task SR, register layout and public
  ABI; production code has no diagnostic instrumentation.
- Add an emitted-code regression, separate live-SR diagnostic and public core
  fiber-runtime probe. Correct the older local-switch probe's coverage claims.
- GCC 16.2.0 affected builds and target links pass. In Flycast interpreter and
  dynarec modes, corrected live-SR and both fiber probes pass; the original
  return sequence fails the live-BL negative control as expected in both modes.
- No physical-hardware exception-window, NMI/UBC, general nesting or exclusive
  addon-provider validation is claimed. Separate TLS/mutex findings remain open.

## 2026-10-12 — bounded acceptance workflow

- Owner subsequently authorized Integration to commit/publish routine reviewed
  changes to fork master once required gates pass, without another permission
  round. Unresolved designs, upstream PRs and history rewrites remain excluded.
- Keep one owner, patch and compact handoff per issue; reuse matching evidence
  and limit Integration rechecks to the actual diff and acceptance gaps.
- Return specific corrections to the owner and integrate the delivered patch
  rather than rebuilding it from a second investigation. Preserve explicit
  source, runtime and hardware gates.

## 2026-10-12 — upstream TCP close retirement

- Merge upstream `568c3404c292a941bcf550465bd968f4fa249aa2` unchanged: reset
  established connections closed with unread data or receiving payload after
  application close, and bound selected closing states when the peer disappears.
- Fresh isolated GCC 16.2.0 utilities/kernel/addon build and HTTP server example
  link pass. Existing warnings remain. No packet-level runtime, emulator or
  hardware validation; BBA shutdown findings remain separate. SH4ZAM unchanged.

## 2026-10-11 — subsystem-owned review-ready delivery

- Make the assigned subsystem responsible for investigation, implementation and
  focused validation, with an exact diff/evidence handoff for Integration review
  and authorized commit. Return corrections to the owner instead of routinely
  reimplementing findings in Integration.
- Retain the source gate, isolated ownership, shared-interface coordination and
  explicit blockers; replace the blanket findings-only/pre-code-review default.
  Documentation-only workflow change, not a runtime fix or validation claim.
- The subsequent all-helper restart permits a bounded parallel round with
  isolated ownership; review/master integration remains serialized and specific
  research/design holds remain in force.

## 2026-10-11 — SH4ZAM update and local alignment correction

- Advance SH4ZAM from `ae8d4c1` to official `7747e5d`, retaining its source
  history and license. Carry an explicit exact-base matrix-alignment patch;
  setup applies it on request and verification rejects unexpected changes.
- Let native matrices/software state retain their required vector alignment.
  Keep raw-buffer XMTRX paths independent of that type alignment, preserving
  existing SH-4 instruction strings, dispatch checks and precise memory spans.
  The KOS memcpy bridge no longer requires equal native/legacy alignment.
- Isolated validation: eight GCC/Clang host-suite runs and a Clang sanitizer
  run pass; Mac utilities rebuild; GCC 16.2.0 force-rebuilds kernel/addons and
  links six integration/context examples. Focused SH-4 O2 wrapper assembly is
  unchanged apart from source-line annotations. Inherited build warnings remain.
- Follow-up canonical rebuild: utilities, kernel/addons and the six selected
  examples rebuild; all eight host-suite runs pass. SH4ZAM integration (including
  native/raw alignment) and the core fiber context probe pass in Flycast v2.7
  interpreter and dynarec modes. Existing unrelated source edits are preserved;
  this dirty-checkout run does not accept those edits as reviewed changes.
- Rebuild other consumers before use because native containing-layout strides
  can change. No hardware or performance claim; alternate fiber bundles and
  unrelated dirty experiments remain outside this validation.

## 2026-10-10 — upstream IRQ compiler-state correction

- Merge upstream `84ed47b77b2c7823c13e48aea32e4f4c0f3d5b14`: declare the T
  register clobbered by `arch_irq_restore()` so compiler condition-state
  tracking accounts for SR restoration. IRQ masks, saved values and the
  separate exception-return sequence are unchanged. SH4ZAM stays pinned.
- Fresh GCC 16.2.0 SH-4 kernel/addon/utility build and four synchronization/
  context example links pass. Focused C/C++ IRQ-header probes pass at three
  optimization levels; no old-code wrong-branch failure was reproduced.
- GCC 14.4 logical-stack host tests pass. Document the Clang host-header
  limitation; legacy general-threading/C++ examples retain their unrelated
  warning exceptions. No emulator or physical-hardware validation is claimed.

## 2026-10-10 — publication of reviewed integration fixes

- Publish the accepted fiber-event ownership, disc-request retirement and
  ordinary PCC1 single-admission changes as separate focused commits, followed
  by their fork guidance and decision records. Complete fiber bundles and
  upstream PR branches are unchanged.
- Rechecked the exact source against the retained October 3 validation
  artifacts and reviewed the outgoing diffs. The test/build results below are
  retained evidence, not newly executed tests for this publication pass.
- Kernel TLS, mutex and condition-variable findings remain research awaiting
  bounded implementation and regression work. Other unaccepted subsystem
  candidates and private research are not part of this publication.
- No new emulator, physical-hardware, DMA/cache timing or FPS claim is made.

## 2026-10-03 — sequential subsystem implementation policy

- Clarify that Kernel's phased investigation covers its complete primary/shared
  responsibility, starting with coverage and whole-startup mapping, rather than
  treating G2-focused lifecycle work as the entire scope.
- Record D17 and a dependency-ordered implementation plan with one lead lane,
  explicit completion gates and mandatory recorded documentation/SDK/Ghidra
  evidence before dependent changes. Source-only exceptions need review.
- Guidance only; no runtime changes or new validation claims in this iteration.

## 2026-10-03 — request retirement and single-pass cooked-cache loading

- Retain disc requests until completion handling, worker references and callbacks
  retire. Destruction rejects live internal owners; callback-free waits preserve
  wait-then-destroy behavior, while callback-bearing ordinary waits remain early.
  DMA stop policy, GAPS ownership and direct-disc defaults are unchanged.
- Add ordinary PCC1 header-only sizing and single-admission loading APIs, including
  module exports. The chunk-scene consumer now validates the payload once instead
  of three times and avoids redundant native-cache scans. Existing checked APIs
  remain supported; failed admission leaves output storage and draw state intact.
- Fresh isolated validation: 14 modeled request cases pass ASan/UBSan; cache tests
  pass normally and with sanitizers, including 1,888 mutation comparisons; scene
  integration/failure-cleanup checks pass using final public headers. GCC 16.2.0
  rebuilds affected SH-4 code and links three disc consumers plus chunk-scene
  against checkout-local archives. Unchanged build prerequisites are reused.
- These are local integrations, not published branch updates. No new emulator,
  physical-console, timing or FPS result is claimed.

## 2026-10-03 — fiber event ownership and graphics work direction

- Check the caller's fiber-runtime ownership before an already-signaled event
  can return success. Foreign and unattached callers receive `EXDEV`; preserve
  IRQ rejection, owner-main immediate success and child-fiber parking behavior.
- Clarify the public event-wait contract and add the focused ownership cases
  to the existing fiber-sync example. The core provider gains no SH4ZAM or
  Service Executor dependency; alternative provider branches are unchanged.
- Integration reran all 11 modeled host cases with Clang ASan/UBSan successfully
  and rebuilt the affected SH-4 code/example with GCC 16.2.0. Unaffected isolated
  build prerequisites were reused. Reviewed helper interpreter/dynarec logs
  each pass 42 ownership checks and the original synchronization sequence;
  no fresh emulator or physical-console run is claimed by integration.
- Record the accepted host-preparation/repeated-check-removal direction in D16.
  Single-admission cooked-cache loading is assigned work, not delivered code.

## 2026-10-01 — upstream ancestry and MMU example reconciliation

- Merge upstream from `7d0972e32d2d9e7af8d90f52bf0eb5a4dec1bfd7` through
  `d458073c00025f065cc68bebb8ef15ea83264e2b`, preserving the fork's direct-I/O
  defaults and previously adapted ISO9660 locking rather than replacing them
  with upstream's single-lock implementation.
- Retain shared SQ mappings (`SH=1`) through the fork's P2 helper. Add the
  upstream ASID example using startup-owned MMU lifecycle and checked mapping
  failure handling. Keep the existing whole-object keyboard initialization.
- Adopt unsigned Maple enumeration indexes and upstream documentation while
  retaining checked function descriptors and NULL results for invalid masks.
  Update the controller snapshot example's out-of-range index accordingly.
- Import upstream's TCP poll-wakeup lock-order fix and GCC 13.5 profile/patch
  maintenance. The active GCC 16.2 toolchain and pinned SH4ZAM revision
  `ae8d4c190bc52d4e01e8ec7a0a9b5a7b4fd7d69b` are unchanged.
- Host validation: MMU page and TLB models, SQ ownership, all 41 ISO9660 cases,
  and a focused Maple enumeration probe with ASan/UBSan pass. These do not
  establish physical cache, bus or DMA behavior.
- A fresh isolated build of the kernel, addons and utilities passes with SH-4
  GCC 16.2.0 (GNU17 kernel). ASID, controller snapshot, asynchronous ISO9660
  and direct ISO9660 examples compile with GNU17/Wall/Wextra/Werror and link.
  Maple header checks pass for GNU17 and GNU++17. Inherited build warnings
  remain; no emulator or physical-hardware run was performed.

## 2026-10-01 — ISO9660 cache lifetime and storage guidance

- Keep shared sector-cache bytes protected through foreground parsing/copying
  on both backends. Adapt upstream `aeee10d9b2de2d6644be5f303921baa9079717bb`
  without forcing direct async admission under BIOS stream serialization.
- Preserve short descriptor publication/finalizer locking, deferred remount,
  generation-aware snapshot invalidation and callback reentry. Keep I/O errors
  distinct from missing paths and reject failed mount reads correctly.
- Add a production-driver host fixture: 41/41 cases pass on integration rerun.
  Storage also reports 41/41 with ASan/UBSan; its transport, requests and VFS
  scheduling are modeled. Driver and two consumer objects compile with SH-4
  GCC 16.2.0/GNU17/Wall/Wextra/Werror; no full target link or hardware result.
- Correct direct-DMA documentation for segmentation/deadlines, callback
  retirement, caller-owned GAPS leases and existing cooperative stream APIs.

## 2026-10-01 — Kosh and Conio baseline import

- Include editable Kosh and Conio addon sources at upstream revisions
  `535a1f04c141aa61ae176dc2d11c373ab8b6c493` and
  `36c234b93c6ba44b58417c18b45828ec62b4d1c2`, respectively, preserving author
  notices and KOS licensing. Record upstream provenance in each addon README.
- Use in-tree header routing and addon archives; remove upstream Makefile
  writes into kos-ports. Neither library is auto-started or kernel-linked.
- Runtime sources/headers are unchanged; Nindows2 parity remains future work.
- Validation: both archives compile with SH-4 GCC 16.2.0, GNU17, -O2,
  -m4-single. Existing basic, debug-I/O and Kosh examples link to the new
  archives, checked in linker maps. Link smoke uses existing core/SH4ZAM
  artifacts, not a fresh full-kernel build. Inherited keyboard-deprecation and
  directory-entry const warnings remain. No emulator/hardware run.
- No Nindows2 implementation or inherited runtime behavior changes in this import.

## 2026-10-01 — project handoff foundation

- Add `AGENTS.md` for safe fork maintenance and subsystem coordination.
- Add `notes.md` distinguishing accepted decisions from implementation status.
- Start this fork-specific iteration log without altering upstream release notes.
- Adopt the owner's KOS Experimental project name. Relocate the local checkout
  and repair its Git/submodule registration without changing commit history.
- Add 21 selectively loaded component guides under `doc/agents`, with source
  entry points, contracts, interface partners, tests and handoff boundaries.
- Assign AICA/Manatee audio to a dedicated sixth subsystem lane, with distinct
  ARM firmware/toolchain, SH-4 host and DSP validation responsibilities.
- Add dedicated LZ4 and Nindows2/Kosh addon lanes and two guides (23 total).
  Record the owner's Kosh-based parity direction, including substantial Conio
  changes where justified. Initial assignments are source/design audits only.
- Require relevant shared-source/tool consultation for subsystem investigations,
  with pinned binary-analysis evidence and original-reference preservation.
- Inventory 1,390 unique changed/local/side-branch-only paths against pinned
  snapshots. This is a routing audit, not a full correctness certification.
- Validation: documentation structure, source anchors and privacy checks only;
  no runtime changes, target build, emulator run or hardware test in this pass.
- Guidance belongs to the experimental fork, not a blanket upstream submission.
  Private research and host-specific handoffs remain outside the repository.

## 2026-10-01 — documentation publication cleanup

- Master commit: `ebfee67bccd106ca2478f2b6365b7a8462eaed8b`.
- Remove eight unapproved documentation paths from affected published branch
  tips and repair references; corresponding cleanup commits cover eight branches.
- Verified planned documentation-only diffs and scanned all 30 published tips.
- Runtime code and pre-existing local edits were unchanged. Published history
  was not rewritten; local recovery material is kept outside the repository.

## Baseline before maintained iteration records

Selected committed milestones, not a complete retrospective changelog:

| Commit | Recorded change |
| --- | --- |
| `c78d405d4` | Interactive 2D/3D rendering showcases |
| `b86108d1e` | Bounded toon color rounding through SH4ZAM |
| `fa4f144b4` | Cooperative direct-stream waits in the fiber-disc adapter |
| `17996703a` | Direct reads into caller-owned GAPS leases |
| `10b70ede9` | Halt when DMA cannot relinquish its destination |
| `a49ac5762` | SH4ZAM dependency update after v0.9.1 |
| `e861241ac` | Upstream merge through `7d0972e3` |

Milestones above were confirmed in history, not retested during documentation
setup. Uncommitted graphics, G2 and NetBSD-port work is not a released milestone.

## Entry convention

For each accepted iteration: date and ID; subsystem; before/after commits (or
explicitly uncommitted); what changed and why; exact validation evidence and
limits; compatibility/dependency impact; remaining risks and next action.
Keep detailed logs and private paths outside this file. Link reviewable commits
instead of copying every experiment into the published changelog.
