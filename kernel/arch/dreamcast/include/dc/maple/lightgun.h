/* KallistiOS ##version##

   dc/maple/lightgun.h
   Copyright (C) 2015 Lawrence Sebald
   Copyright (C) 2026 KallistiOS Contributors
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/maple/lightgun.h
    \brief   Dreamcast light-gun capture API.
    \ingroup peripherals_lightgun

    The Dreamcast light gun reports its buttons through the controller Maple
    function. Its aim is measured separately: a special Maple descriptor gives
    one physical port exclusive ownership of the bus while the PVR scans the
    following video field. This API joins those two mechanisms and schedules
    captures automatically on trigger edges.

    \author Lawrence Sebald
*/

#ifndef __DC_MAPLE_LIGHTGUN_H
#define __DC_MAPLE_LIGHTGUN_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>

struct maple_device;
struct cont_snapshot;

/** \defgroup peripherals_lightgun  Lightgun
    \brief                          Maple light-gun input and aim capture
    \ingroup                        peripherals

    @{
*/

/** \brief Port-mask values accepted by lightgun_set_enabled_ports(). */
enum lightgun_port_mask {
    LIGHTGUN_PORT_A   = 0x01,
    LIGHTGUN_PORT_B   = 0x02,
    LIGHTGUN_PORT_C   = 0x04,
    LIGHTGUN_PORT_D   = 0x08,
    LIGHTGUN_PORT_ALL = 0x0f
};

/** \brief Default automatic capture-flash color in RGB888. */
#define LIGHTGUN_FLASH_COLOR_DEFAULT 0x00c0c0c0u

/** \brief Coherent result from one completed light-gun capture. */
typedef struct lightgun_snapshot {
    int port;               /**< \brief Physical Maple port, from 0 through 3. */
    int x;                  /**< \brief Raw PVR horizontal counter value. */
    int y;                  /**< \brief Raw PVR vertical counter value. */
    uint32_t sequence;      /**< \brief Completed-capture counter. */
} lightgun_snapshot_t;

/** \brief Immediate light-gun capture callback.

    The callback runs in the Maple DMA interrupt after the coherent result has
    been published. The snapshot pointer is valid only for the duration of the
    callback.

    \warning Interrupt-context restrictions apply: do not block, allocate,
             invoke VFS operations, or call APIs that require later interrupts.
*/
typedef void (*lightgun_capture_handler_t)(const lightgun_snapshot_t *snapshot,
                                           void *user_data);

/** \brief Per-sample policy callback for physical light-gun triggers.

    This callback runs in the Maple DMA interrupt for every successful
    controller sample from an enabled light gun. Returning nonzero permits the
    normal newly-pressed trigger edge to request a capture; returning zero
    suppresses it. The callback may call lightgun_request_capture() to produce
    software-controlled shots or rapid fire on any enabled gun.

    \warning The same interrupt-context restrictions as
             lightgun_capture_handler_t apply.
*/
typedef int (*lightgun_trigger_filter_t)(struct maple_device *dev,
                                         const struct cont_snapshot *snapshot,
                                         void *user_data);

/** \brief Enable automatic trigger capture on selected physical ports.

    A newly pressed A/trigger button on a connected controller/light-gun device
    requests a capture. If several ports request one before service, the lowest
    numbered port wins and the others remain pending for later fields. A zero
    mask disables automatic capture. Captures already active complete normally.

    \param  port_mask       OR of \ref lightgun_port_mask values.
    \retval 0               The mask was installed.
    \retval -1              Unsupported mask bits, with errno set to EINVAL.
*/
int lightgun_set_enabled_ports(uint8_t port_mask);

/** \brief Return the automatic-capture port mask. */
uint8_t lightgun_get_enabled_ports(void);

/** \brief Request one light-gun capture without a physical trigger edge.

    Repeated requests for the same port coalesce while pending. The port must
    be enabled and contain a valid device advertising both controller and
    light-gun Maple functions.

    \param  port            Physical Maple port, from 0 through 3.
    \retval 0               The request was queued.
    \retval -1              Failure, with errno set to EINVAL or ENODEV.
*/
int lightgun_request_capture(int port);

/** \brief Copy the latest completed light-gun capture atomically.

    Coordinates are raw PVR counter values and require game/video-mode-specific
    calibration. The hardware does not provide a portable normalized screen
    coordinate through Maple.

    \param  snapshot        Receives a caller-owned coherent snapshot.
    \retval 0               A completed capture was copied.
    \retval -1              Invalid pointer or no completed capture, with errno
                            set to EINVAL or EAGAIN.
*/
int lightgun_get_snapshot(lightgun_snapshot_t *snapshot);

/** \brief Register an immediate completed-capture handler.

    There is one process-wide handler. Passing NULL removes it. Registration is
    atomic with respect to Maple response interrupts.
*/
void lightgun_set_capture_handler(lightgun_capture_handler_t callback,
                                  void *user_data);

/** \brief Register a per-sample physical-trigger policy callback.

    There is one process-wide callback. Passing NULL restores the default policy
    in which each newly pressed trigger edge requests one capture.
*/
void lightgun_set_trigger_filter(lightgun_trigger_filter_t callback,
                                 void *user_data);

/** \brief Set the RGB888 color flashed for automatic and requested captures.

    The default is \ref LIGHTGUN_FLASH_COLOR_DEFAULT. A value of zero disables
    the automatic flash. The original border color and video-configuration bit
    are restored after the capture field.
*/
void lightgun_set_flash_color(uint32_t rgb888);

/** \brief Return the configured RGB888 capture-flash color. */
uint32_t lightgun_get_flash_color(void);

/* \cond */
/* Internal hooks joining the controller and Maple DMA layers. */
void lightgun_controller_sample(struct maple_device *dev,
                                const struct cont_snapshot *snapshot);
void lightgun_capture_complete(int port, int x, int y);

/* Init / Shutdown */
void lightgun_init(void);
void lightgun_shutdown(void);
/* \endcond */

/** @} */

__END_DECLS

#endif  /* __DC_MAPLE_LIGHTGUN_H */
