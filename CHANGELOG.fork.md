# KOS Experimental iteration changelog

This complements, and does not replace, upstream `doc/CHANGELOG.md` or Git
history. Start of maintained iteration records: 2026-10-01. Earlier chat
iterations cannot be reconstructed completely from commit messages. The local
control folder contains a generated index of pinned published-branch history;
it is a historical index, not evidence that those changes passed tests.

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
