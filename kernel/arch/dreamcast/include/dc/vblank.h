/* KallistiOS ##version##

   dc/vblank.h
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/vblank.h
    \brief   VBlank handler registration.
    \ingroup system_vblank

    This file allows functions to be registered to be called on each vblank
    interrupt that occurs. This gives a way to schedule small functions that
    must occur regularly, without using threads.

    \author Megan Potter
*/

#ifndef __DC_VBLANK_H
#define __DC_VBLANK_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <dc/asic.h>
#include <stdint.h>

/** \defgroup system_vblank     VBlank
    \brief                      VBlank interrupt handler management
    \ingroup                    system

    @{
*/

/** \brief  Add a vblank handler.

    This function adds a handler to the vblank handler list. The function will
    be called at the start of every vblank period with the same parameters that
    were passed to the IRQ handler for vblanks.

    \param  hnd             The handler to add.
    \param  data            A user pointer that will be passed to the callback.

    \return                 The handle id on success, or <0 on failure.
*/
int vblank_handler_add(asic_evt_handler hnd, void *data);

/** \brief  Add a priority-ordered vblank handler.

    This function adds a handler to the vblank handler list. Handlers run in
    ascending priority order; lower numeric values execute first. Handlers
    with the same priority retain their registration order.

    The callback executes in interrupt context and must remain bounded. It may
    remove itself or another registered handler with vblank_handler_remove().
    Registration allocates memory and must not be attempted from interrupt
    context.

    \param  hnd             The handler to add.
    \param  data            A user pointer passed to the callback.
    \param  priority        Execution priority from 0 (first) to 255 (last).

    \return                 The handle id on success, or <0 on failure with
                            errno set to `EINVAL`, `ENOMEM`, `EOVERFLOW`, or
                            `EPERM`.

    \sa vblank_handler_add, vblank_handler_remove
*/
int vblank_handler_add_prio(asic_evt_handler hnd, void *data,
                            uint8_t priority);

/** \brief Default priority used by vblank_handler_add(). */
#define VBLANK_PRIORITY_DEFAULT UINT8_C(128)

/** \brief  Remove a vblank handler.

    This function removes the specified handler from the vblank handler list.

    \param  handle          The handle id to remove (returned by
                            vblank_handler_add() when the handler was added).

    \retval 0               On success.
    \retval -1              If the handle does not exist, with errno set to
                            `ENOENT`.

    This function is safe to call from interrupt context, including a vblank
    callback. Removal takes effect before the next callback in the same
    dispatch when that callback has not already started. Storage reclamation is
    deferred until a later thread-context VBlank API call or shutdown.
*/
int vblank_handler_remove(int handle);

/* \cond */
/** Initialize the vblank handler. This must be called after the asic module
    is initialized. */
int vblank_init(void);

/** Shut down the vblank handler. */
int vblank_shutdown(void);
/* \endcond */

/** @} */

__END_DECLS

#endif  /* __DC_VBLANK_H */
