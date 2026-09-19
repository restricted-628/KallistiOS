/* KallistiOS ##version##

   dc/pvr/pvr_chunk_asset_io.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_asset_io.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <dc/g2bus.h>
#include <kos/irq.h>
#include <kos/timer.h>

#define PVR_CHUNK_ASSET_MAX_FAD UINT32_C(0x00ffffff)

static int deadline_remaining(uint64_t deadline, uint32_t *remaining) {
    uint64_t now = timer_ms_gettime64();
    uint64_t left;

    if(now >= deadline) {
        errno = ETIMEDOUT;
        return -1;
    }

    left = deadline - now;
    *remaining = left > UINT32_MAX ? UINT32_MAX : (uint32_t)left;
    return 0;
}

static int physical_span(size_t asset_bytes, size_t *physical_bytes,
                         size_t *sectors) {
    const size_t mask = GDROM_DIRECT_SECTOR_SIZE - 1u;

    if(!asset_bytes || asset_bytes > SIZE_MAX - mask) {
        errno = asset_bytes ? EOVERFLOW : EINVAL;
        return -1;
    }

    *physical_bytes = (asset_bytes + mask) & ~mask;
    *sectors = *physical_bytes / GDROM_DIRECT_SECTOR_SIZE;
    return 0;
}

static int validate_config(const pvr_chunk_asset_direct_config_t *config,
                           const void *storage, size_t storage_bytes,
                           const pvr_chunk_asset_view_t *view,
                           size_t *physical_bytes, size_t *sectors,
                           gaps_sram_info_t *gaps_info,
                           size_t *staging_sectors) {
    size_t available;

    if(!config || !storage || !view || irq_inside_int()
            || ((uintptr_t)storage & (PVR_CHUNK_ASSET_ALIGNMENT - 1u))
            || !config->timeout || config->fad < 150u
            || config->fad > PVR_CHUNK_ASSET_MAX_FAD
            || (config->sector_type != GDROM_DIRECT_SECTOR_MODE1
                && config->sector_type != GDROM_DIRECT_SECTOR_MODE2_FORM1)
            || (config->path != PVR_CHUNK_ASSET_DIRECT_TO_RAM
                && config->path != PVR_CHUNK_ASSET_DIRECT_VIA_GAPS)) {
        errno = irq_inside_int() ? EPERM : EINVAL;
        return -1;
    }

    if(physical_span(config->asset_bytes, physical_bytes, sectors) < 0)
        return -1;
    if(storage_bytes < *physical_bytes
            || *sectors - 1u > PVR_CHUNK_ASSET_MAX_FAD - config->fad) {
        errno = storage_bytes < *physical_bytes ? ENOSPC : EOVERFLOW;
        return -1;
    }

    *staging_sectors = GDROM_DIRECT_DMA_MAX_SECTORS;
    if(config->path == PVR_CHUNK_ASSET_DIRECT_TO_RAM)
        return 0;

    if(config->gaps_lease == GAPS_SRAM_LEASE_INVALID) {
        errno = EBADF;
        return -1;
    }
    if((config->gaps_offset & (GDROM_DIRECT_SECTOR_SIZE - 1u))
            || (config->g2_channel != G2_DMA_CHAN_CH2
                && config->g2_channel != G2_DMA_CHAN_CH3)) {
        errno = EINVAL;
        return -1;
    }
    if(gaps_sram_get_info(config->gaps_lease, gaps_info) < 0)
        return -1;
    if(config->gaps_offset >= gaps_info->size) {
        errno = ENOSPC;
        return -1;
    }

    available = gaps_info->size - config->gaps_offset;
    *staging_sectors = available / GDROM_DIRECT_SECTOR_SIZE;
    if(*staging_sectors > GDROM_DIRECT_DMA_MAX_SECTORS)
        *staging_sectors = GDROM_DIRECT_DMA_MAX_SECTORS;
    if(!*staging_sectors) {
        errno = ENOSPC;
        return -1;
    }
    return 0;
}

static int read_gaps_chunk(const pvr_chunk_asset_direct_config_t *config,
                           const gaps_sram_info_t *gaps_info, void *destination,
                           uint32_t fad, size_t sectors, uint64_t deadline,
                           pvr_chunk_asset_direct_result_t *result) {
    size_t bytes = sectors * GDROM_DIRECT_SECTOR_SIZE;
    uint32_t remaining;
    uint64_t started;

    if(deadline_remaining(deadline, &remaining) < 0)
        return -1;
    started = timer_ms_gettime64();
    if(gdrom_direct_read_sectors_dma_gaps(
            config->gaps_lease, config->gaps_offset, fad, sectors,
            config->sector_type, remaining, &result->transport) < 0) {
        result->gdrom_milliseconds += timer_ms_gettime64() - started;
        return -1;
    }
    result->gdrom_milliseconds += timer_ms_gettime64() - started;

    if(deadline_remaining(deadline, &remaining) < 0)
        return -1;
    started = timer_ms_gettime64();
    if(g2_dma_transfer(destination,
                       (void *)(uintptr_t)(gaps_info->physical_address
                                           + config->gaps_offset),
                       bytes, 0, NULL, NULL, G2_DMA_TO_SH4, 0,
                       config->g2_channel, 0) < 0) {
        result->g2_milliseconds += timer_ms_gettime64() - started;
        return -1;
    }
    if(deadline_remaining(deadline, &remaining) < 0) {
        int saved_errno = errno;

        (void)g2_dma_cancel(config->g2_channel);
        result->g2_milliseconds += timer_ms_gettime64() - started;
        errno = saved_errno;
        return -1;
    }
    if(g2_dma_wait(config->g2_channel, remaining) < 0) {
        int saved_errno = errno;

        /* This call owns the selected generic channel exclusively. Canceling
           on every wait failure prevents a late DMA write after publication;
           a transfer which raced to terminal simply makes cancel a no-op. */
        (void)g2_dma_cancel(config->g2_channel);
        result->g2_milliseconds += timer_ms_gettime64() - started;
        errno = saved_errno;
        return -1;
    }
    result->g2_milliseconds += timer_ms_gettime64() - started;
    return 0;
}

int pvr_chunk_asset_read_direct(
    const pvr_chunk_asset_direct_config_t *config, void *storage,
    size_t storage_bytes, pvr_chunk_asset_view_t *view,
    pvr_chunk_asset_direct_result_t *result) {
    pvr_chunk_asset_direct_result_t local_result;
    pvr_chunk_asset_direct_result_t *observed = result ? result : &local_result;
    pvr_chunk_asset_view_t parsed;
    gaps_sram_info_t gaps_info = { 0 };
    size_t physical_bytes;
    size_t sectors;
    size_t staging_sectors;
    size_t completed_sectors = 0;
    uint32_t remaining;
    uint64_t started = timer_ms_gettime64();
    uint64_t deadline;
    int saved_errno;

    memset(observed, 0, sizeof(*observed));
    if(view)
        memset(view, 0, sizeof(*view));

    if(validate_config(config, storage, storage_bytes, view, &physical_bytes,
                       &sectors, &gaps_info, &staging_sectors) < 0) {
        saved_errno = errno;
        observed->total_milliseconds = timer_ms_gettime64() - started;
        errno = saved_errno;
        return -1;
    }

    observed->path = config->path;
    observed->asset_bytes = config->asset_bytes;
    observed->physical_bytes = physical_bytes;
    deadline = started + config->timeout;

    while(completed_sectors < sectors) {
        size_t chunk = sectors - completed_sectors;
        uint8_t *destination = (uint8_t *)storage
            + completed_sectors * GDROM_DIRECT_SECTOR_SIZE;
        uint32_t fad = config->fad + (uint32_t)completed_sectors;
        uint64_t gdrom_started;

        if(chunk > staging_sectors)
            chunk = staging_sectors;

        if(config->path == PVR_CHUNK_ASSET_DIRECT_VIA_GAPS) {
            if(read_gaps_chunk(config, &gaps_info, destination, fad, chunk,
                               deadline, observed) < 0)
                goto fail;
        }
        else {
            if(deadline_remaining(deadline, &remaining) < 0)
                goto fail;
            gdrom_started = timer_ms_gettime64();
            if(gdrom_direct_read_sectors_dma(
                    destination, fad, chunk, config->sector_type, remaining,
                    &observed->transport) < 0) {
                observed->gdrom_milliseconds +=
                    timer_ms_gettime64() - gdrom_started;
                goto fail;
            }
            observed->gdrom_milliseconds +=
                timer_ms_gettime64() - gdrom_started;
        }

        completed_sectors += chunk;
        ++observed->chunks;
    }

    if(deadline_remaining(deadline, &remaining) < 0)
        goto fail;
    if(pvr_chunk_asset_open(storage, config->asset_bytes, &parsed) < 0)
        goto fail;
    if(parsed.size != config->asset_bytes) {
        errno = EILSEQ;
        goto fail;
    }
    if(deadline_remaining(deadline, &remaining) < 0)
        goto fail;

    *view = parsed;
    observed->total_milliseconds = timer_ms_gettime64() - started;
    return 0;

fail:
    saved_errno = errno;
    observed->total_milliseconds = timer_ms_gettime64() - started;
    errno = saved_errno;
    return -1;
}
