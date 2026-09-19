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

    Allocation and release must be performed from thread context. A lease
    remains valid until explicitly released or the bridge is shut down.

    @{
*/

#define GAPS_SRAM_PHYS_BASE 0x01840000u /**< Physical SRAM base. */
#define GAPS_SRAM_SIZE      0x00008000u /**< SRAM size in bytes. */
#define GAPS_SRAM_ALIGNMENT 32u        /**< Smallest allocation unit. */

/** \brief Invalid SRAM lease value. */
#define GAPS_SRAM_LEASE_INVALID (-1)

/** \brief Opaque SRAM allocation handle. */
typedef int gaps_sram_lease_t;

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

/** \brief Initialize the bridge and its SRAM allocator.

    Calls are reference-counted. Each successful call must be paired with
    \ref gaps_shutdown. The first initialization clears the complete SRAM
    window before allocations are admitted.

    \retval 0 Success.
    \retval -1 Failure; errno is set.
*/
int gaps_init(void);

/** \brief Release one bridge reference.

    The final release fails with `EBUSY` while SRAM leases remain active.

    \retval 0 Success.
    \retval -1 Failure; errno is set.
*/
int gaps_shutdown(void);

/** \brief Allocate a contiguous SRAM range using first fit.

    \param size       Nonzero number of bytes. Rounded up to 32 bytes.
    \param alignment  Power-of-two alignment from 32 through 32768 bytes.
    \param lease      Receives an opaque lease handle.

    \retval 0 Success.
    \retval -1 Failure; errno is set.
*/
int gaps_sram_alloc(size_t size, size_t alignment,
                    gaps_sram_lease_t *lease);

/** \brief Reserve one exact SRAM range.

    This is useful for devices whose hardware layout has fixed offsets. Both
    offset and size must be multiples of 32 bytes.

    \retval 0 Success.
    \retval -1 Failure; errno is set.
*/
int gaps_sram_reserve(size_t offset, size_t size,
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
