/* KallistiOS ##version##

   dc/pvr_chunk_asset_io.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_asset_io.h
    \brief   Bounded direct-disc input for compact-model assets.
    \ingroup pvr_chunk_model

    This optional layer joins the direct GD-ROM and GAPS/G2 DMA facilities to
    the storage-independent compact-model container. It performs no allocation
    and starts no worker, service, thread, or fiber.
*/

#ifndef __DC_PVR_CHUNK_ASSET_IO_H
#define __DC_PVR_CHUNK_ASSET_IO_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/gaps.h>
#include <dc/g2bus.h>
#include <dc/gdrom_direct.h>
#include <dc/pvr_chunk_asset.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Physical path used to fill compact-model asset storage. */
typedef enum pvr_chunk_asset_direct_path {
    /** Direct GD DMA from the drive into caller-owned system RAM. */
    PVR_CHUNK_ASSET_DIRECT_TO_RAM = 0,
    /** Direct GD DMA into leased GAPS SRAM, then G2 DMA into system RAM. */
    PVR_CHUNK_ASSET_DIRECT_VIA_GAPS = 1
} pvr_chunk_asset_direct_path_t;

/** \brief Parameters for one bounded direct-disc asset read. */
typedef struct pvr_chunk_asset_direct_config {
    uint32_t fad;                  /**< First asset sector, at least 150. */
    size_t asset_bytes;            /**< Exact logical container size. */
    gdrom_direct_sector_type_t sector_type; /**< Exact cooked-sector type. */
    uint32_t timeout;              /**< Nonzero whole-operation deadline, ms. */
    pvr_chunk_asset_direct_path_t path; /**< Selected physical data path. */
    gaps_sram_lease_t gaps_lease;  /**< Required only for VIA_GAPS. */
    size_t gaps_offset;            /**< Sector-aligned offset in the lease. */
    uint32_t g2_channel;           /**< VIA_GAPS channel; CH2 or CH3 only. */
} pvr_chunk_asset_direct_config_t;

/** \brief Timing and transport observations from a direct-disc asset read. */
typedef struct pvr_chunk_asset_direct_result {
    pvr_chunk_asset_direct_path_t path;
    size_t asset_bytes;            /**< Logical container bytes admitted. */
    size_t physical_bytes;         /**< Full cooked-sector bytes transferred. */
    size_t chunks;                 /**< Number of bounded GD commands. */
    uint64_t gdrom_milliseconds;   /**< Time spent in direct GD operations. */
    uint64_t g2_milliseconds;      /**< Time spent in G2 staging operations. */
    uint64_t total_milliseconds;   /**< Complete read and admission time. */
    gdrom_direct_result_t transport; /**< Last direct transport observation. */
} pvr_chunk_asset_direct_result_t;

/** \brief Read and admit one compact-model asset without BIOS filesystem I/O.

    The destination must be 32-byte-aligned system RAM and large enough for
    `asset_bytes` rounded up to a complete 2048-byte sector. Padding after the
    logical asset remains available in storage but is excluded from parsing.

    The RAM path issues bounded direct GD-DMA commands into successive pieces
    of storage. The GAPS path serializes each GD-DMA leg with a timed G2-DMA
    leg from an explicitly supplied live SRAM lease. Its usable staging range
    starts at `gaps_offset`; the function automatically selects the largest
    whole-sector chunk that fits, up to the direct transport's command bound.
    The caller must exclusively own G2 channel CH2 or CH3 for the duration of
    a staged call. The AICA and BBA channels are never accepted for staging.

    One nonzero timeout covers the complete operation, including container
    validation. A timed-out G2 leg is cancelled before this function returns.
    On failure, result contains completed timing and the last transport
    observation when supplied, but storage contents are undefined.
    No LZ4 decoding is performed; the returned view may be passed to the
    synchronous, incrementally stepped, or service-fiber decoder.

    \retval 0 The complete container was transferred and structurally valid.
    \retval -1 Validation, deadline, ownership, transport, G2, or asset error.

    \par Error Conditions:
    \em EINVAL - Invalid argument, path, alignment, sector type, or G2 channel.\n
    \em EBADF - Invalid or stale GAPS lease.\n
    \em ENOSPC - Destination or staging range is too small.\n
    \em EOVERFLOW - Asset span or FAD range cannot be represented.\n
    \em ETIMEDOUT - The whole-operation deadline expired.\n
    \em EILSEQ - The transferred container is malformed or not the exact size.\n
    Other direct-transport, G1, G2, and asset-admission errors are forwarded.
*/
int pvr_chunk_asset_read_direct(
    const pvr_chunk_asset_direct_config_t *config, void *storage,
    size_t storage_bytes, pvr_chunk_asset_view_t *view,
    pvr_chunk_asset_direct_result_t *result);

/** @} */

__END_DECLS
#endif /* __DC_PVR_CHUNK_ASSET_IO_H */
