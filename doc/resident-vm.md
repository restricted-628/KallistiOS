# Resident data regions

`<kos/vm.h>` adds explicit virtual reservations and owned backing above the
Dreamcast checked MMU helpers. Ordinary malloc and P1 code/data are unchanged.
This is resident CPU data, not executable loading, managed stacks, paging, DMA
pinning, guard-fault recovery, NX or process isolation.

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
Reading a reserved page or writing a read-only page uses the existing fault
policy; the API supplies no automatic recovery handler.

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

With SH-4 GCC 16.2.0 the structures use 8 bytes per reserved page, 12 bytes per
reservation and 28 bytes per space, before allocator overhead. Backing adds 4096
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
