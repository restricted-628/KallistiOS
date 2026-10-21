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

    The distinct kos/vm_exec.h reservations share this owner. Data APIs reject
    executable-owned regions; they never expose, protect or evict that backing.

    Ordinary data only: no managed stacks, executable code, device/SQ access
    or application-created aliases. Pins retain backing and describe physical
    bytes; they do not authorize DMA or change existing driver admission. Fault-driven commitment requires an
    explicitly prepared one-page ticket; see vm_fault_arm(). Direct aliases
    bypass protection. Never pass a returned P0 pointer to free(). Resident
    pages can still miss in the TLB. See doc/resident-vm.md for the full contract.
*/
#ifndef __KOS_VM_H
#define __KOS_VM_H

#include <kos/cdefs.h>
#include <arch/mmu.h>
#include <stddef.h>
#include <stdint.h>
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

/** Explicit read-only file residency. All file-wide caller/lifetime restrictions
    above apply. These synchronous operations may block a child fiber's carrier;
    they never dispatch siblings or initiate I/O from an exception. */
typedef struct vm_file vm_file_t;
typedef enum {
    VM_FILE_ABSENT, VM_FILE_LOADING, VM_FILE_READY
} vm_file_page_state_t;

typedef struct {
    void *requested;             /**< NULL first fit, otherwise whole-page base. */
    uint64_t offset;             /**< Source offset; offset+bytes <= INT32_MAX. */
    size_t bytes;                /**< Nonzero useful length; last page zero padded. */
    size_t resident_limit;       /**< Maximum READY + LOADING pages, in [1,pages]. */
    size_t budget_bytes;         /**< VM payload envelope; see resident-vm.md. */
} vm_file_options_t;

typedef struct {
    const void *address;         /**< Snapshot only, not a use lease. */
    uint64_t offset;
    size_t bytes, pages;
    size_t resident_limit, resident_pages, loading_pages;
    size_t budget_bytes, fixed_bytes, table_reserve_bytes, charged_bytes;
    int cancelled;
} vm_file_info_t;

typedef struct {
    vm_file_page_state_t state;
    size_t valid_bytes;
} vm_file_page_info_t;

/** Own a fresh private O_RDONLY open and new reservation. Source must remain
    immutable, synchronous and seekable through final close. Regular source size
    <= INT32_MAX; fstat/total checked only at open. No shared fd or pathname reopen.
    Generic VM mutations and fault tickets reject the file-owned region EBUSY.
    Outputs unchanged on failure; construction unwinds its private open once. */
int vm_file_open(vm_space_t *space, const char *path,
                 const vm_file_options_t *options, vm_file_t **result);
int vm_file_query(vm_file_t *file, vm_file_info_t *result);
int vm_file_page_query(vm_file_t *file, size_t page,
                       vm_file_page_info_t *result);
/** Load at most one page using exact seek/read and complete initialization.
    ECANCELED for stopped admission, EBUSY for another load, EAGAIN for capacity.
    Prefetch grants no lease: acquire a VM3 READ pin before use and retry if
    eviction won the gap. ABSENT/LOADING CPU accesses retain fatal fault policy. */
int vm_file_prefetch(vm_file_t *file, size_t page);
/** Evict one unpinned page; EBUSY for LOADING or pins. ABSENT is a no-op. */
int vm_file_evict(vm_file_t *file, size_t page);
/** Sticky logical cancellation; may block on mutex, never joins/aborts I/O. */
int vm_file_cancel(vm_file_t *file);
/** Final owner joins all callers/waiters/consumers first. Active load or pins:
    EBUSY with ownership/output unchanged. Success consumes the object and writes
    backend close errno (or zero) separately; never retry a consumed handle/fd.
    Private descriptor ownership, no global descriptor drain/shutdown, no forced
    carrier destruction/error wake and synchronous backend retirement are required
    from construction through close. Generic fs_close errors need not consume. */
int vm_file_close(vm_file_t *file, int *source_close_error);

typedef struct vm_pin vm_pin_t;
typedef enum {
    VM_PIN_READ = 1, VM_PIN_WRITE = 2, VM_PIN_READWRITE = 3
} vm_pin_access_t;

/** Exact useful bytes, in logical order. Physical is a CPU MMU byte address,
    not a page ID, direct alias or bus address. Offset is from the pin address. */
typedef struct {
    uintptr_t physical;
    size_t offset;
    size_t bytes;
} vm_phys_run_t;

/** Immutable copied identity/geometry; no independent pointer lease. */
typedef struct {
    const void *address;
    size_t bytes;
    vm_pin_access_t access;
    size_t run_count;
    mmucontext_t *context;
    unsigned asid;
} vm_pin_info_t;

/** Retain already committed ordinary data within one reservation. Byte endpoints
    may be unaligned; every touched page is retained. READ/WRITE describe the
    consumer's memory access (disc-to-RAM is WRITE). WRITE requires VM_READWRITE.
    No implicit fault, commit, protection change, context switch or cache sync.

    Every live fault ticket, including RESOLVED, excludes pin admission. All
    overlapping mutations (even same-protection protect) fail EBUSY while pinned.
    Independent overlapping pins are allowed; they do not serialize data access.

    Error precedence: caller EPERM; null/zero EINVAL; address wrap EOVERFLOW;
    arena EINVAL; access ENOTSUP; lock error; free start ENOENT/cross-reservation
    EINVAL; any ticket EBUSY; any uncommitted page ENOENT; write protection EACCES;
    counter/allocation arithmetic EOVERFLOW; allocation ENOMEM. Outputs unchanged
    on failure. Space/context/ASID and resident caller state outlive the token.
    See doc/resident-vm.md for the complete ownership/transfer/cache contract. */
int vm_pin_acquire(vm_space_t *space, const void *address, size_t bytes,
                   vm_pin_access_t access, vm_pin_t **result);
int vm_pin_query(vm_pin_t *pin, vm_pin_info_t *result);
/** Copy min(capacity, run_count-first) immutable runs; short capacity is successful
    partial enumeration, not transfer admission. Continue at first+written until
    run_count. Zero capacity permits NULL runs and makes no progress; first==count
    succeeds with zero written, first>count fails EINVAL. written is required;
    nonzero capacity requires runs. Capacity multiplication overflow: EOVERFLOW.
    Errors leave all outputs unchanged; success leaves unused elements unchanged.
    Outputs must not overlap. Retain the same token through count and all copies. */
int vm_pin_runs(vm_pin_t *pin, size_t first, vm_phys_run_t *runs,
                size_t capacity, size_t *written);
/** One final owner closes admission and joins other token API users/mutex waiters
    and suspended holders, separately from retiring all memory/device/IRQ/worker/
    finalizer/callback users, BEFORE release. Cancel/timeout/status is not retirement.
    Ordinary IRQ-enabled thread only; another carrier or inactive context is OK.
    No retain/clone, automatic consumer drain, stale-handle or double-release safety.
    Success invalidates the handle, leaves backing committed and releases only this
    token's counts. Copied runs/identity grant no lease. No IRQ/TLS-hook cleanup. */
int vm_pin_release(vm_pin_t *pin);

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
   Commit/protect/decommit ranges are nonzero, 4096-byte aligned and within
   one reservation:
   EINVAL for invalid arguments/crossing, ENOENT for a start in free space.
   Commit/protect accept only VM_READ or VM_READWRITE (otherwise ENOTSUP).
   Success need not clear errno. Stale nonnull handles/pointers are invalid;
   address reuse is not guarded by generation tokens. */

__END_DECLS
#endif
