/* KallistiOS ##version##

   dcload_syscalls.c

   Copyright (C) 2026 Andy Barajas

*/

#include <dc/dcload.h>

#include <dc/fifo.h>
#include <dc/memory.h>

#include <kos/irq.h>
#include <errno.h>
#ifndef __NAOMI__
#include "gaps_internal.h"
#endif

/* This is the address where the function pointer for the dcload syscall is fetched from */
#define VEC_DCLOAD        (MEM_AREA_P1_BASE | 0x0C004008)

/*
    This is the single syscall dcload provides. It is then multiplexed out based on the `cmd`
    parameter.
*/

int dcload_syscall_native(dcload_cmd_t cmd, void *param1, void *param2, void *param3) {
    /* Disable IRQs until the syscall returns */
    irq_disable_scoped();

    /* A resident IP loader bypasses KOS's BBA driver and SRAM leases. Once a
       KOS owner takes over, never re-enter its network I/O behind that owner's
       back (including after socket-backend shutdown restores this transport).
       These two metadata calls are retained for KOS's normal startup handoff. */
#ifndef __NAOMI__
    if(cmd != DCLOAD_ASSIGNWRKMEM && cmd != DCLOAD_GETHOSTINFO
            && !gaps_native_loader_allowed()) {
        errno = EBUSY;
        return -1;
    }
#endif

    uintptr_t *syscall_ptr = (uintptr_t *)VEC_DCLOAD;
    int (*syscall)(uintptr_t, uintptr_t, uintptr_t, uintptr_t) =
        (int (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))(*syscall_ptr);

    /* Ensure that the FIFO buffer is clear */
    /* XXX - Is this needed? It seems like something only for serial. */
    while(FIFO_STATUS & FIFO_SH4)            ;

    /* Make the call */
    return syscall((uintptr_t)cmd, (uintptr_t)param1,
                   (uintptr_t)param2, (uintptr_t)param3);
}
