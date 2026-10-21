/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Private VM/MMU interface; not an exception callback API. */
#ifndef KOS_VM_INTERNAL_H
#define KOS_VM_INTERNAL_H
#include <kos/vm.h>
#include <kos/irq.h>

/* Reserved software bit only; never compiled into PTEL. One ticket per page. */
#define MMU_VM_PIN 1u

/* Ordinary-thread preparation. Caller serializes context mutation. */
int mmu_vm_prepare(mmucontext_t *c, unsigned vpn, unsigned physical,
                   page_prot_t protection, mmupage_t *prepared);
void mmu_vm_hide(mmucontext_t *c, unsigned vpn);
void mmu_vm_unpin(mmucontext_t *c, unsigned vpn);

/* Executable publication uses private preallocated tables. No allocation/free
   in these helpers; caller holds the space mutex. Returned empty tables are
   detached under IRQ exclusion and must be freed outside the manager lock. */
int mmu_vm_context_matches(mmucontext_t *c, unsigned asid);
int mmu_exec_map(mmucontext_t *c, unsigned vpn, unsigned physical,
                 page_prot_t protection, mmusubcontext_t **spare);
int mmu_exec_unmap(mmucontext_t *c, unsigned vpn, int physical, bool mapped);
mmusubcontext_t *mmu_exec_detach_empty(mmucontext_t *c, unsigned top);

/* Called only for a missing mapping in the default mapper, on the direct IRQ
   stack. Returns a prepared, now-published PTE or NULL without changing errno.
   The caller immediately LDTLBs it and returns through unchanged saved state. */
mmupage_t *vm_fault_resolve(mmucontext_t *c, irq_t event,
                          const irq_context_t *frame, uint32_t address,
                          uint32_t pteh, uint32_t mmucr, uintptr_t carrier);
#endif
