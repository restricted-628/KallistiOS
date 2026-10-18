/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */

/** \file kos/vm.h
    \brief Opt-in resident Dreamcast data regions.

    A space borrows a caller-owned MMU context and unique live ASID. Only one
    space may use a context; callers exclude static mappings in its arena and
    all external context mutations until destruction. Context selection remains
    explicit and global. This API does not enable the MMU or select an ASID.

    Every operation may block the carrier thread. IRQ/exception and BL/IMASK-
    masked callers fail with EPERM before locking. No SQ ownership, allocator
    lock, provider hook or masked TLS destructor may enclose these calls.

    Callers quiesce affected raw-pointer users, suspended continuations and
    callbacks before mutation. A mutex does not track memory accesses. Before
    destroy, close admission and join all manager users, including lock waiters.
    Handles, arguments/results and metadata must remain directly mapped.

    Ordinary CPU data only: no managed stacks, executable code, DMA/device/SQ
    use or application-created aliases. Fault-driven commitment requires an
    explicitly prepared one-page ticket; see vm_fault_arm(). Direct aliases
    bypass protection. Never pass a returned P0 pointer to free(). Resident
    pages can still miss in the TLB. See doc/resident-vm.md for the full contract.
*/
#ifndef __KOS_VM_H
#define __KOS_VM_H

#include <kos/cdefs.h>
#include <arch/mmu.h>
#include <stddef.h>
__BEGIN_DECLS

typedef struct vm_space vm_space_t;
typedef enum { VM_READ = 1, VM_READWRITE = 3 } vm_prot_t;
typedef enum { VM_FREE, VM_RESERVED, VM_COMMITTED, VM_GUARDED } vm_state_t;

/** Copied snapshot; does not extend the lifetime of any pointer. */
typedef struct {
    void *region_base;       /**< Entire reservation, or NULL for free space. */
    size_t region_size;      /**< Zero for free space. */
    void *run_base;          /**< Maximal matching run containing the address. */
    size_t run_size;         /**< Ends at reservation/arena boundary. */
    vm_state_t state;
    unsigned protection;    /**< Zero for FREE/RESERVED; retained vm_prot_t for COMMITTED/GUARDED. */
} vm_info_t;

/** Borrow an empty, whole-page arena in [0x1000, 0x7c000000).
    Return NULL on EINVAL, EEXIST (dynamic mapping), ENOMEM or caller error.
    The caller must exclude static/global mappings; they cannot be detected
    by a dynamic page-table scan. The context remains caller-owned. */
vm_space_t *vm_space_create(mmucontext_t *context, void *arena, size_t bytes);
/** Destroy an empty space; EBUSY leaves a space with reservations intact. */
int vm_space_destroy(vm_space_t *space);
/** Reserve whole pages, first fit for requested==NULL, otherwise exact placement.
    EEXIST on fixed overlap; ENOMEM on exhaustion/allocation failure. */
int vm_reserve(vm_space_t *space, void *requested, size_t bytes, void **result);
/** Commit an entirely reserved range to fresh zeroed pages; EALREADY if any
    page is committed. Allocation failure leaves the whole range reserved. */
int vm_commit(vm_space_t *space, void *address, size_t bytes, vm_prot_t prot);
/** Change protection of a wholly committed range; ENOENT for any reserved hole.
    Same protection is a no-op. Contents and backing are preserved. */
int vm_protect(vm_space_t *space, void *address, size_t bytes, vm_prot_t prot);
/** Retire/free backing, leaving the range reserved. Reserved holes are no-ops. */
int vm_decommit(vm_space_t *space, void *address, size_t bytes);
/** Retire/free an entire reservation, including mixed page states.
    ENOENT if no reservation starts at this address. */
int vm_release(vm_space_t *space, void *reservation_base);
/** Query any byte in the arena; free runs and reservation/page runs are distinct. */
int vm_query(vm_space_t *space, const void *address, vm_info_t *result);

typedef struct vm_fault vm_fault_t;
typedef enum { VM_FAULT_ZERO_COMMIT, VM_FAULT_GUARD } vm_fault_kind_t;
typedef enum { VM_FAULT_ARMED, VM_FAULT_RESOLVED } vm_fault_state_t;
typedef enum { VM_FAULT_READ_OR_FETCH, VM_FAULT_WRITE } vm_fault_access_t;
typedef enum { VM_FAULT_MANAGED } vm_fault_class_t;
typedef enum { VM_FAULT_RETRY } vm_fault_outcome_t;

/** Post-global-hook/post-dispatcher snapshot, before resolver mutation.
    Identity addresses are copied values, not retained pointer leases. EXPEVT
    does not distinguish instruction fetch from data read. */
typedef struct {
    uint32_t event, address, pteh, mmucr, pc, sr, sp;
    uintptr_t table, carrier;
    unsigned asid;
    vm_fault_access_t access;
    vm_fault_class_t classification;
    vm_fault_outcome_t outcome;
} vm_fault_snapshot_t;

/** Copied ticket state; kind/generation also identify its resolution snapshot. */
typedef struct {
    void *page;
    vm_fault_kind_t kind;
    vm_prot_t protection;
    uint64_t generation;
    vm_fault_state_t state;
    bool snapshot_valid;
    vm_fault_snapshot_t snapshot;
} vm_fault_info_t;

/** Prepare and arm exactly one aligned 4 KiB data page, failure atomically.
    ZERO_COMMIT requires RESERVED and stages zeroed backing; GUARD requires
    COMMITTED with identical protection and preserves backing/bytes. The first
    permitted ordinary-thread access restores/commits the mapping and retries
    the same continuation once. Forbidden writes never consume a read-only arm.

    All ticket APIs have the ordinary-thread/blocking restrictions above. Live
    tickets (even resolved) make overlapping VM mutations fail EBUSY. Query
    remains allowed; a guard queries as VM_GUARDED with retained protection.
    Invalid page/kind or guard-protection mismatch: EINVAL; wrong page state:
    EALREADY; no reservation: ENOENT; duplicate ticket: EBUSY; staging failure:
    ENOMEM; generation exhaustion: EOVERFLOW; unsupported protection: ENOTSUP.

    Managed resolution requires no global IRQ hook, or a direct resident,
    nonfaulting observer preserving saved context, errno/TLS/reentrancy, carrier,
    table/ASID, fault/MMU registers and handler selection, without waiting,
    allocation or ticket mutation. Legacy hook ordering/filter behavior is
    unchanged. Mutating hooks have no VM recovery/preservation guarantee.
    Saved MD=1/RB=0/BL=0/IMASK=0 and direct P1 code/valid stack are required;
    raw gUSA is rejected before logical-SP conversion. Direct child stacks are
    eligible without provider scans. Masked hooks/destructors must not fault on
    armed pages. See doc/resident-vm.md for caller-owned lifetime requirements. */
int vm_fault_arm(vm_space_t *space, void *page, vm_fault_kind_t kind,
                 vm_prot_t protection, vm_fault_t **result);
int vm_fault_query(vm_fault_t *ticket, vm_fault_info_t *result);
/** Close admission and retire every other ticket API user/mutex waiter and
    suspended handle/callback owner BEFORE this call, even if the space survives.
    Separately quiesce affected-page users. Exactly one releaser; copied results
    and generations grant no lease/stale-handle safety. Space/context/ASID remain
    alive through completion. Unresolved zero-commit returns to RESERVED;
    unresolved guard restores COMMITTED; resolved backing stays COMMITTED.
    Success invalidates the handle. No automatic lifetime/refcount service. */
int vm_fault_release(vm_fault_t *ticket);

/* Integer calls return 0 or -1 with errno. Outputs stay unchanged on failure.
   Mutating ranges are nonzero, 4096-byte aligned and within one reservation:
   EINVAL for invalid arguments/crossing, ENOENT for a start in free space.
   Commit/protect accept only VM_READ or VM_READWRITE (otherwise ENOTSUP).
   Success need not clear errno. Stale nonnull handles/pointers are invalid;
   address reuse is not guarded by generation tokens. */

__END_DECLS
#endif
