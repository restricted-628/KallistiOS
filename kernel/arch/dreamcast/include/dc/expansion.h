/* KallistiOS ##version##

   dc/expansion.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/expansion.h
    \brief   Expansion-port capability discovery.
    \ingroup system_expansion
*/

#ifndef __DC_EXPANSION_H
#define __DC_EXPANSION_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>
#include <stdint.h>

/** \defgroup system_expansion Expansion port
    \brief Bounded discovery of expansion-port devices and capabilities.
    \ingroup system
    @{
*/

/** \brief Expansion device class. */
typedef enum expansion_device_type {
    EXPANSION_DEVICE_NONE = 0,       /**< No recognized device. */
    EXPANSION_DEVICE_BROADBAND,      /**< PCI Ethernet adapter. */
    EXPANSION_DEVICE_LAN,            /**< 8-bit Ethernet adapter. */
    EXPANSION_DEVICE_MODEM,          /**< 8-bit modem. */
    EXPANSION_DEVICE_UNKNOWN_8BIT,   /**< Active unclassified 8-bit device. */
    EXPANSION_DEVICE_UNKNOWN_PCI     /**< Active unclassified PCI device. */
} expansion_device_type_t;

/** \name Expansion capability flags
    @{
*/
#define EXPANSION_CAP_8BIT       0x00000001u /**< 8-bit interface. */
#define EXPANSION_CAP_PCI        0x00000002u /**< PCI bridge interface. */
#define EXPANSION_CAP_IRQ        0x00000004u /**< External interrupt. */
#define EXPANSION_CAP_DMA        0x00000008u /**< G2 DMA transfers. */
#define EXPANSION_CAP_NETWORK    0x00000010u /**< Network connectivity. */
#define EXPANSION_CAP_ETHERNET   0x00000020u /**< Ethernet frames. */
#define EXPANSION_CAP_TELEPHONY  0x00000040u /**< Telephone-line modem. */
#define EXPANSION_CAP_10MBIT     0x00000080u /**< 10 Mbit Ethernet. */
#define EXPANSION_CAP_100MBIT    0x00000100u /**< 100 Mbit Ethernet. */
/** @} */

/** \name Expansion probe flags
    @{
*/
#define EXPANSION_PROBE_DEFAULT    0x00000000u /**< Side-effect-free probe. */
#define EXPANSION_PROBE_RESET_8BIT 0x00000001u /**< Permit 8-bit reset tests. */
/** @} */

/** \brief Coherent result of one expansion-port probe. */
typedef struct expansion_status {
    expansion_device_type_t type; /**< Detected device class. */
    uint32_t capabilities;        /**< EXPANSION_CAP_* flags. */
    uint32_t probe_flags;         /**< Flags used for this probe. */
    uint32_t sequence;            /**< Process-local probe sequence. */
    uint32_t maximum_bps;         /**< Known maximum line rate, or zero. */
    int probe_error;              /**< Zero or the errno value on failure. */
    bool present;                 /**< A device was identified. */
    bool active;                  /**< An expansion interrupt is owned. */
    bool complete;                /**< All permitted probe stages ran. */
    bool reset_performed;         /**< The 8-bit interface was reset. */
} expansion_status_t;

/** \brief Probe the expansion port and copy a capability snapshot.

    The default probe performs only read-only PCI signature inspection and
    observes existing external-interrupt ownership. It cannot determine which
    inactive 8-bit device is connected, so \ref expansion_status_t::complete is
    false in that case.

    EXPANSION_PROBE_RESET_8BIT permits bounded reset and self-test operations
    for the LAN adapter and modem. It is refused while an 8-bit interrupt owner
    is active. The call may take several hundred milliseconds and must run in
    ordinary thread context.

    No worker, cache, or persistent allocation is created.

    \retval 0               Snapshot copied, including a negative finding.
    \retval -1              Invalid arguments, active owner, or probe failure.
*/
int expansion_probe(expansion_status_t *status, uint32_t flags);

/** @} */

__END_DECLS

#endif /* __DC_EXPANSION_H */
