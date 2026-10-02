# KOS Experimental decisions

Recorded 2026-10-01. This is a decision register, not a claim that every policy
is fully implemented. Earlier decision dates are not reconstructed. Policies
below come from the owner's instructions in the coordinating project chat;
implementation anchors are listed separately and must be rechecked after changes.
Do not copy private conversation transcripts or research into this file.

## D01 — Development and contribution boundaries

**Accepted:** Keep integrated master for experimental development. Slow upstream
PRs; extract focused, justified changes only when ready. Direct GD-ROM and core
fibers remain desired contributions, not automatically approved submissions.
Keep complete SDK bundles separate from narrow review branches. Do not submit
an integrated branch wholesale. Anchor: `FORK.md`, `BRANCHES.md` (dated guides).
The owner selected **KOS Experimental** as the working project name on
2026-10-01. This does not rename the GitHub repository or imply an upstream release.

## D02 — Two mutually exclusive fiber providers

**Accepted:** The core/upstream provider must not require SH4ZAM or the Service
Executor. The addon supplies a complete alternative fiber stack and Service
Executor, using SH4ZAM directly for its relevant math/context operations, not
KOS-math fallbacks. Do not offer both providers simultaneously or as runtime
choices. Complete bundles must work without cloning multiple topic branches.
Anchors: `pr/core-fibers`, `addon/fiber-service-sh4zam`; the separate
`pr/core-fibers-submission` is the narrow review candidate. Recheck provider
symbol/link audits and header/archive provenance in the actual target bundle.

## D03 — Direct disc access is deliberate default

**Accepted:** Direct access is the fork default for `/cd` and migrated generic
disc APIs, including the legacy migration. BIOS paths remain explicitly named
and deliberately selected; no silent fallback. Breaking old BIOS-stream callers
is acceptable when migration is explicit. Anchors: `doc/disc-backend-defaults.md`,
`doc/direct-dma-examples.md`. Keep the low-density policy of the contribution
series; do not reintroduce high-density access while preparing upstream work.

## D04 — GAPS ownership belongs with direct access

**Accepted:** Keep GAPS integration with direct-access work. Owner, SRAM lease,
DMA transfer and NIC activity are different lifetimes. Networking and independent
staging are deliberately separate driver roles; do not borrow live NIC buffers.
Owner-authorized G1-to-BBA SRAM remains allowed under NETWORK ownership. Do not
confuse software ownership with hardware addressability. A complete resident
loader detach/restore and driver-switch protocol is not yet established.
Anchor: `doc/gaps-ownership.md`; current research is outside this repository.

## D05 — Failure must not release a live DMA destination

**Accepted:** Prefer the agreed halt behavior if a direct-disc DMA engine cannot
relinquish its destination. Do not introduce the rejected cross-system quarantine
design as a cleanup or recovery improvement without reopening the decision.
Implementation anchor: commit `10b70ede9` (inspect current descendants).

## D06 — Fiber I/O is optional, not an executor dependency

**Accepted:** Application fibers may cooperatively await direct I/O. Networking
and direct-disc completion must not depend on the Fiber Service Executor.
Fibers run within carrier threads; do not assume independent TLS/errno, automatic
fiber-safe blocking socket calls, or isolation. Anchor: `addons/libfiber_disc`.

## D07 — SH4ZAM graphics policy

**Accepted:** Use direct SH4ZAM APIs for fork graphics math and exploit applicable
affine 3x4 and related APIs after checking their contracts. Preserve upstream
source/history through the pinned dependency. Approximate power is accepted for
specular lighting; approximation alone is not a bug. Judge remainder and other
helpers against their stated domains and an actual rendering use case. Do not
silently substitute KOS math, add blanket clobbers, or claim unmeasured speedups.
Anchor: `addons/libsh4zam/README.md`, `doc/sh4zam-direct-graphics-math.md`.

## D08 — Language intent is not build configuration

**Accepted direction:** The owner prefers C23 for the fork; upstream patches
must respect upstream requirements. **Verified snapshot:** `environ_base.sh`
still sets `KOS_CSTD="-std=gnu17"` at `ebfee67bccd1`. Do not describe the target
as wholly migrated to C23 or change the standard merely to make this note true.
Record actual compiler version and flags separately for host and SH-4 lanes.

## D09 — MMU and VBlank policy

**Accepted:** MMU-on is the fork startup preference. Verified at `ebfee67bccd1`:
`kernel/arch/dreamcast/include/arch/init_flags.h` includes `INIT_MMU` in
`INIT_DEFAULT_ARCH`. This does not change untranslated address-region semantics
or prove RAM-mod compatibility. Preserve legacy VBlank registration order;
explicit-priority callbacks use lower priority first, equal priority newest
first. Optional fiber adapters must remain usable without the service executor.
Anchor: `doc/vblank-callback-safety.md`.

## D10 — NetBSD port scope

**Accepted:** BBA first; LAN adapter and modem/PPP follow. Port selected NetBSD
networking onto KOS services, not the NetBSD kernel. Reconcile sockets/VFS,
timers, blocking/wakeup, memory budgets, DMA and existing fork ownership.
Start with core host networking; IPv6 is in scope. Bridges/forwarding/multicast
routing and unrelated workstation facilities are not first-milestone features.
Tunnels/IPsec, firewall/NAT, SCTP and DCCP are deferred, not promised or forbidden.
Do not treat the present pool/mbuf work as a complete linked protocol stack.
Socket design still requires evidence-led reconciliation, not an ABI cast.

## D11 — Research before networking changes

**Current hold:** Complete the bounded driver-focused reconciliation before
implementing new fixes. Compare pinned KOS, fork, NetBSD and historical vendor
behavior; do not reproduce an obsolete stack wholesale. Treat unverified
hardware details as gates, not new APIs. Configuration is a platform service;
do not add automatic flash or NIC EEPROM writes. Private evidence and its
location map remain in the external control folder, not public documentation.

## D12 — Evidence, privacy and maintained context

**Accepted:** Hardware tests may be deferred, but never reported as passed.
Host/emulator success does not prove physical DMA, cache, timing or rendering.
Private reverse-engineering documentation and personal machine paths must stay
out of the fork. Do not restore deliberately removed documentation from history.
New subsystem chats share persistent records, not assumed conversational memory.
Every iteration records status and evidence; larger policy changes require the
owner's direction. A historical commit index is not a reconstructed test log.

## D13 — Dedicated AICA/Manatee ownership

**Accepted:** AICA/Manatee audio has its own subsystem chat, separate from
kernel/platform, because its firmware architecture and toolchain need focused
maintenance. Audio owns ARM firmware, SH-4 audio transport, sound RAM, channels,
DSP/streams and Manatee capability reconciliation. Kernel retains shared G2,
IRQ/cache mechanisms and global lifecycle; integration co-reviews toolchains.
This is an ownership split, not permission to import proprietary firmware or
change shared interfaces without coordination. Anchor: `doc/agents/audio.md`.

## D14 — Dedicated LZ4 addon ownership

**Accepted:** Give the Dreamcast LZ4 addon a dedicated subsystem chat. It owns
codec provenance/build and decode/service adapters; graphics retains asset
formats/rendering, storage retains I/O, and fibers retains optional scheduling.
Audit the current implementation before choosing extensions or optimizations.
Anchor: `doc/agents/lz4-addon.md`.

## D15 — Nindows2 parity as a Kosh-based addon

**Accepted direction:** Give Nindows2 parity its own chat and build it around
Kosh as an addon, with Conio in scope. Substantial changes to Kosh and Conio are
allowed by this direction, subject to bounded implementation assignments and
interface coordination. Existing Kosh already depends on Conio. First reconcile
versioned reference capabilities with current source; exact parity scope and
addon boundaries remain design work. Preserve provenance and independently
implement behavior, without importing proprietary SDK implementations/assets.
**Packaging decision:** The owner selected editable sources directly in this
fork, not submodules or separate library forks. Preserve upstream revision
provenance and licenses; do not describe a source import as a history merge.
Anchors: `doc/agents/nindows-kosh.md`, `addons/libkosh/README.md` and
`addons/libconio/README.md`.

## Updating this register

Use a stable decision ID. Record the decision, reason/tradeoff, user authority,
implementation anchor and validation limits. Mark a replaced decision as
superseded with a link to its replacement rather than silently erasing history.
Integration owns this file; subsystem chats propose updates in their handoffs.
