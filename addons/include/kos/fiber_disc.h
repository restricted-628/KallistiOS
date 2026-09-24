/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef __KOS_FIBER_DISC_H
#define __KOS_FIBER_DISC_H

#include <dc/gdrom_direct.h>
#include <kos/cdefs.h>
__BEGIN_DECLS

/** Optional application-fiber adapter for direct GD-DMA reads.
 * Link -lfiber_disc. Uses public fiber/event and disc-request APIs only.
 * It creates no thread and does not use the Fiber Service Executor.
 *
 * All calls belong to the creating thread and attached fiber runtime. The
 * main fiber must pump completions and dispatch ready application fibers.
 * Do not detach, destroy a waiting fiber, or release DMA buffers while an
 * operation is outstanding. Submission/cancellation may take ordinary short
 * kernel locks and allocate; they are not hard-real-time operations.
 */
typedef struct fiber_disc fiber_disc_t;
typedef struct fiber_disc_read fiber_disc_read_t;

/** Create after fiber_attach(). capacity bounds live read handles, including
 * completed handles not yet released. No payload buffers are allocated.
 */
fiber_disc_t *fiber_disc_create(size_t capacity);

/** Submit direct DMA with a nonzero execution timeout. Uses the existing
 * direct driver's format, alignment and count rules; never falls back to
 * BIOS. The timeout does not bound initial queue residence. EAGAIN means the
 * adapter is at capacity; ECANCELED means shutdown has started.
 */
fiber_disc_read_t *fiber_disc_read_dma(fiber_disc_t *disc, void *buffer,
    uint32_t fad, size_t sectors, gdrom_direct_sector_type_t type,
    uint32_t timeout);

/** Completion pump, called by the owner main fiber. Never waits for device
 * or callback completion; retirement may use ordinary allocator locks.
 * Returns the number of reads still awaiting safe retirement, or -1.
 * A child is made ready only after the request and its callback are finished
 * and the underlying request has been destroyed. No fiber is switched here.
 */
int fiber_disc_pump(fiber_disc_t *disc);

/** Main-fiber-only bounded idle wait, after dispatching all ready fibers.
 * Pump again after return. A completion-before-wait notification is retained.
 * An early callback notification may require a 1 ms retry to let that callback
 * return; no kernel callback-wait is performed on a child fiber.
 * timeout must be nonzero. ETIMEDOUT is an idle timeout, not read cancellation.
 */
int fiber_disc_idle(fiber_disc_t *disc, uint32_t timeout);

/** Park only the calling child fiber. Returns 0 once safely retired, even
 * for a failed/cancelled read: inspect status.state/result/error. One waiter
 * per read. The optional status is copied before return, never used by a
 * callback. No per-wait deadline; cancel explicitly from the owner scheduler.
 */
int fiber_disc_await(fiber_disc_read_t *read, cdrom_request_status_t *status);

/** Request cancellation, not immediate buffer release. Continue pumping and
 * dispatching until await completes before releasing the read/buffer.
 */
int fiber_disc_cancel(fiber_disc_read_t *read);

/** Reject new work and request cancellation of every outstanding read.
 * Does not wait for completion, join, dispatch or free handles. Cancellation
 * may take the request-queue lock. Keep pumping/dispatching.
 */
int fiber_disc_shutdown(fiber_disc_t *disc);

/** Release a retired read with no active awaiter; otherwise EBUSY. */
int fiber_disc_read_destroy(fiber_disc_read_t *read);

/** Destroy an empty adapter (all read handles released), otherwise EBUSY. */
int fiber_disc_destroy(fiber_disc_t *disc);

__END_DECLS
#endif
