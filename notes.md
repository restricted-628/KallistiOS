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

**Scoped exception, 2026-10-11:** The owner authorizes a fork-local SH4ZAM
matrix-alignment correction without upstream coordination. Preserve the
official pinned submodule and licenses, with an explicit exact-base patch and
source verification. This does not authorize arbitrary vendor edits or moving
pins. Native matrix alignment and raw-buffer contracts must agree without
adding repeated runtime checks or broad memory clobbers.

**Dependency reconciliation, 2026-10-20:** SH4ZAM advances from `c18faab` to
`9a214a2` with the same explicit alignment patch. The new named quaternion
constructors use their documented axis-product order. The deprecated
`from_angles_xyz` now means XYZ rather than its previous YXZ behavior; no
tracked fork consumer uses that deprecated entry. This does not switch graphics
algorithms, fiber providers, language defaults or the compiler.

## D08 — Language intent is not build configuration

**Accepted direction:** The owner prefers C23 for the fork; upstream patches
must respect upstream requirements. **Verified snapshot:** `environ_base.sh`
still sets `KOS_CSTD="-std=gnu17"` at `ebfee67bccd1`. Do not describe the target
as wholly migrated to C23 or change the standard merely to make this note true.
Record actual compiler version and flags separately for host and SH-4 lanes.

**Toolchain reconciliation, 2026-10-20:** Retain GCC; importing upstream's
experimental LLVM configuration does not switch this fork to Clang. Environment
setup now lives under `env/`, with root compatibility entry points and GCC
selection when older configs omit `KOS_TOOLCHAIN`. Preserve architecture setup
before final flag expansion and retain SH4ZAM linkage. GNU17/GNU++17 defaults
remain unchanged; individual consumers may explicitly select newer standards.

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

## D16 — Host preparation and non-repeated graphics admission

**Accepted, 2026-10-03:** Move asset-dependent preparation and expensive content
validation to the development PC wherever practical. The primary optimization
goal is eliminating unnecessary repeated checking and reconstruction, not adding
new preparation/validation layers. A cooked loading path should establish the
necessary loaded-buffer integrity, layout and bindings once, then reuse those
facts while the admitted storage remains stable. Rendering retains checks
justified by changing values, resources and output capacity, not repeated scans
of unchanged asset content. Existing general-purpose checked APIs retain their
contracts; optimized paths must not chain them and repeat the same work.

Authority: owner's host-offload direction and subsequent clarification that
removing redundant checks is the goal. Loading cost and frame cost are separate;
neither a source call count nor host cooking alone proves an FPS improvement.
First bounded implementation target is ordinary cooked-cache single-admission
loading. Compiled skin/morph/animation formats remain separate follow-ups, not
implemented features. Coordinate graphics asset interfaces with storage/codec
owners; keep numerical behavior and file/I/O/RAM costs explicit.

**Implementation, 2026-10-03:** Ordinary PCC1 now has header-only sizing and a
single-admission native draw constructor; chunk-scene uses that path. Existing
checked APIs and outer-container/codec checks remain intact. Host operation
counts confirm three full PCC1 admissions become one; timing is unmeasured.
See the matching CHANGELOG.fork.md entry for validation limits.

## D17 — Sequential, source-led subsystem implementation

**Accepted, 2026-10-03:** Work one helper's subsystem at a time, with named
interface reviews instead of independently advancing all backlogs. Automatically
read the supplied documentation and relevant SDK material, inspect applicable
SDK binaries with Ghidra, and compare fork/upstream implementations before
proposing fixes. No repeated owner reminder is needed within an assigned scope.
The original blanket Integration review before implementation is superseded by
the 2026-10-11 amendment below; source-only exceptions still need that review.
An old report, SDK directory listing or decompiler guess
does not establish a fact; contradictory manuals/binaries remain explicit.

Use `doc/agents/implementation-plan.md` for the dependency-ordered first pass,
source gate and completion criteria. Private control records carry live task
assignments and source paths. This changes workflow, not production behavior;
it neither certifies earlier deliveries nor authorizes new publication, hardware
writes, proprietary code imports or broad rewrites.

### D17 clarification — full Kernel responsibility

**Owner clarified, 2026-10-03:** The phased Kernel investigation is to cover
all of its primary and shared responsibilities, not repeatedly narrow back to
G2. Establish a current coverage map, including inherited implementation, then
review the complete startup sequence and remaining component areas. Keep
cross-lane ownership and per-phase evidence/implementation gates; broad research
authority is not permission for wholesale runtime changes or publication.

### D17 amendment — owner-delivered implementations

**Accepted, 2026-10-11:** To avoid paying for the same implementation twice,
the assigned subsystem owns investigation, implementation, focused validation
and a review/commit-ready handoff. Within a bounded assigned issue and established
contracts, complete the source gate and proceed without a separate routine
research-only stop. Integration reviews the actual diff and evidence, performs
appropriate integration checks and handles authorized commits/publication;
requested corrections return to the owning agent.

The delivery checklist and stop conditions live in
`doc/agents/implementation-plan.md`. Evidence exceptions, unresolved design or
shared-interface decisions, scope expansion and checkout conflicts still need
disposition before dependent code. Explicit audit-only assignments and specific
holds remain; parked lanes are not restarted by this policy. No runtime behavior
or test result changes, and no new commit/push authority is granted.

### D17 restart exception — parallel bounded delivery

**Accepted, 2026-10-11:** The owner's subsequent request to get all helpers
working authorizes a parallel round, replacing parking for its assigned tasks.
Each lane gets a bounded deliverable and isolated checkout; Integration retains
serialized review/integration. Shared-interface coordination, D11's networking
research hold and unresolved failure/hardware policy are not lifted. The existing
queue remains a dependency order, not permission to advance whole backlogs.

### D17 clarification — review once, integrate the delivered patch

**Accepted, 2026-10-12:** The owner identified repeated investigation and patch
reconstruction as wasted work. Keep one owner, patch and compact evidence handoff
per issue. Integration reviews the exact diff and remaining acceptance gaps,
reuses matching artifacts, and returns concrete corrections instead of repeating
the investigation or independently rewriting the fix. Reopen only named evidence
gaps, contradictions or affected base/interface changes. A missing execution
check is a bounded validation task. Track disposition and one next action in a
single private queue; integrate accepted changes individually when authorized.
The acceptance loop in the implementation plan operationalizes this policy;
source/hardware gates and publication authority are unchanged.

## D18 — Routine Integration publication authority

**Accepted, 2026-10-12:** The owner explicitly authorized committing the reviewed
merge/workflow changes to the fork and clarified that Integration may make
commits for this kind of work. Once the required review and validation pass,
Integration may commit/publish routine fixes, accepted helper deliveries and
approved upstream merges to fork master without another permission round. Use
the GitHub plugin and inspect the exact outgoing diff. This supersedes a separate
publication checkpoint for those changes, not source/validation gates, explicit
implementation holds, unresolved policy decisions, unrelated branch updates,
force-pushes, published-history rewrites or upstream PR creation. Helpers still
deliver to Integration unless separately authorized to publish.

## D19 — Selective KOS-native virtual memory

**Accepted direction, 2026-10-14:** Add useful memory-management capabilities
around KOS and this fork's contracts, not another OS's process architecture.
Preserve direct-mapped P1 code/data and ordinary allocation behavior; provide
opt-in translated regions so applications can choose protection, backing-store
flexibility and their associated costs. P1 code may access translated data.
MMU-on does not imply universal translation or protection from direct aliases.

Threads and their fibers retain the existing shared-address-space/carrier
contract. Resident pages may still incur TLB misses. Blocking storage faults,
fiber-local suspension, DMA pinning, reclamation policy and fault recovery need
explicit contracts; no automatic scheduling or isolation guarantees follow.
Do not silently replace malloc, require the Service Executor, import process
slots, revive cross-system quarantine, or claim that arbitrary faults are safely
recoverable. Kosh isolation remains a possible later design, not a prerequisite.

The original direction did not itself establish an implemented VM service or API.
Kernel owns the phased implementation with named interface reviews. SDK research
remains private; reuse inspected evidence and independently implement the selected
capabilities. Validation must distinguish source, host, target and hardware results.

### D19 milestone — resident ordinary-data regions

**Accepted, 2026-10-18:** `<kos/vm.h>` and `kernel/mm/vm.c` implement explicit
reserve, zeroed commit, protect, decommit, release, copied query and empty-space
destruction. Arenas exclude OCRAM. The application owns context/ASID selection
and must quiesce raw-pointer users before mutation; ordinary allocation and P1
access remain unchanged. See `doc/resident-vm.md` for the complete contract.

Host regressions/sanitizers, GCC 16.2.0 kernel/probe builds and translated-data
execution across core fibers/carrier preemption pass in both modes of a private
strict-MMU Flycast runtime. An unchanged emulator quadword-alignment assertion
fails separately; this aligned-data probe does not establish full MMU conformance.
Physical cache behavior, prohibited-write faults, alternate-provider execution
and performance remain unverified. Fault recovery, pinning, file-backed paging,
managed stacks and executable overlays are not part of this milestone.

### D19 milestone — prepared resident fault tickets

**Accepted, 2026-10-18:** Explicit one-page zero-commit and guard tickets prepare
backing and page-table storage in thread context. The exception path may publish
only that prepared resident mapping for an eligible interrupted state; it does
not allocate, block, perform I/O or suspend a fiber. Ticket lifetime and mapped
page lifetime remain distinct. Unarmed and ineligible faults retain fatal policy.
See `doc/resident-vm.md` for admission, ownership and query contracts.

Production-code host checks, GCC 16.2.0 builds/links, resident recovery/state
probes and complete core/exclusive-addon provider tests pass in the recorded
private strict-MMU emulator configurations. Fatal reporting uses the selected
debug device rather than stdio/VFS; ordinary logging is unchanged. Generic and
VM2 fatal cases reach the real exit sequence, but direct-ELF launch stack reuse
prevents a genuine loader return in that setup. Real-loader completion and
physical teardown remain deferred/unverified, not passed. Dynarec alignment,
target raw-gUSA/nested rejection and physical cache/timing remain explicit limits.
This milestone does not implement DMA pinning, disc-backed paging, managed
stacks or executable overlays, and does not change ordinary allocation defaults.

### D19 milestone — resident pin tokens and physical byte runs

**Accepted, 2026-10-18:** Independent pin tokens retain committed ordinary-data
backing and mapping interpretation. Live fault tickets exclude acquisition;
covered pages reject mutation until their last pin is released. Physical runs
describe exact useful CPU-physical bytes, not device bus addresses or presumed
contiguity. Copied metadata grants no independent lease. The final owner must
close admission and retire all handle users and memory/device consumers before
release; cancellation, callback completion or fiber destruction alone is not
retirement. See `doc/resident-vm.md` for the API and failure-order contract.

Actual-source host/sanitizer regressions, GCC 16.2.0 build/link and recorded
core/exclusive-addon pin and carrier-blocking emulator probes pass. Provider
executions use accepted provider archives with a fresh VM object, not fresh
whole-provider rebuilds. Hardware DMA/cache and real-loader behavior remain
unverified, with existing emulator alignment limits retained. Pinning does not
add cache synchronization, transport admission, automatic lifetime management,
file-backed residency or executable overlays. Ordinary allocation, P1 access
and provider defaults are unchanged.

### D19 milestone — explicit read-only file residency

**Accepted, 2026-10-20:** File-owned ordinary-data reservations support bounded,
synchronous one-page prefetch, eviction and reload from a stable immutable
regular-file source. One private cursor and reusable page-sized P1 staging
buffer serve each file; exact reads and initialized backing precede read-only
publication. Resident-page budgets are explicit, not a whole-system RAM cap.
Consumers acquire independent VM3 READ pins before use; prefetch alone grants
no lease. Eviction and close reject live pins or loads. Cancellation does not
abort storage I/O or retire consumers. Final owners must join users before close.

Production-code host/sanitizer checks, GCC 16.2.0 build/link and recorded
core/exclusive-addon interpreter/dynarec direct-disc probes pass. The example
uses ISO9660 /cd through direct GD-ROM, not an embedded RAM filesystem.
Provider probes reuse accepted archives with a fresh VM object. Emulator
optical-source results do not certify physical media, cache/DMA visibility,
real-loader return or timing; no ISO mount-drain result is claimed.
See `doc/resident-vm.md` for source, close and memory-accounting contracts.

This is explicit residency, not transparent page-fault I/O, compressed paging,
writeback, managed stacks or executable overlays. Ordinary allocation, P1
access, provider defaults and the physical-drive scheduler remain unchanged.

## D20 — Overlay build foundation and runtime boundary

**Accepted build-time slice, 2026-10-20:** `utils/kos-overlay` describes a linked
SH executable's execution slot, image identity, file ranges and exact zero tails.
It keeps execution addresses separate from physical backing and supports plans
for P1 and translated addresses without claiming either is reserved or executable.
Host tests and SH-4 compile/link fixtures pass; this initial slice did not
implement runtime loading.

The reviewed first runtime direction uses explicitly reserved P1 RAM, synchronous
staging and residency references covering suspended calls and escaped pointers.
Translated executable publication remains a separate Kernel contract; VM1–VM4
are ordinary-data services, not permission to execute their mapped pages.
The host plan is not a frozen runtime ABI, image-subset admission or a new archive
format. Existing ELF libraries, fiber providers and storage defaults are unchanged.

**Accepted fixed-P1 runtime slice, 2026-10-20:** The owner's continuation and
the reconciled Kernel/Fibers/Storage reviews establish the independent `DCo@` v1
package and optional `libkosoverlay`. This supersedes the initial host-only
implementation status, not the executable-MMU boundary. Build the addon explicitly;
no default discovery or dependency change. It is not `MWo@` binary/debugger
compatibility and does not transplant a vendor implementation.

Use trusted resident package identity, a restricted unstripped static-C SH4 ELF,
and a caller-reserved 32-byte-aligned P1 slot of at most 1 MiB. Validate actual
RAM/stack limits; application linker reservation and excluding physical aliases
remain caller responsibilities. The synchronous loader consumes immutable direct
ISO9660 `/cd` content once through 32 KiB staging, hashes every package/ELF byte,
zeros exact BSS, closes its private descriptor, and synchronizes caches before
notification/admission. It adds no physical-drive scheduler or DMA destination API.

Stable caller tokens retain a generation across suspension and all return paths;
escaped pointers/callbacks retain their own tokens until fully retired. Busy
references refuse replacement. Preparation failure preserves the old image;
failure after its unloaded notification leaves EMPTY. Destruction requires
external joining of callers. No forced carrier/fiber teardown, foreign internal
error wakes, source replacement, implicit cancellation or total deadline is
supported. The source-lifetime exclusions are part of the private-close proof.

Host packaging and actual-C sanitizer/failure tests, SHA differential checks,
target builds/link-map reservation and direct-disc execution pass. Both complete
fiber providers pass separate Flycast interpreter/dynarec runs. Physical cache,
timing, boot/shutdown and peak-resource measurements remain unverified. This
does not authorize translated execution, demand paging, dynamic imports,
relocation, constructors, TLS or unwinding through overlays.

### D20 milestone — resident executable VM foundation

**Accepted, 2026-10-21:** `kos/vm_exec.h` supplies a separate executable-region
owner in the existing VM space registry. Ordinary-data VM/file/pin APIs cannot
operate on these regions. Slots are fully resident, 4 KiB-aligned, at most 1 MiB,
and contained in the existing translated arena below OCRAM. A copied page plan
admits absent, read-only, read/execute and read/write pages, not writable code.

Preparation preserves a published image on failure. A valid begin attempt is
irreversible; subsequent failure retains the load/backing until successful abort
or cleanup. Synchronous write/zero operations consume resident sources without
exposing private backing or using translated destinations. Publication and
retirement use physical operand-cache maintenance, execution-VMA instruction
invalidation and ASID TLB retirement; backing is freed only after retirement.

The application owns one coherent selected context for the union of live
translated uses, including suspended returns and escaped callbacks. Boundary
checks verify table identity, frozen ASID, hardware selection and supported MMU
mode; they are not an enforced lease or per-resume repair. No per-fiber ASIDs,
automatic context switching, demand paging or Storage/DMA admission is added.

This accepts the Kernel foundation, not a translated `libkosoverlay` consumer.
The existing fixed-P1 DCo@ profile remains unchanged. Host/sanitizer and focused
cache/TLB models, target build/link and both exclusive providers' direct-disc
Flycast A/B/A fixtures pass. Full consumer token/notification, independent-carrier
preemption and retained-callback tests remain required. Physical cache-color,
IIX, instruction reuse and timing validation remain separate unrun gates.

### D20 milestone — explicit translated overlay consumer

**Accepted software slice, 2026-10-21:** `ovl_slot_create_vm` binds the optional
addon to the existing executable VM owner. Independent DCo@ v1 profile 2 uses
4 KiB pages in the accepted translated arena; profile 1 and the P1 constructor
remain unchanged and do not require enabling the MMU. There is no automatic
backend choice, demand-paged execution, relocation or provider/scheduler change.

Prepare the copied page plan before old-image notification; write/zero through
the synchronous Kernel interface, verify the complete package, close the private
source once and publish before admission. Acquisition checks the selected
context before exposing a view. Applications still own the stable context
interval across all live carriers, suspended returns and escaped callbacks;
no per-resume check or ASID lease is introduced.

After destructive replacement begins, failed abort/retire retains ownership and
refuses loading/destruction. Unload retries cleanup without repeating notification.
PREPARED abort preserving the old image does not reopen consumer admission:
explicit retirement must still succeed. Primary load and source-close errors
remain distinct. Final teardown requires joined users and released tokens.

Kernel, Fibers and Storage accepted the source interfaces. Packaging and actual-C
sanitizer tests, GCC16.2 links and four combined direct-disc emulator runs pass:
both exclusive providers in interpreter/dynarec, including actual-VM fault-injected
cleanup. The original fixture checks five instruction pages, staging crossings,
physical replacement/reuse, carrier preemption and retained callback suspension.
Component memory accounting is documented; full call-chain/heap high-water,
physical cache/IIX/DMA/timing and real boot/shutdown remain unverified. OVL4 is
not part of this acceptance.

### D20 scope decision — overlay demand paging deferred

**Owner decision, 2026-10-21:** Defer OVL4 demand paging for overlays. It is
not a requirement for completing the accepted resident overlay functionality
and is not the next automatic implementation task. Preserve fixed-P1/no-MMU
overlays and fully resident translated overlays unchanged. Keep the paging
proposal for later reconsideration; do not start its fault-continuation design,
partner reviews or implementation without an explicit resumption. This deferral
is specific to overlays, not a cancellation of other VM work. Existing hardware
and memory high-water validation gaps remain open. No runtime change follows
from this decision.

### D20 milestone — explicit overlay project tooling

**Accepted host/build slice, 2026-10-21:** Games explicitly choose named slots,
member placement and replacement times. `utils/kos-overlay/project.py` generates
P1 linker reservations, member base fragments and resident package catalogs from
a versioned project manifest. It reuses DCo@ v1 unchanged. Final resident-link
checking covers dedicated P1 NOBITS reservations, section overlap and explicit
RAM/heap/stack bounds; VM backing and physical aliases remain runtime/application
responsibilities. This is not generic module/ABI negotiation or scheduling.

Host schema/generation/refusal tests and actual GCC16.2 SH-4 member/catalog links
pass. No new runtime, emulator, hardware or installed-memory claim follows.
Demand paging remains deferred; compact release packaging, debugger integration
and asynchronous loading are not included in this slice.

### D20 milestone — project-driven P1 example

**Accepted consumer slice, 2026-10-21:** The original direct-disc overlay example
now consumes the project tool's layout and trusted catalog instead of a bespoke
two-link identity pipeline. Its explicit 8 KiB P1 slot at `0x8c100000` is reserved
by a dedicated NOBITS section before the normal heap boundary, using a private
linker-script copy. The fixed layout reserves any intervening gap as well; game
authors must choose placement deliberately. The shared linker script is unchanged.

Host tests, both exclusive GCC16.2 fiber-provider links and all four Flycast
provider/mode runs pass with the original guest test results. Runtime, DCo@
format and provider behavior remain unchanged. No MMU dependency is introduced;
hardware cache/timing/boot and memory high-water validation remain open. OVL4
remains deferred.

## Updating this register

Use a stable decision ID. Record the decision, reason/tradeoff, user authority,
implementation anchor and validation limits. Mark a replaced decision as
superseded with a link to its replacement rather than silently erasing history.
Integration owns this file; subsystem chats propose updates in their handoffs.
