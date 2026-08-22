/* KallistiOS ##version##

   kernel/thread/fiber_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __KERNEL_THREAD_FIBER_INTERNAL_H
#define __KERNEL_THREAD_FIBER_INTERNAL_H

#include <kos/fiber.h>
#include <kos/irq.h>

#include <stdbool.h>

typedef void (*kfiber_wait_cancel_t)(void *data);
typedef void (*kfiber_wait_observer_t)(kfiber_t *fiber, bool waiting,
                                      void *data);

void *_fiber_runtime_cookie(void);
kfiber_t *_fiber_wait_current_locked(void *runtime_cookie,
                                     irq_mask_t saved_irq);
int _fiber_park_current_locked(void *wait_key, kfiber_wait_cancel_t cancel,
                               void *cancel_data, irq_mask_t saved_irq);
bool _fiber_wake_locked(kfiber_t *fiber, void *wait_key, int result);
int _fiber_cancel_wait(kfiber_t *fiber, int result);
int _fiber_set_wait_observer(kfiber_t *fiber,
                             kfiber_wait_observer_t observer, void *data);
void _fiber_sync_acquired(kfiber_t *fiber);
void _fiber_sync_released(kfiber_t *fiber);

#endif /* __KERNEL_THREAD_FIBER_INTERNAL_H */
