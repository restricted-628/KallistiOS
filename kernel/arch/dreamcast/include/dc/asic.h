/* KallistiOS ##version##

   dc/asic.h
   Copyright (C) 2001-2002 Megan Potter
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/asic.h
    \brief   Dreamcast ASIC event handling support.
    \ingroup asic

    This file provides definitions of the events that the ASIC (a part of the
    PVR) in the Dreamcast can trigger as IRQs, and ways to set responders for
    those events. Pretty much, this covers all IRQs that aren't generated
    internally in the SH4 (SCIF and the SH4 DMAC can generate their own IRQs,
    as well as the trapa instruction, and various exceptions -- those are not
    dealt with here).

    \author Megan Potter
    \author Joseph Black
*/

#ifndef __DC_ASIC_H
#define __DC_ASIC_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <stdbool.h>

/** \defgroup asic  Events
    \brief          Events pertaining to the DC's System ASIC
    \ingroup        system

*/

/* All event codes are two 8-bit integers; the top integer is the event code
   register to look in to check the event (and to acknowledge it). The
   register to check is 0xa05f6900+4*regnum. The bottom integer is the
   bit index within that register. */

/** \defgroup asic_events           Event Codes
    \brief                          Values for various Holly event codes
    \ingroup  asic
    @{
*/

/** \defgroup asic_events_pvr       PowerVR
    \brief                          Event code values for PowerVR events
    \ingroup  asic_events

    These are events that the PVR itself generates that can be hooked.
    @{
*/
#define ASIC_EVT_PVR_RENDERDONE_VIDEO     0x0000  /**< \brief Video render stage completed */
#define ASIC_EVT_PVR_RENDERDONE_ISP       0x0001  /**< \brief ISP render stage completed */
#define ASIC_EVT_PVR_RENDERDONE_TSP       0x0002  /**< \brief TSP render stage completed */
#define ASIC_EVT_PVR_VBLANK_BEGIN         0x0003  /**< \brief VBLANK begin interrupt */
#define ASIC_EVT_PVR_VBLANK_END           0x0004  /**< \brief VBLANK end interrupt */
#define ASIC_EVT_PVR_HBLANK_BEGIN         0x0005  /**< \brief HBLANK begin interrupt */

#define ASIC_EVT_PVR_YUV_DONE             0x0006  /**< \brief YUV completed */
#define ASIC_EVT_PVR_OPAQUEDONE           0x0007  /**< \brief Opaque list completed */
#define ASIC_EVT_PVR_OPAQUEMODDONE        0x0008  /**< \brief Opaque modifiers completed */
#define ASIC_EVT_PVR_TRANSDONE            0x0009  /**< \brief Transparent list completed */
#define ASIC_EVT_PVR_TRANSMODDONE         0x000a  /**< \brief Transparent modifiers completed */

#define ASIC_EVT_PVR_DMA                  0x0013  /**< \brief PVR DMA complete */
#define ASIC_EVT_PVR_PTDONE               0x0015  /**< \brief Punch-thrus completed */

#define ASIC_EVT_PVR_ISP_OUTOFMEM         0x0200  /**< \brief ISP out of memory */
#define ASIC_EVT_PVR_STRIP_HALT           0x0201  /**< \brief Halt due to strip buffer error */
#define ASIC_EVT_PVR_PARAM_OUTOFMEM       0x0202  /**< \brief Param out of memory */
#define ASIC_EVT_PVR_OPB_OUTOFMEM         0x0203  /**< \brief OPB went past PVR_TA_OPB_END */
#define ASIC_EVT_PVR_TA_INPUT_ERR         0x0204  /**< \brief Vertex input error */
#define ASIC_EVT_PVR_TA_INPUT_OVERFLOW    0x0205  /**< \brief Vertex input overflowed queue */
/** @} */

/** \defgroup asic_events_gd        GD-ROM Drive
    \brief                          Event code values for GD-ROM events
    \ingroup  asic_events

    These are events that the GD-ROM drive generates that can be hooked.
    @{
*/
#define ASIC_EVT_GD_COMMAND         0x0100  /**< \brief GD-Rom Command Status */
#define ASIC_EVT_GD_DMA             0x000e  /**< \brief GD-Rom DMA complete */
#define ASIC_EVT_GD_DMA_OVERRUN     0x020d  /**< \brief GD-Rom DMA overrun */
#define ASIC_EVT_GD_DMA_ILLADDR     0x020c  /**< \brief GD-Rom DMA illegal address */
#define ASIC_EVT_GD_DMA_ACCESS      0x020e  /**< \brief G1 access during GD-Rom DMA */
/** @} */

/** \defgroup asic_events_maple     Maple
    \brief                          Event code values for Maple events
    \ingroup  asic_events

    These are events that Maple generates that can be hooked.
    @{
*/
#define ASIC_EVT_MAPLE_DMA          0x000c  /**< \brief Maple DMA complete */
#define ASIC_EVT_MAPLE_ERROR        0x000d  /**< \brief Maple error (?) */
/** @} */

/** \defgroup asic_events_spu       AICA
    \brief                          Event code values for AICA events
    \ingroup  asic_events

    These are events that the SPU (AICA) generates that can be hooked.
    @{
*/
#define ASIC_EVT_SPU_DMA            0x000f  /**< \brief SPU (G2 channel 0) DMA complete */
#define ASIC_EVT_SPU_IRQ            0x0101  /**< \brief SPU interrupt */
/** @} */

/** \defgroup asic_events_g2dma     G2 Bus DMA
    \brief                          Event code values for G2 Bus events
    \ingroup  asic_events

    These are events that G2 bus DMA generates that can be hooked.
    @{
*/
#define ASIC_EVT_G2_DMA0            0x000f  /**< \brief G2 DMA channel 0 complete */
#define ASIC_EVT_G2_DMA1            0x0010  /**< \brief G2 DMA channel 1 complete */
#define ASIC_EVT_G2_DMA2            0x0011  /**< \brief G2 DMA channel 2 complete */
#define ASIC_EVT_G2_DMA3            0x0012  /**< \brief G2 DMA channel 3 complete */
/** @} */

/** \defgroup asic_events_ext      External Port
    \brief                          Event code values for external port events
    \ingroup  asic_events

    These are events that external devices generate that can be hooked.
    @{
*/
#define ASIC_EVT_EXP_8BIT           0x0102  /**< \brief Modem / Lan Adapter */
#define ASIC_EVT_EXP_PCI            0x0103  /**< \brief BBA IRQ */
/** @} */

/** @} */

/** \defgroup asic_regs             Registers
    \brief                          Addresses for various ASIC eveng registers
    \ingroup  asic

    These are the locations in memory where the ASIC registers sit.
    @{
*/
#define ASIC_ACK_A            0xa05f6900  /**< \brief IRQD ACK register */
#define ASIC_ACK_B            0xa05f6904  /**< \brief IRQB ACK register */
#define ASIC_ACK_C            0xa05f6908  /**< \brief IRQ9 ACK register */

#define ASIC_IRQD_A            0xa05f6910  /**< \brief IRQD first register */
#define ASIC_IRQD_B            0xa05f6914  /**< \brief IRQD second register */
#define ASIC_IRQD_C            0xa05f6918  /**< \brief IRQD third register */
#define ASIC_IRQB_A            0xa05f6920  /**< \brief IRQB first register */
#define ASIC_IRQB_B            0xa05f6924  /**< \brief IRQB second register */
#define ASIC_IRQB_C            0xa05f6928  /**< \brief IRQB third register */
#define ASIC_IRQ9_A            0xa05f6930  /**< \brief IRQ9 first register */
#define ASIC_IRQ9_B            0xa05f6934  /**< \brief IRQ9 second register */
#define ASIC_IRQ9_C            0xa05f6938  /**< \brief IRQ9 third register */
/** @} */

/** \defgroup asic_irq_lv           IRQ Levels
    \brief                          values for the various ASIC event IRQ levels
    \ingroup  asic

    You can pick one at hook time, or don't choose anything and the default will
    be used instead.
    @{
*/
#define ASIC_IRQ9           0  /**< \brief IRQ level 9 */
#define ASIC_IRQB           1  /**< \brief IRQ level B (11) */
#define ASIC_IRQD           2  /**< \brief IRQ level D (13) */

#define ASIC_IRQ_MAX        3  /**< \brief Don't take irqs from here up */
#define ASIC_IRQ_DEFAULT    ASIC_IRQ9  /**< \brief Pick an IRQ level for me! */
/** @} */

/** \brief   ASIC event handler type.
    \ingroup asic

    Any event handlers registered must be of this type. These will be run in an
    interrupt context, so don't try anything funny.

    \param  code            The ASIC event code that generated this event.
    \param  data            The user pointer that was passed to
                            \ref asic_evt_set_handler.
    \see    asic_events
*/
typedef void (*asic_evt_handler)(uint32_t code, void *data);

/** \brief   ASIC event handler table entry.
    \ingroup asic
 */
typedef struct {
    asic_evt_handler hdl;
    void *data;
} asic_evt_handler_entry_t;

/** \defgroup asic_ownership Exclusive Event Ownership
    \brief Exclusive ASIC event claims and status inspection
    \ingroup asic
    @{
*/

/** \brief Opaque exclusive claim on one ASIC event source. */
typedef uint32_t asic_evt_claim_t;

/** \brief Invalid ASIC event claim value. */
#define ASIC_EVT_CLAIM_INVALID 0u

/** \brief Coherent ASIC event-source status snapshot. */
typedef struct asic_evt_status {
    uint16_t code;            /**< Event code. */
    uint8_t enabled_levels;   /**< Bit mask of ASIC_IRQ* levels. */
    bool handler_present;     /**< A handler is installed. */
    bool exclusively_claimed; /**< Handler is protected by a claim. */
    uint64_t dispatches;      /**< Number of dispatched occurrences. */
} asic_evt_status_t;

/** \brief Exclusively claim, install, and enable an ASIC event handler.

    Claims prevent legacy handler replacement and give the owner one balanced
    lifecycle for the handler and interrupt mask. A source which already has a
    handler or is enabled at any interrupt level is considered busy; the claim
    does not silently take over pre-existing state. No memory or worker thread
    is allocated. The handler runs in interrupt context.

    \param code            ASIC event code.
    \param irqlevel        One of the ASIC_IRQ* levels.
    \param handler         Interrupt-context handler.
    \param data            Opaque handler argument.
    \param claim           Receives the claim token.
    On failure, `claim` is set to ASIC_EVT_CLAIM_INVALID when it is non-NULL.

    \retval 0              Event claimed and enabled.
    \retval -1             Error, with `EINVAL` or `EBUSY` in `errno`.
*/
int asic_evt_claim(uint16_t code, uint8_t irqlevel,
                   asic_evt_handler handler, void *data,
                   asic_evt_claim_t *claim);

/** \brief Temporarily mask an exclusively claimed ASIC event.

    \retval 0              Event masked.
    \retval -1             Claim is stale or invalid, with `ENOENT` in `errno`.
*/
int asic_evt_claim_mask(asic_evt_claim_t claim);

/** \brief Unmask an exclusively claimed ASIC event.

    \retval 0              Event unmasked at its claimed interrupt level.
    \retval -1             Claim is stale or invalid, with `ENOENT` in `errno`.
*/
int asic_evt_claim_unmask(asic_evt_claim_t claim);

/** \brief Disable, remove, and release an ASIC event claim.

    \retval 0              Claim released.
    \retval -1             Claim is stale or invalid, with `ENOENT` in `errno`.
*/
int asic_evt_release(asic_evt_claim_t claim);

/** \brief Copy a coherent event-source status snapshot.

    \retval 0              Snapshot copied.
    \retval -1             Invalid event or output pointer, with `EINVAL`.
*/
int asic_evt_get_status(uint16_t code, asic_evt_status_t *status);

/** @} */

/** \brief   Set or remove an ASIC handler.
    \ingroup asic

    This function will register an event handler for a given event code, or if
    the handler is NULL, unregister any that is currently registered.

    An exclusively claimed event cannot be changed through this legacy entry
    point. A threaded handler must be removed with asic_evt_remove_handler().
    In either case, the existing entry is returned unchanged and `errno` is set
    to `EBUSY`.

    \param  code            The ASIC event code to hook (see \ref asic_events).
    \param  handler         The function to call when the event happens.
    \param  data            A user pointer that will be passed to the callback.

    Invalid event codes return an empty entry and set `errno` to `EINVAL`.

*/
asic_evt_handler_entry_t asic_evt_set_handler(uint16_t code, asic_evt_handler handler, void *data);

/** \brief   Register a threaded handler with the given ASIC event.
    \ingroup asic

    This function will spawn a thread, that will sleep until notified when an
    event happens. It will then call the handler. As the handler is not called
    in an interrupt context, it can hold locks, and even sleep.

    \param  code            The ASIC event code to hook (see \ref asic_events).
    \param  handler         The function to call when the event happens.
    \param  data            A user pointer that will be passed to the callback.
    \param  ack_and_mask    An optional function that will be called by the real
                            interrupt handler, to acknowledge and mask the
                            interrupt, so that it won't trigger again while the
                            threaded handler is running.
    \param  unmask          An optional function that will be called by the
                            thread after the handler function returned, to
                            re-enable the interrupt.

    \retval 0              Threaded handler installed.
    \retval -1             Invalid input, allocation failure, or an event with
                            an existing handler or claim. `errno` is `EINVAL`,
                            an allocation error, or `EBUSY`, respectively.
*/
int asic_evt_request_threaded_handler(uint16_t code, asic_evt_handler handler,
                                      void *data,
                                      void (*ack_and_mask)(uint16_t),
                                      void (*unmask)(uint16_t));

/** \brief   Unregister any handler set to the given ASIC event.
    \ingroup asic

    Removing a threaded handler from its own worker callback is refused with
    `errno` set to `EDEADLK`; the handler remains installed.
    Invalid event codes set `errno` to `EINVAL`, and claimed events remain
    installed with `errno` set to `EBUSY`.

    \param  code            The ASIC event code to unhook (see
                            \ref asic_events).
*/
void asic_evt_remove_handler(uint16_t code);

/** \brief   Disable all ASIC events.
    \ingroup asic

    This function will disable hooks for every event that has been hooked. In
    order to reinstate them, you must individually re-enable them. Not a very
    good idea to be doing this normally.
*/
void asic_evt_disable_all(void);

/** \brief   Disable one ASIC event.
    \ingroup asic

    This function will disable the hook for a specified code that was registered
    at the given IRQ level. Generally, you will never have to do this yourself
    unless you're adding in some new functionality.

    An invalid event or level sets `errno` to `EINVAL`. An exclusively claimed
    event remains unchanged and sets `errno` to `EBUSY`.

    \param  code            The ASIC event code to unhook (see
                            \ref asic_events).
    \param  irqlevel        The IRQ level it was hooked on (see
                            \ref asic_irq_lv).
*/
void asic_evt_disable(uint16_t code, uint8_t irqlevel);

/** \brief   Enable an ASIC event.
    \ingroup asic

    This function will enable the hook for a specified code and register it at
    the given IRQ level. You should only register each event at a max of one
    IRQ level (this will not check that for you), and this does not actually set
    the hook function for the event, you must do that separately with
    asic_evt_set_handler(). Generally, unless you're adding in new
    functionality, you'll never have to do this.

    An invalid event or level sets `errno` to `EINVAL`. An exclusively claimed
    event remains unchanged and sets `errno` to `EBUSY`.

    \param  code            The ASIC event code to hook (see \ref asic_events).
    \param  irqlevel        The IRQ level to hook on (see \ref asic_irq_lv).
 */
void asic_evt_enable(uint16_t code, uint8_t irqlevel);

/** \cond   Enable ASIC events */
void asic_init(void);
/* Shutdown ASIC events, disabling all hooks. */
void asic_shutdown(void);
/** \endcond */

__END_DECLS

#endif  /* __DC_ASIC_H */
