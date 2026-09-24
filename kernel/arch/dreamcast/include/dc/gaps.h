/* KallistiOS ##version##

   dc/gaps.h

   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/gaps.h
    \brief   GAPS bridge and shared SRAM management.
    \ingroup system_gaps
*/

#ifndef __DC_GAPS_H
#define __DC_GAPS_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

/** \defgroup system_gaps GAPS Bridge
    \brief Expansion bridge lifecycle and on-board SRAM allocation.
    \ingroup system

    The bridge exposes one flat 32 KiB SRAM window. Allocations are software
    ownership records; they do not create independent DMA banks or permit two
    bus masters to use the window concurrently.

    One explicit owner holds the whole bridge, either for staging or networking.
    SRAM leases subdivide that owner's memory; they do not share the bridge
    between drivers. Acquisition, allocation and release require thread context.
    A lease remains valid until explicitly released; ownership cannot be released
    while leases remain. No implicit preemption or driver switching is performed.

    Both NETWORK and STAGING owners may authorize G1 or G2 DMA through their
    leases. The role is software ownership policy, not a DMA addressability
    restriction. Transfers are serialized across the whole SRAM window. A
    networking owner must also coordinate NIC RX/TX use of the target buffers;
    the transfer claim does not fence the NIC's autonomous accesses.

    @{
*/

#define GAPS_SRAM_PHYS_BASE 0x01840000u /**< Physical SRAM base. */
#define GAPS_SRAM_SIZE      0x00008000u /**< SRAM size in bytes. */
#define GAPS_SRAM_ALIGNMENT 32u        /**< Smallest allocation unit. */

/** \brief Invalid SRAM lease value. */
#define GAPS_SRAM_LEASE_INVALID (-1)

/** \brief Opaque SRAM allocation handle. */
typedef int gaps_sram_lease_t;

/** Whole-device ownership token. Not a thread ID; keep until safe shutdown. */
typedef uint32_t gaps_owner_t;
#define GAPS_OWNER_INVALID 0u

typedef enum gaps_role {
    GAPS_ROLE_NONE = 0,
    GAPS_ROLE_STAGING,
    GAPS_ROLE_NETWORK,
    GAPS_ROLE_LOADER,     /**< Resident IP/unknown loader; query only. */
    GAPS_ROLE_TRANSITION  /**< Acquisition/release in progress; query only. */
} gaps_role_t;

/** Read the ownership policy state (not a hardware-presence probe).
 * An unclassified resident loader is conservatively reported as LOADER.
 */
gaps_role_t gaps_get_role(void);

/** \brief Description of one active SRAM lease. */
typedef struct gaps_sram_info {
    uint32_t physical_address; /**< Physical G2 address of the allocation. */
    size_t size;               /**< Allocated byte count. */
    size_t offset;             /**< Offset from \ref GAPS_SRAM_PHYS_BASE. */
} gaps_sram_info_t;

/** \brief Probe for a compatible bridge without initializing it.

    \retval 1 Bridge detected.
    \retval 0 Bridge not detected.
*/
int gaps_probe(void);

/** Acquire exclusive STAGING or NETWORK ownership and initialize the bridge.
 * Repeated acquisition, even for the same role, fails with EBUSY. STAGING also
 * rejects a resident IP/unknown dcload before any register write or SRAM clear.
 * NETWORK is the deliberate driver takeover path; the caller must arrange KOS
 * loader-service transport before resuming console/file use. Staging leaves
 * NIC interrupts, RX/TX and PCI bus mastering disabled. Acquisition clears SRAM.
 * There is no force-steal API or implicit loader disconnect.
 */
int gaps_acquire(gaps_role_t role, gaps_owner_t *owner);

/** Release a valid owner after its driver has stopped all hardware activity.
 * EBUSY while any SRAM lease remains; EBADF for a stale/wrong token. A networking
 * caller must first stop its workers, NIC and transfers. No waiting or driver
 * shutdown is hidden here. Resident loader protection returns after release;
 * a loader-booted application cannot switch to staging merely by stopping KOS
 * networking. Boot via disc/serial for staging until a loader-detach API exists.
 */
int gaps_release(gaps_owner_t owner);

/** \brief Allocate a contiguous SRAM range using first fit.

    \param owner      Current exclusive owner token.
    \param size       Nonzero number of bytes. Rounded up to 32 bytes.
    \param alignment  Power-of-two alignment from 32 through 32768 bytes.
    \param lease      Receives an opaque lease handle.

    \retval 0 Success.
    \retval -1 Failure; errno is set.
*/
int gaps_sram_alloc(gaps_owner_t owner, size_t size, size_t alignment,
                    gaps_sram_lease_t *lease);

/** \brief Reserve one exact SRAM range.

    This is useful for devices whose hardware layout has fixed offsets. Both
    offset and size must be multiples of 32 bytes.

    \retval 0 Success.
    \retval -1 Failure; errno is set.
*/
int gaps_sram_reserve(gaps_owner_t owner, size_t offset, size_t size,
                      gaps_sram_lease_t *lease);

/** \brief Copy information about an active lease.

    \retval 0 Success.
    \retval -1 Invalid or stale handle; errno is set to `EBADF`.
*/
int gaps_sram_get_info(gaps_sram_lease_t lease, gaps_sram_info_t *info);

/** \brief Release an SRAM lease.

    \retval 0 Success.
    \retval -1 Invalid/stale handle (`EBADF`) or active DMA claim (`EBUSY`).
*/
int gaps_sram_free(gaps_sram_lease_t lease);

/** @} */

__END_DECLS
#endif /* __DC_GAPS_H */
