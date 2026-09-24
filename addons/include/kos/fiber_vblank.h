/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
/** \file kos/fiber_vblank.h
    \brief Bounded, ordered VBlank work on a caller-scheduled child fiber.

    Optional adapter, link with -lfiber_vblank. Works with either mutually
    exclusive fiber provider. It contains no math backend or context-switch
    code, creates no thread/fiber, and does not depend on the Service Executor.

    Configure handlers before starting. A single consumer child fiber calls
    dispatch() after a durable IRQ wake signal (a manual-reset fiber event or
    fiber_service_wake()). Callbacks run lower-priority-number first, newest
    first on ties, and may yield: later callbacks wait for the earlier callback
    to return. This is deferred work, NOT a VBlank interrupt deadline guarantee.

    One pending batch coalesces missed frames. Each callback receives the same
    elapsed-frame count, saturated at UINT32_MAX. There is no per-frame queue
    or allocation. Applications decide how to handle skipped frames.

    Lifecycle operations must be externally serialized. stop() unregisters the
    IRQ producer and discards pending frames; an already-selected callback may finish,
    but remaining callbacks are skipped. Then join/finish the consumer before
    destroy(). Keep callback data, wake targets, and borrowed fiber stacks alive
    through that join. Never forcibly destroy a fiber suspended in dispatch().
    VBlank itself must remain initialized until stop() completes.
*/
#ifndef __KOS_FIBER_VBLANK_H
#define __KOS_FIBER_VBLANK_H
#include <kos/cdefs.h>
#include <stddef.h>
#include <stdint.h>
__BEGIN_DECLS

typedef struct fiber_vblank fiber_vblank_t;
typedef void (*fiber_vblank_callback_t)(uint32_t frames, void *data);
/** IRQ-safe, bounded notification only: no switching, allocation or blocking.
    Called only when pending changes from zero to nonzero. The consumer must
    clear its durable wake hint BEFORE dispatch(), not afterwards. */
typedef void (*fiber_vblank_wake_t)(void *data);

/** Allocate fixed handler capacity. Thread context; no fiber attachment needed. */
fiber_vblank_t *fiber_vblank_create(size_t capacity);
/** Register before first start; full capacity returns ENOSPC, sealed returns
    EBUSY. No removal/mutation during dispatch; stop and replace the dispatcher
    to reconfigure it. */
int fiber_vblank_add(fiber_vblank_t *dispatcher, uint8_t priority,
                     fiber_vblank_callback_t callback, void *data);
/** Register the notification IRQ at VBLANK_PRIORITY_DEFAULT. One-shot lifecycle:
    a successful start seals configuration, including after stop (EALREADY). */
int fiber_vblank_start(fiber_vblank_t *dispatcher, fiber_vblank_wake_t wake,
                       void *data);
/** Run at most one batch on a child fiber with interrupts enabled. Returns 1
    for a batch, 0 if idle/stopped, -1 on error. Overlapping/reentrant dispatch
    returns EBUSY, main fiber/IRQ use returns EPERM. */
int fiber_vblank_dispatch(fiber_vblank_t *dispatcher);
/** Thread context, idempotent. Does not join or wake a parked consumer; the
    caller must signal its event or request Service Executor shutdown. */
int fiber_vblank_stop(fiber_vblank_t *dispatcher);
/** Thread context. EBUSY until stopped and no dispatch is in progress. The
    caller must also ensure nobody can enter any API after destruction. */
int fiber_vblank_destroy(fiber_vblank_t *dispatcher);

__END_DECLS
#endif
