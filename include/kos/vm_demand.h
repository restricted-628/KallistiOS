/* KallistiOS ##version## */
/** \file kos/vm_demand.h
    \brief Opt-in read-only paging from immutable direct ISO9660 media.

    Link the matching libvmdemand addon as described in its README. This is
    a closed application profile: the selected address space, source fd,
    carrier, polled SCIF debug path and resident kernel dependencies must
    remain live. No scheduler polling callbacks, media/backend changes,
    VFS shutdown or MMU context changes during an admitted use. Ordinary
    kernel threads only; not IRQs or child fiber stacks. Reads in an
    admitted range may block. Do not hold locks needed by recovery.
*/
#ifndef __KOS_VM_DEMAND_H
#define __KOS_VM_DEMAND_H
#include <kos/vm.h>
__BEGIN_DECLS
typedef struct vm_demand vm_file_demand_t;
/** Reserve count file pages starting at first, without reading a page.
    The first read faults and retries transparently after recovery. The
    output is unchanged on failure. The caller must have at least 8192
    bytes of available directly mapped stack, including at each fault.
    An unrecoverable admitted fault is fail-stop, not a recoverable signal.
    Each carrier can have at most one outstanding use. */
int vm_file_demand_begin(vm_file_t *file, size_t first, size_t count,
                         vm_file_demand_t **out);
/** End the quiescent use on the admitting thread. Pages can then be evicted
    unless retained by another owner. Never use a handle after success. */
int vm_file_demand_end(vm_file_demand_t *use);
__END_DECLS
#endif
