# Resident data regions

`<kos/vm.h>` adds explicit virtual reservations and owned backing above the
Dreamcast checked MMU helpers. Ordinary malloc and P1 code/data are unchanged.
This is resident ordinary data with explicit pin lifetimes and physical-run
descriptions, not executable loading, managed stacks, paging, a DMA adapter,
NX or process isolation. Explicit one-page resident fault tickets are
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
| protect | Every page must be committed (ENOENT for a reserved hole). Preserve backing and bytes; unchanged protection is a no-op only without a live ticket/pin. |
| decommit | Retire and free backing, preserving the reservation. Reserved holes are successful no-ops. Contents are discarded; recommit starts at zero. |
| release | Retire all committed backing and release the entire reservation, including mixed states. The address must be its exact base (ENOENT otherwise). |
| query | Any byte in the arena returns a copied snapshot of the maximal run with one reservation, state and protection. It retains no ownership. Free runs end at the next reservation or arena end. |
| destroy | Only empty spaces can be destroyed; EBUSY leaves reservations intact. The borrowed context survives. |

Commit/protect/decommit ranges are nonzero/aligned and remain in one reservation; adjacent
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
separate. VM4 file-backed residency remains a later contract: neither storage I/O
nor LZ4 decompression belongs in this resident resolver.


## Resident pin tokens and physical byte runs

`vm_pin_acquire` retains already committed ordinary-data backing and its mapping
interpretation. It admits a nonzero arbitrary byte interval wholly inside one
reservation. Every touched 4 KiB page is retained, even for a one-byte request.
It never resolves a fault, consumes a guard, commits pages, elevates protection,
selects a context or performs I/O/cache synchronization.

`VM_PIN_READ`, `VM_PIN_WRITE` and `VM_PIN_READWRITE` describe the consumer's
memory access. Disc-to-RAM is WRITE and requires VM_READWRITE backing. READ
accepts either committed protection and does not prevent CPU writes. Independent
overlapping tokens are supported; neither their counts nor their access intent
serialize concurrent data access.

Acquire validates categories in this order: architectural caller EPERM;
null/zero EINVAL; byte-address overflow EOVERFLOW; outside-arena EINVAL;
unsupported access ENOTSUP; lock error; free start ENOENT or cross-reservation
EINVAL; any live ticket EBUSY; any uncommitted page ENOENT; insufficient write
permission EACCES; counter/allocation arithmetic EOVERFLOW; allocation ENOMEM.
A later-page ticket takes precedence over an earlier-page hole or protection
failure. ARMED and RESOLVED tickets of either kind all exclude acquisition;
release the ticket first. All failures leave outputs and ownership unchanged.

| Operation with a covered page pinned | Behavior |
| --- | --- |
| commit / protect / decommit | EBUSY before partial mutation, including already-committed commit and same-protection protect |
| reservation release | EBUSY, entire reservation unchanged |
| fault arm | EBUSY, either kind |
| space destroy | EBUSY while regions/tickets/pins remain; caller still joins all manager users first |
| query / pin query / run copying | Allowed; copies add no lease |
| independent overlapping acquire | Allowed if admission succeeds and counts do not overflow |
| unrelated page mutation | Existing rules; no exclusion merely from a neighboring pin |

The existing space mutex serializes whole-range validation and changes. Pin
admission allocates all metadata and constructs runs before incrementing counts
and publishing a token. A resolver requires a live ticket and leaves its pointer
installed after resolution; rejecting tickets before reading backing ensures
that an admitted page cannot change from an exception. Ticket arm/release use
the same mutex. Private MMU_VM_PIN remains VM2 table-slot bookkeeping.

### Copying a physical description

`vm_pin_query` returns immutable address, byte length, access intent, run_count,
borrowed context and ASID. Identity copies are not independent leases.
`vm_phys_run_t` contains:

- `physical`: CPU MMU physical **byte** address, not a page number, P1/P2 alias
  or bus address. It uses the same direct backing coordinate as the VM mapping.
- `offset`: logical byte offset from the requested virtual address.
- `bytes`: exact useful byte length, clipped at the first/last page edges.

Runs are in logical order, cover each requested byte exactly once, and coalesce
only if both logical and physical bytes are adjacent without wrap. Virtual
adjacency does not imply physical adjacency. Count is at least one and at most
the number of covered pages. The token fixes this sequence throughout its life.

`vm_pin_runs(pin, first, runs, capacity, &written)` copies
min(capacity, run_count-first) whole runs. Short capacity is successful partial
enumeration; advance by written until the cursor equals run_count. Zero capacity
permits NULL runs and returns zero without progress. first==run_count succeeds
with zero; first>run_count fails EINVAL. written is always required; nonzero
capacity requires runs. Impossible capacity multiplication fails EOVERFLOW.
Other errors preserve all output bytes; success preserves the unused suffix.
Outputs must be valid, writable and nonoverlapping. Query/count/copy chunks all
require the same retained token; no internal array pointer is exposed.

A copied prefix or even a complete array grants no transfer admission or lease.
Enumeration has no allocation/hidden cursor; work is bounded by caller capacity
and remaining runs. Use small capacities when short manager critical sections
matter. Admission/release scan covered pages; acquisition is not constant-time.

### Final owner and retirement

Each acquire creates one independent token, with no retain/clone API. Exactly
one designated owner finally releases it. A transfer requires synchronized
resident publication and acknowledgement before the sender relinquishes its
role. Until acknowledgement, the sender remains responsible. The sole ownership
record cannot be on a child stack that may be reused. Copies, wakes and job-state
labels are not additional leases or acknowledgement.

Before `vm_pin_release`, close new handle/consumer admission and join every
other token API user, including mutex waiters and suspended holders. Separately
retire queued work, DMA/IRQ, chained segments, worker copies, finalizers,
callbacks and suspended consumers, as applicable. Failed submission must drain
anything that escaped; cancel, timeout, terminal status and callback entry do
not prove retirement. Release does not stop hardware or join these users for you.

All pin calls use ordinary IRQ-enabled thread context and may block the carrier.
IRQ/exception/BL/IMASK calls fail EPERM before locking/dereferencing arguments.
Allocator/SQ/provider-hook restrictions remain caller preconditions, not promised
dynamic detection. No new scheduler or Service Executor is required. IRQ
completion can notify a resident owner through an existing supported path; final
release occurs later after callback/IRQ retirement. Never release in masked
TLS destructors or switch/wait hooks.

Yield, preemption and cooperative waits keep the lease live. Destroying a parked
child can abandon its cleanup tail: clean up first or transfer to a live resident
owner. Carrier/executor join does not retire independent external work. The
same ownership rules apply to both exclusive providers. There are no per-fiber
ASIDs or managed stacks.

Any eligible thread can query/copy/release, even with another/no context selected.
CPU dereference still needs the correct explicit context/ASID. Keep space,
reservation, context/ASID and retirement state alive through release completion.
Existing prohibition on external raw context mutation/destruction and application
aliases remains. The mutex/counts cannot protect against violating that rule.

Successful release removes only that token's counts, frees its metadata and
leaves backing committed. It does not unmap, free backing, synchronize caches or
switch context. The handle is then invalid; no stale-pointer/generation or
concurrent double-release safety is promised. Zero pins alone is not proof that
space API users/mutex waiters have retired before destruction.

### Device and cache boundaries

Pins do not imply TLB residency, physical contiguity, hardware scatter/gather,
cache-line ownership, device leases or acceptance of P0 by legacy drivers.
Transport still owns aperture/address conversion, alignment/length/sector rules,
split/bounce policy, deadlines/progress and cache visibility transitions. A P1/P2
cast does not remove a dirty P0 synonym. Existing mapping-retirement cache helpers
are not a new public DMA synchronization API.

Partial 32-byte cache-line edges require ownership of the complete affected
lines/aliases or a separately reviewed bounce/copy path. Retaining a page grants
no access to neighboring payload bytes and does not make CPU/device races safe.
Unquiesced direct-disc DMA retains backing under the existing halt policy.
A later VM-buffer Storage adapter needs its own reviewed admission/cache/lifetime
contract; this service does not modify G1/G2/GAPS or silently admit new buffers.

Memory: one uint32_t count per reserved page and one live-token count per space;
per token, a fixed header plus worst-case one run per covered page. On the SH-4
ABI the page record is 16 bytes (previously 12), the token header 40 bytes and
runs 12 bytes each, excluding allocator overhead; space padding is ABI-dependent.
No allocation occurs during run copying. Host geometry/retirement models and
translated CPU probes remain distinct from physical cache/DMA validation.
