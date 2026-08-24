/* KallistiOS ##version##

   include/kos/fiber.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    kos/fiber.h
    \brief   Cooperative execution contexts within a KOS thread.
    \ingroup fibers

    Fibers provide caller-directed execution on caller-owned stacks. They are
    not kernel threads and are never scheduled independently by KOS.

    Every fiber inherits its owner thread's address space, MMU context, GBR and
    compiler TLS, KOS TLS, newlib state and errno, working directory, priority,
    and kernel wait identity. A fiber switch changes only the CPU continuation
    and active stack. Consequently, blocking in a fiber blocks its whole owner
    thread while allowing other KOS threads to run.

    A fiber stack must remain mapped, writable, and alive in the owner thread's
    address space until the fiber is destroyed. This API does not create MMU
    mappings or guard pages and never changes an MMU context or ASID.
*/

#ifndef __KOS_FIBER_H
#define __KOS_FIBER_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>

/** \defgroup fibers Cooperative Fibers
    \brief Cooperative execution contexts owned by KOS threads
    \ingroup threading

    @{ */

/** \brief Minimum supported fiber stack size. */
#define KOS_FIBER_STACK_MIN 1024u

/** \brief Opaque cooperative fiber handle. */
typedef struct kfiber kfiber_t;

/** \brief Fiber entry-point function. */
typedef void (*kfiber_entry_t)(void *data);

/** \brief Observable fiber state. */
typedef enum kfiber_state {
    KFIBER_STATE_INVALID  = 0, /**< \brief Invalid fiber or failed query. */
    KFIBER_STATE_READY    = 1, /**< \brief Ready for an explicit switch. */
    KFIBER_STATE_RUNNING  = 2, /**< \brief Current on its owner thread. */
    KFIBER_STATE_FINISHED = 3, /**< \brief Entry point has returned. */
    KFIBER_STATE_WAITING  = 4  /**< \brief Parked on cooperative state. */
} kfiber_state_t;

/** \brief Optional execution-context state preserved by a fiber runtime. */
typedef enum kfiber_attach_flags {
    /** Preserve only the normal C call-boundary continuation. */
    KFIBER_ATTACH_DEFAULT = 0,

    /** Preserve the architecture math-accelerator context.

        On Dreamcast this saves and restores XMTRX for every cooperative
        transfer. This is appropriate for fibers that retain SH4ZAM or KOS
        matrix state across a yield. It adds one 64-byte, 32-byte-aligned
        allocation per fiber and the corresponding matrix store/load work to
        each switch. It does not create a thread or affect unattached threads.
    */
    KFIBER_ATTACH_MATH_CONTEXT = 1u << 0
} kfiber_attach_flags_t;

/** \brief Callback invoked immediately before a fiber transfer.

    The callback runs on the outgoing fiber with interrupts masked. It must be
    bounded, must not block, and must not create, destroy, or switch fibers.
    The owner thread and outgoing fiber are still current during the callback.
*/
typedef void (*kfiber_switch_cb_t)(kfiber_t *from, kfiber_t *to, void *data);

/** \brief Attach cooperative-fiber state to the calling KOS thread.

    The currently executing continuation becomes the thread's main fiber.
    Repeated calls by the same thread return the same main fiber.

    \return The calling thread's main fiber, or `NULL` on error.
*/
kfiber_t *fiber_attach(void);

/** \brief Attach configurable cooperative-fiber state to the calling thread.

    The flags apply to the complete fiber runtime owned by this thread. A later
    call may request the same flags or a subset, but cannot upgrade an existing
    runtime after attachment. Call this instead of fiber_attach() before
    creating fibers when XMTRX must survive cooperative transfers.

    \param flags Bitwise combination of kfiber_attach_flags_t values.
    \return The calling thread's main fiber, or `NULL` on error.
*/
kfiber_t *fiber_attach_ex(unsigned int flags);

/** \brief Return the calling thread's fiber-attachment flags.

    \return Attachment flags, or zero on error with `errno` set.
*/
unsigned int fiber_get_attach_flags(void);

/** \brief Create a fiber owned by the calling thread.

    The calling thread must first call fiber_attach(). The stack is borrowed,
    not owned: it must be aligned to `THD_STACK_ALIGNMENT`, its size must be a
    multiple of that alignment, and it must not overlap another fiber stack.

    \param stack       Base of a caller-owned, mapped, writable stack.
    \param stack_size  Stack size in bytes.
    \param entry       Function run the first time the fiber is selected.
    \param data        Caller data supplied to entry.
    \return A new ready fiber, or `NULL` on error.
*/
kfiber_t *fiber_create(void *stack, size_t stack_size,
                       kfiber_entry_t entry, void *data);

/** \brief Destroy a non-running fiber.

    The main fiber cannot be destroyed. This function never frees the borrowed
    stack.

    \retval 0  Success.
    \retval -1 Error, with `errno` set.
*/
int fiber_destroy(kfiber_t *fiber);

/** \brief Cooperatively transfer to another fiber on the calling thread.

    This call returns when another fiber later transfers back. It may not be
    called from interrupt context. Interrupt handlers should signal or wake the
    owner thread and let that thread perform the transfer.

    A transfer is also rejected while interrupts are masked or while the
    calling thread owns a thread-scoped hardware transaction which has
    inhibited cooperative switching, such as a locked store queue mapping.

    KOS mutexes are owned by threads rather than fibers. The caller must not
    transfer while holding an ordinary or recursive KOS mutex; sibling fibers
    would appear to be the same mutex owner.

    \retval 0  The calling fiber was resumed successfully.
    \retval -1 Error, with `errno` set. `EBUSY` indicates an unsafe switch
               point or a target which is not ready.
*/
int fiber_switch(kfiber_t *fiber);

/** \brief Return the calling thread's current fiber, or `NULL` if unattached. */
kfiber_t *fiber_current(void);

/** \brief Return the calling thread's main fiber, or `NULL` if unattached. */
kfiber_t *fiber_main(void);

/** \brief Return a fiber's caller data.

    This operation is restricted to the fiber's owner thread.
*/
void *fiber_get_data(const kfiber_t *fiber);

/** \brief Return a fiber's current state.

    Returns KFIBER_STATE_INVALID and sets `errno` when the handle is invalid or
    belongs to another thread.
*/
kfiber_state_t fiber_get_state(const kfiber_t *fiber);

/** \brief Set the calling thread's optional pre-switch callback. */
int fiber_set_switch_callback(kfiber_switch_cb_t callback, void *data);

/** @} */

__END_DECLS
#endif /* __KOS_FIBER_H */
