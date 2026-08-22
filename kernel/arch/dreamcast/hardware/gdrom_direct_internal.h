/* KallistiOS ##version##

   Private direct-GD-ROM interfaces used by the request engine.
   Copyright (C) 2026 Joseph Black
*/

#ifndef __KERNEL_ARCH_DREAMCAST_HARDWARE_GDROM_DIRECT_INTERNAL_H
#define __KERNEL_ARCH_DREAMCAST_HARDWARE_GDROM_DIRECT_INTERNAL_H

#include <dc/gdrom_direct.h>

#include "cdrom_request.h"

#include <stdint.h>

typedef struct gdrom_direct_stream gdrom_direct_stream_t;

/* Execute one direct DMA segment with cancellation, wakeup, and progress
   owned by request. This is intentionally private: public callers use the
   normal direct sync/async entry points. */
int gdrom_direct_read_sectors_dma_request(
    cdrom_request_t *request, void *buffer, uint32_t fad, size_t sectors,
    gdrom_direct_sector_type_t sector_type, uint32_t timeout,
    gdrom_direct_result_t *result);

/* The caller already owns G1. Used only by non-queueing background sampling;
   the bounded packet operation does not release that ownership. */
int gdrom_direct_get_status_locked(
    gdrom_direct_status_t *status, uint32_t timeout,
    gdrom_direct_result_t *result);

/* Finalizer-aware constructor used by ISO9660 to retain and serialize its
   descriptor while a direct pickup seek is queued or running. */
cdrom_request_t *gdrom_direct_seek_async_internal(
    uint32_t fad, uint32_t timeout, gdrom_direct_result_t *result,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data);

/* One direct CD_READ2 command remains active across application-directed
   Holly DMA transfers. begin owns G1 until end releases it. */
gdrom_direct_stream_t *gdrom_direct_stream_begin(
    cdrom_request_t *owner, semaphore_t *wake, uint32_t fad,
    size_t sectors, gdrom_direct_sector_type_t sector_type,
    uint32_t timeout, gdrom_direct_result_t *result);
int gdrom_direct_stream_transfer(
    gdrom_direct_stream_t *stream, cdrom_request_t *owner,
    cdrom_request_t *transfer, void *buffer, size_t bytes,
    uint32_t timeout, gdrom_direct_result_t *result);
int gdrom_direct_stream_end(gdrom_direct_stream_t *stream,
                            gdrom_direct_result_t *result);

#endif /* __KERNEL_ARCH_DREAMCAST_HARDWARE_GDROM_DIRECT_INTERNAL_H */
