/* KallistiOS ##version## */
#ifndef __ARCH_SHUTDOWN_INTERNAL_H
#define __ARCH_SHUTDOWN_INTERNAL_H

#include <stdbool.h>

/* Full exit owns the allocator transition until the image is gone. */
void arch_shutdown_prepare(void);
bool arch_shutdown_owns_wave_gate(void);
int arch_shutdown_require_main_thread(void);
void arch_wave_shutdown_wait(int error) __attribute__((noreturn));

/* Early stock boot already excludes runtime admission. */
void hardware_periph_init_boot(void);
/* Requires platform ownership and retired SPU request and callback workers. */
void hardware_shutdown_after_spu_retired(void);
int g2_dma_shutdown_checked(void);

#endif
