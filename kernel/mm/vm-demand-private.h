/* Private opt-in DP1 experiment. Not installed or part of kos/vm.h. */
#ifndef KOS_VM_DEMAND_PRIVATE_H
#define KOS_VM_DEMAND_PRIVATE_H
#include <kos/vm.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include <kos/genwait.h>
#ifndef KOS_VM_DP1_HOST_TEST
_Static_assert(sizeof(irq_context_t) == 256, "private return bridge copies 64 words");
#endif
#define VM_DP1_USERS 8u
typedef struct vm_demand vm_demand_t;
/* Only the fixed public wrapper or an internal fixture supplies this profile. The complete program
   owns callback registration, debug backend, selected MMU context and all
   borrowed stacks/carriers until every use ends. This is NOT enforcement of
   arbitrary application's global callbacks.
   poll_bytes/debug_bytes are profile-ledger inputs, not measured/enforced
   callback bounds; headroom is supplied by the closed program's proof.
   Admission and each use stay on the same carrier and registered child (or
   validated carrier stack). The owner retains stack, TLS/errno, provider and
   selected context, and prevents foreign destruction until end succeeds.
   Keep the private fd, immutable source and all recovery dependencies resident;
   no global VFS shutdown, source replacement or sibling-dependent completion.
   The target gate verifies a retained direct ISO descriptor. */
typedef struct vm_demand_profile {
    size_t headroom, poll_bytes, debug_bytes;
    bool closed_test;
} vm_demand_profile_t;
typedef enum { VM_DP1_IDLE, VM_DP1_SAVED, VM_DP1_WAITING,
               VM_DP1_LOADING, VM_DP1_RETRY, VM_DP1_FAILED } vm_demand_state_t;
int vm_demand_begin(vm_file_t *, size_t, size_t,
                    const vm_demand_profile_t *, vm_demand_t **);
int vm_demand_end(vm_demand_t *);
/* Explicit call only after normal refill/VM2 handling. No default hook added. */
bool vm_demand_defer(mmucontext_t *, irq_t, irq_context_t *, uint32_t,
                     uint32_t, uint32_t);
int vm_demand_prepare(vm_demand_t *);
/* Caller excludes IRQs and immediately uses the private return bridge on true.
   On false it must allow scheduling and prepare again before terminal entry. */
bool vm_demand_retry_locked(vm_demand_t *, irq_context_t **, irq_context_t **);
bool vm_demand_terminal_locked(vm_demand_t *);
void vm_demand_recover(vm_demand_t *) __attribute__((noreturn));
void vm_demand_return(irq_context_t *, irq_context_t *) __attribute__((noreturn));
void vm_demand_stop(void) __attribute__((noreturn));
#endif
