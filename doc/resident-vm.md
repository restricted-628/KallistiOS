# Resident data regions

`<kos/vm.h>` adds explicit virtual reservations and owned backing above the
Dreamcast checked MMU helpers. Ordinary malloc and P1 code/data are unchanged.
This is resident CPU data, not executable loading, managed stacks, paging, DMA
pinning, NX or process isolation. Explicit one-page resident fault tickets are
described below; they do not provide disc-backed paging.

Create an MMU context with a caller-assigned unique live ASID, then create one
VM space borrowing that context and an empty arena. Addresses and lengths must
be whole 4096-byte pages within `[0x00001000, 0x7c000000)`; the upper aperture is
OCRAM, not an ordinary translated arena. Creation rejects existing dynamic
mappings. The caller must exclude static/global mappings too, and must not
mutate the context externally until destroying the space. Existing mappings
outside the arena may remain unchanged. Context destruction and ASID reuse
remain caller responsibilities after all uses and mappings are retired.

Table/ASID selection is explicit and global, not performed by VM or the thread/
fiber scheduler. The default MMU table lookup must remain installed. Enable
translation before dereferencing committed P0 pointers. Every pointer requires
the matching selected context/ASID; switching to a fiber does not select one.

| Operation | Contract |
| --- | --- |
| reserve | NULL requested address selects lowest first fit; otherwise exact placement. No physical backing yet. Fixed overlap is EEXIST; exhaustion/metadata failure is ENOMEM. |
| commit | Entire range must be reserved (EALREADY for any committed page). Fresh individually allocated pages are zeroed. Allocation failure rolls back the whole range. |
| protect | Every page must be committed (ENOENT for a reserved hole). Preserve backing and bytes; unchanged protection is a no-op. |
| decommit | Retire and free backing, preserving the reservation. Reserved holes are successful no-ops. Contents are discarded; recommit starts at zero. |
| release | Retire all committed backing and release the entire reservation, including mixed states. The address must be its exact base (ENOENT otherwise). |
| query | Any byte in the arena returns a copied snapshot of the maximal run with one reservation, state and protection. It retains no ownership. Free runs end at the next reservation or arena end. |
| destroy | Only empty spaces can be destroyed; EBUSY leaves reservations intact. The borrowed context survives. |

All mutating ranges are nonzero/aligned and remain in one reservation; adjacent
reservations cannot be combined. Invalid arguments/ranges use EINVAL; a start
in free space uses ENOENT. Commit/protect accept only VM_READ and VM_READWRITE,
otherwise ENOTSUP. These map to privileged read-only/read-write, copy-back,
non-shared 4 KiB entries with D=1. Direct aliases bypass these protections.
There is no retained-backing NOACCESS state or executable permission API.
Unarmed reserved pages and forbidden writes use the existing fatal fault policy.
Only an explicitly armed, prepared ticket permits the recovery described below.

Integer operations return 0 or -1 with errno; create returns a handle or NULL.
Outputs stay unchanged on error; success need not clear errno. Stale handles
and reused numeric addresses are caller lifetime errors, not generation tokens.
Never free a returned P0 pointer with the ordinary allocator.

## Callers and ownership

All calls, even query, require ordinary thread context with BL/IMASK clear and
reject IRQ/exception/masked entry with EPERM before locking. No caller may hold
an allocator lock, SQ transaction or other resource prohibiting blocking.
Pre-switch hooks and masked TLS destructors cannot call this API. Lock order is
space mutex then allocator lock; no user callbacks or cooperative yield occur
under the space mutex. A blocking wait stalls the carrier and its sibling fibers.
No executor or particular fiber provider is required; providers remain exclusive.

The mutex serializes APIs, not raw loads/stores. Quiesce all affected users,
including suspended continuations and callbacks on every carrier, before mutation.
Before release, quiesce the whole reservation. Before destroy, close admission
and join every manager user, including threads waiting for its lock. An inactive
context, cancellation flag or finished fiber is not proof of quiescence. There
is no automatic reader count and no EBUSY promise for untracked users.

Keep arguments/results, metadata, stack, scheduler/provider/cancellation storage
and exception workspaces in direct resident memory. Managed pages are ordinary
application data only: no application aliases, DMA/device/SQ users or stacks.
These are caller preconditions, not runtime pointer-discovery scans.

## Backing, rollback and costs

Commit stages every aligned page before mapping. It zeros and purges the direct
alias before allowing a differently colored P0 alias. After publication, the VM
does not touch that direct alias until translated use has retired. Failure after
some mappings were installed unmaps those pages before freeing any staged backing.
Decommit/release likewise retire physical cache state and ASID-tagged translations
through the checked MMU helpers before free, including inactive contexts.
Protection changes validate every page before remapping existing backing; their
page tables already exist, so they cannot require new allocations. No cache mode
or low-level MMU policy is changed.

Atomicity is at API completion under caller quiescence, not an atomic hardware
multi-page switch. Rollback preserves observable reservations, permissions and
unrelated contents; internal empty page-table allocation shape may change.

With SH-4 GCC 16.2.0 the structures use 12 bytes per reserved page, 12 bytes per
reservation and 4140 bytes per space, before allocator overhead. The space
includes a fixed 1024-entry ticket-table root. A populated ticket table costs
2052 bytes per 2 MiB span; each ticket costs 108 bytes. The ASID registry is
1024 bytes system-wide. An armed zero-commit ticket also owns one staged page. Backing adds 4096
bytes per committed page plus allocator/alignment overhead, without a physical-
contiguity promise. The caller-owned MMU root costs 4100 bytes and populated
second-level tables cost 6144 bytes per 2 MiB virtual span. Reservations consume
page metadata even before commitment. These sizes are layout costs, not a budget
or guarantee of allocation success.

First-fit/list search is linear in reservations; page transitions/query runs are
linear in affected pages plus allocator and existing cache-tag scans. Retirement
can mask interrupts for significant time. Resident means physical backing exists,
not a guaranteed TLB hit or fixed execution time. Ordinary loads/TLB refills gain
no VM-wide admission scan. There is no FPS or latency guarantee.

Validation entry points: `utils/vm-test` compiles actual VM and checked MMU table
code with host allocation/IRQ/cache/mutex substitutes; the `basic/mmu/resident-vm`
target probe checks translated P0 data from P1 across fibers and carrier preemption
when the runtime implements general translation. Source, host tests, target link,
emulator execution and physical cache/hardware proof are separate evidence levels.

## Prepared one-page faults

`vm_fault_arm(space, page, kind, protection, &ticket)` prepares one aligned
4 KiB page entirely in ordinary thread context. It allocates all backing and
lookup/page-table storage before admitting accesses. Failure leaves the page's
state and contents unchanged. Each successful arm receives a space-local
monotonic 64-bit generation; exhaustion returns EOVERFLOW, without wrapping.

| Kind | Before arm | While armed | First permitted access |
| --- | --- | --- | --- |
| `VM_FAULT_ZERO_COMMIT` | RESERVED | RESERVED, with staged zeroed resident backing | Publish prepared mapping; become COMMITTED. |
| `VM_FAULT_GUARD` | COMMITTED, same requested protection | GUARDED, retaining bytes, backing and protection | Restore prepared mapping; become COMMITTED. |

Only a read/fetch or write TLB miss with the correct selected table/ASID can
resolve. A write cannot consume a read-only ticket. The saved interrupted state
must have MD=1, RB=0, BL=0 and IMASK=0, with direct P1 code and a valid direct
stack. Raw soft-gUSA markers are rejected before logical stack conversion.
Direct child stacks are eligible; no carrier-stack membership or provider scan
is performed. Exception workspaces, metadata, hook code and all their data must
remain resident and directly mapped. Managed pages cannot contain stacks,
executable code, scheduler/provider storage or callback metadata.

The global exception hook must be absent or a resident, nonfaulting observer
that preserves the frame, carrier/TLS/reentrancy/errno, table/ASID, fault registers
and handler selection. It cannot allocate, wait or mutate tickets. Legacy hook
order and filtering are unchanged; intentionally mutating hooks have no managed
preservation guarantee. Ticket snapshots describe the post-hook, post-dispatcher
state before resolver mutation. The read event cannot distinguish fetch from data.

Resolution uses a fixed ASID/two-level lookup and publishes a prepared PTE before
the existing LDTLB and exception return. It performs no allocation, cache purge,
locking, I/O, user callback or scheduling. Only ARMED-to-RESOLVED progress permits
retry; the same continuation resumes with its saved state unchanged. Nested
exceptions and other fault classes retain existing fatal handling. This does not
make nested faults recoverable or widen page permissions.

`vm_fault_query` copies state/snapshot under short IRQ exclusion. `vm_query`
returns coherent runs using a sequence-validated scan under the space mutex:
it retries if a ticket resolved while scanning. No new arm can enter under that
mutex, and the finite number of armed pages is less than 2^19, so a complete
32-bit sequence wrap cannot occur during one scan. A short wrap across zero is
safe. The scan does not mask interrupts for the length of a reservation.

Every live ticket, including RESOLVED, makes overlapping commit/protect/decommit,
reservation release and duplicate arm fail EBUSY before any mutation. After
stopping page accesses, release the ticket first. Releasing an unresolved zero
arm discards its staged page and leaves RESERVED; releasing an unresolved guard
restores COMMITTED and its original protection/bytes. Resolved backing stays
COMMITTED. Prepared table slots stay pinned until ticket release, including when
unrelated mappings in that table disappear.

Before `vm_fault_release`, close ticket admission and retire **all other handle
users and API/mutex waiters**, suspended continuations and callbacks. Separately
retire all affected-page users. One sole releaser then invalidates the handle;
the space/context/ASID must remain alive throughout. Copied snapshots and numeric
generations grant no lease or stale-handle safety. Cancellation or a finished
fiber does not prove either retirement condition. The API supplies no automatic
reference counting, callback draining or page-user discovery.

The `basic/mmu/resident-fault` probe exercises ordinary/delay-slot accesses on
carrier and direct child stacks, comparing integer/special registers and both
FPU banks, plus carrier TLS/errno and ticket snapshots. Its current-worker core
results cannot substitute for separate complete core and exclusive SH4ZAM bundle
validation. Fault-conformance, physical cache/timing and hardware evidence remain
separate. VM3 pin/span and VM4 file-backed residency are later contracts: neither
storage I/O nor LZ4 decompression belongs in this resident resolver.
