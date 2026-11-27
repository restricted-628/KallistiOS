/* KallistiOS ##version##

   newlib_exit.c
   Copyright (C)2004 Megan Potter

*/

#include <arch/arch.h>
#include <kos/init_base.h>

#ifdef __DREAMCAST__
#include <reent.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../arch/dreamcast/kernel/shutdown_internal.h"
#endif

extern void arch_exit_handler(int ret_code) __noreturn;

static int ret_code;

void kos_shutdown(void) {
    arch_exit_handler(ret_code);

    __builtin_unreachable();
}

KOS_INIT_FLAG_WEAK(kos_shutdown, true);

#ifdef __DREAMCAST__
/* Newlib 4.6's callback order, with the platform barrier before atexit. */
#ifdef _LITE_EXIT
extern void __call_exitprocs(int code, void *dso) __weak_symbol;
#else
extern void __call_exitprocs(int code, void *dso);
#endif

void exit(int code) {
    if(kos_shutdown_weak)
        arch_shutdown_prepare();

#ifdef _LITE_EXIT
    if(__call_exitprocs)
#endif
        __call_exitprocs(code, NULL);

    if(__stdio_exit_handler)
        __stdio_exit_handler();

    _exit(code);
}
#endif

__used void _exit(int code) {
    ret_code = code;

    KOS_INIT_FLAG_CALL(kos_shutdown);

    arch_panic("exit() called with INIT_NO_SHUTDOWN flag");
}
