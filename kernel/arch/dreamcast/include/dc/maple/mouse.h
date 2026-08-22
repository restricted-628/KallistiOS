/* KallistiOS ##version##

   dc/maple/mouse.h
   Copyright (C) 2000-2002 Jordan DeLong and Megan Potter
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/maple/mouse.h
    \brief   Definitions for using the mouse device.
    \ingroup mouse

    This file contains the definitions needed to access the Maple mouse type
    device.

    \author Jordan DeLong
    \author Megan Potter
*/

#ifndef __DC_MAPLE_MOUSE_H
#define __DC_MAPLE_MOUSE_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <kos/regfield.h>

/** \defgroup   mouse   Mouse
    \brief              Driver for the Dreamcast's Mouse Input Device
    \ingroup            peripherals
*/

/** \defgroup   mouse_buttons   Buttons
    \brief                      Masks for the buttons on a mouse
    \ingroup                    mouse

    These are the possible buttons to press on a maple bus mouse.

    @{
*/
#define MOUSE_MIDDLEBUTTON  BIT(0)  /**< \brief Middle mouse button */
#define MOUSE_RIGHTBUTTON   BIT(1)  /**< \brief Right mouse button */
#define MOUSE_LEFTBUTTON    BIT(2)  /**< \brief Left mouse button */
#define MOUSE_SIDEBUTTON    BIT(3)  /**< \brief Side mouse button */
#define MOUSE_UPBUTTON      BIT(4)  /**< \brief Up auxiliary button */
#define MOUSE_DOWNBUTTON    BIT(5)  /**< \brief Down auxiliary button */
#define MOUSE_LEFTAUXBUTTON BIT(6)  /**< \brief Left auxiliary button */
#define MOUSE_RIGHTAUXBUTTON BIT(7) /**< \brief Right auxiliary button */
/** @} */

/** \defgroup   mouse_axes   Axes
    \brief                   Masks for axes advertised by a mouse
    \ingroup                 mouse

    @{
*/
#define MOUSE_AXIS_X        BIT(0)  /**< \brief X axis */
#define MOUSE_AXIS_Y        BIT(1)  /**< \brief Y axis */
#define MOUSE_AXIS_Z        BIT(2)  /**< \brief Z axis or wheel */
#define MOUSE_AXIS_3        BIT(3)  /**< \brief Additional axis 3 */
#define MOUSE_AXIS_4        BIT(4)  /**< \brief Additional axis 4 */
#define MOUSE_AXIS_5        BIT(5)  /**< \brief Additional axis 5 */
#define MOUSE_AXIS_6        BIT(6)  /**< \brief Additional axis 6 */
#define MOUSE_AXIS_7        BIT(7)  /**< \brief Additional axis 7 */
/** @} */

/* More civilized mouse structure. There are several significant
   differences in data interpretation between the "cooked" and
   the old "raw" structs:

   - buttons are zero-based: a 1-bit means the button is PRESSED
   - no dummy values

   Note that this is what maple_dev_status() will return.
 */

/** \brief   Mouse status structure.
    \ingroup mouse

    This structure contains information about the status of the mouse device,
    and can be fetched with maple_dev_status().

    \headerfile dc/maple/mouse.h
*/
typedef struct {
    /** \brief  Buttons pressed bitmask.
        \see    mouse_buttons
    */
    uint32_t  buttons;

    /** \brief  X movement value */
    int dx;

    /** \brief  Y movement value */
    int dy;

    /** \brief  Z movement value */
    int dz;

    /** \brief  Additional movement axes 3 through 7.

        Check the corresponding MOUSE_AXIS_* bits from mouse_get_info() before
        consuming these values. The first three axes remain available through
        dx, dy, and dz for source and binary compatibility.
    */
    int axis[5];

    /** \brief  Per-axis overflow flags using MOUSE_AXIS_* masks. */
    uint8_t overflow;

    /** \brief  Device-defined option byte from the current condition. */
    uint8_t options;

    uint8_t reserved[2];   /**< \brief Reserved for future use. */
} mouse_state_t;

/** \brief Decoded hardware metadata from the mouse function descriptor.
    \ingroup mouse
*/
typedef struct mouse_info {
    uint8_t reserved;          /**< \brief Protocol-reserved descriptor byte. */
    uint8_t buttons;           /**< \brief Supported MOUSE_*BUTTON mask. */
    uint8_t axes;              /**< \brief Supported MOUSE_AXIS_* mask. */
    uint8_t reserved2;         /**< \brief Second protocol-reserved byte. */
} mouse_info_t;

/** \brief Coherent snapshot of one completed mouse sample.
    \ingroup mouse

    The state, transition masks, and sequence are copied atomically with
    respect to Maple response handling. This avoids mixing movement or button
    fields from different samples while an interrupt updates the driver-owned
    status object.
*/
typedef struct mouse_snapshot {
    mouse_state_t state;       /**< \brief Current decoded mouse condition. */
    uint32_t pressed;          /**< \brief Buttons newly pressed this sample. */
    uint32_t released;         /**< \brief Buttons newly released this sample. */
    uint32_t sequence;         /**< \brief Successful-sample counter. */
} mouse_snapshot_t;

struct maple_device;

/** \brief Copy the latest mouse sample atomically.
    \ingroup mouse

    \param  device          Connected mouse device.
    \param  snapshot        Receives the coherent caller-owned snapshot.
    \retval 0               A completed sample was copied.
    \retval -1              Invalid arguments, disconnected device, or no
                            completed sample; errno is EINVAL, ENODEV, or
                            EAGAIN respectively.
*/
int mouse_get_snapshot(const struct maple_device *device,
                       mouse_snapshot_t *snapshot);

/** \brief Copy decoded mouse hardware metadata.
    \ingroup mouse

    \param  device          Connected mouse device.
    \param  info            Receives the descriptor metadata.
    \retval 0               Metadata was copied.
    \retval -1              Invalid arguments or disconnected/non-mouse
                            device; errno is EINVAL or ENODEV.
*/
int mouse_get_info(const struct maple_device *device, mouse_info_t *info);

/* \cond */
/* Init / Shutdown */
void mouse_init(void);
void mouse_shutdown(void);
/* \endcond */

__END_DECLS

#endif  /* __DC_MAPLE_MOUSE_H */
