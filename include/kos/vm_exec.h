/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
/** \file kos/vm_exec.h
    \brief Explicit resident executable regions owned by a VM space.

    These are trusted-code primitives, not an ELF loader or an isolation API.
    The caller owns image validation, source close, generation/use references,
    notifications and quiescence. All arguments, sources, stacks and controls
    remain resident/directly mapped and disjoint from managed backing.

    Ordinary threads with IRQs enabled only; the exclusions in kos/vm.h apply.
    The application keeps the selected context/ASID/refill mode stable across
    every live translated use, including suspended returns and escaped pointers.
    Context checks are snapshots, not leases. No forced teardown or foreign
    error wakes. Handle operations/destruction require external serialization
    and joining; consumed handle aliases are invalid.

    See doc/resident-vm.md for phases, accounting and executable publication.
*/
#ifndef KOS_VM_EXEC_H
#define KOS_VM_EXEC_H
#include <kos/vm.h>
#include <stddef.h>
#include <stdint.h>
__BEGIN_DECLS

typedef struct vm_exec_region vm_exec_region_t;
typedef struct vm_exec_load vm_exec_load_t;
typedef enum {
    VM_EXEC_EMPTY, VM_EXEC_READY, VM_EXEC_PREPARED,
    VM_EXEC_WRITING, VM_EXEC_FAILED, VM_EXEC_CLEANUP
} vm_exec_phase_t;
#define VM_EXEC_PAGE_ABSENT 0u
#define VM_EXEC_PAGE_READ 4u
#define VM_EXEC_PAGE_RX 5u
#define VM_EXEC_PAGE_RW 6u

typedef struct {
    void *base;
    size_t bytes;
    vm_exec_phase_t phase;
    int published;           /**< Old image may remain mapped in PREPARED. */
    size_t backing_bytes;    /**< Includes failure-held backing. */
    size_t metadata_bytes;   /**< Region, reservation, load and copied plan. */
    size_t table_budget_bytes; /**< Conservative shared-table envelope. */
} vm_exec_info_t;

/** All return 0 or -1/errno. Output objects are unchanged on failure.
    Pointer-to-pointer operations consume and NULL only on success. */
int vm_exec_reserve(vm_space_t *space, void *base, size_t bytes,
                    vm_exec_region_t **out);
int vm_exec_query(vm_exec_region_t *region, vm_exec_info_t *out);
int vm_exec_check_context(vm_exec_region_t *region);
/** Copy one intent byte per page, including at least one RX page. */
int vm_exec_prepare(vm_exec_region_t *region, const uint8_t *page_plan,
                    size_t plan_pages, vm_exec_load_t **out);
/** A valid permitted PREPARED attempt is irreversible even on context failure.
    The caller's unloaded notification must immediately precede this attempt. */
int vm_exec_begin(vm_exec_load_t *load);
/** Synchronous source consumption, also on error; no source pointer escapes.
    A WRITING range/source error poisons the load until abort. */
int vm_exec_write(vm_exec_load_t *load, size_t offset,
                  const void *source, size_t bytes);
int vm_exec_zero(vm_exec_load_t *load, size_t offset, size_t bytes);
/** Requires the loader's completed digest/close proof. No I/O is done here. */
int vm_exec_publish(vm_exec_load_t **load);
/** PREPARED abort preserves the old image. After begin, it retires everything.
    CLEANUP failure retains the load and backing for a later abort retry. */
int vm_exec_abort(vm_exec_load_t **load);
/** No live load or raw users; may retry CLEANUP. Inactive contexts supported. */
int vm_exec_retire(vm_exec_region_t *region);
/** EMPTY only; join all callers/waiters before consuming the region. */
int vm_exec_destroy(vm_exec_region_t **region);
__END_DECLS
#endif
