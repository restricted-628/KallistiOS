/* KallistiOS ##version##

   Private interfaces shared by the Dreamcast GD-ROM and ISO9660 drivers.
   Copyright (C) 2026 Joseph Black
*/

#ifndef __KERNEL_ARCH_DREAMCAST_HARDWARE_CDROM_REQUEST_H
#define __KERNEL_ARCH_DREAMCAST_HARDWARE_CDROM_REQUEST_H

#include <dc/cdrom.h>
#include <dc/gdrom_direct.h>
#include <dc/syscalls.h>

#include <kos/sem.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Runs before terminal status publication in normal thread context. It must
   be bounded and context-agnostic: queued shutdown cancellation invokes it on
   the shutdown caller instead of the GD request worker. */
typedef void (*cdrom_request_finalizer_t)(
    cdrom_request_t *request, const cdrom_request_status_t *status, void *data);

/* One physical command in a logical DMA request. The request engine owns the
   installed copy. data_direct indicates that useful bytes land directly in
   the caller-visible destination; bounced data becomes visible only after the
   continuation has copied it. */
typedef struct cdrom_request_dma_segment {
    cd_read_params_t params;
    void *buffer;
    size_t io_bytes;
    size_t data_offset;
    size_t data_bytes;
    bool cacheable;
    bool data_direct;
} cdrom_request_dma_segment_t;

/* Runs on the request worker after a successful segment and cache publication,
   with G1 ownership released. It may copy bounced data, update driver state,
   and fill next. Returning a positive value requeues the same logical request
   with next, zero completes it, and a negative value fails the request. The
   request object remains opaque to the driver. */
typedef int (*cdrom_request_continue_t)(
    cdrom_request_t *request,
    const cdrom_request_dma_segment_t *completed,
    cdrom_request_dma_segment_t *next, void *data);

typedef void (*cdrom_stream_session_finalizer_t)(
    cdrom_stream_session_t *session,
    const cdrom_stream_session_status_t *status, void *data);

/* Runs on the GD request worker and owns any controller access it performs.
   It returns a stable KOS ERR_* result; the common request layer publishes
   terminal state and dispatches the callback. */
typedef int (*cdrom_request_executor_t)(cdrom_request_t *request, void *data);

int cdrom_request_system_init(void);
void cdrom_request_system_shutdown(void);

/* Select the transport used only by the non-queueing media sampler. */
void cdrom_media_monitor_use_direct(bool direct);

/* Queue a media-significant request result for thread-context dispatch after
   the request releases G1. Non-media errors are ignored and repeated reports
   may be coalesced. */
void cdrom_media_monitor_report_result(
    int result, cdrom_request_backend_t backend);

/* Decode the firmware/SPI 14-byte Q-channel layout shared by both transports. */
void cdrom_decode_cdda_status_internal(
    const uint8_t subcode[14], cdrom_cdda_status_t *status);

/* Raw range and ISO planners require fixed 2,048-byte cooked sectors. */
size_t cdrom_sector_size_internal(void);

cdrom_request_t *cdrom_request_submit_internal(
    cd_cmd_code_t command, const void *params, size_t params_size,
    uint32_t timeout, cdrom_request_finalizer_t finalizer,
    void *finalizer_data, cdrom_request_callback_t callback,
    void *callback_data);

int cdrom_request_dma_segment_init(
    cdrom_request_dma_segment_t *segment, void *buffer, uint32_t sector,
    size_t sector_count, size_t data_offset, size_t data_bytes,
    bool data_direct);

int cdrom_request_dma_segment_init_sized(
    cdrom_request_dma_segment_t *segment, void *buffer, uint32_t sector,
    size_t sector_count, size_t sector_size, size_t data_offset,
    size_t data_bytes, bool data_direct);

cdrom_request_t *cdrom_request_submit_dma_chain(
    const cdrom_request_dma_segment_t *first, size_t requested_bytes,
    size_t data_bytes, size_t io_bytes, uint32_t timeout,
    cdrom_request_continue_t continuation, void *continuation_data,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data);

cdrom_request_t *cdrom_request_submit_direct_dma_chain(
    const cdrom_request_dma_segment_t *first,
    gdrom_direct_sector_type_t sector_type,
    size_t requested_bytes, size_t data_bytes, size_t io_bytes,
    uint32_t timeout, cdrom_request_continue_t continuation,
    void *continuation_data, cdrom_request_finalizer_t finalizer,
    void *finalizer_data, cdrom_request_callback_t callback,
    void *callback_data);

cdrom_request_t *cdrom_request_submit_noop(
    cd_cmd_code_t command, size_t requested_bytes,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data);

cdrom_request_t *cdrom_request_submit_executor(
    cd_cmd_code_t command, const void *params, size_t params_size,
    size_t requested_bytes, size_t data_bytes, size_t io_bytes,
    uint32_t timeout, cdrom_request_executor_t executor,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data);

bool cdrom_request_cancel_requested_internal(
    const cdrom_request_t *request);
semaphore_t *cdrom_request_event_internal(cdrom_request_t *request);
void cdrom_request_update_direct_progress(
    cdrom_request_t *request, size_t io_completed_bytes);

cdrom_request_t *cdrom_read_sectors_async_internal(
    void *buffer, uint32_t sector, size_t cnt, uint32_t timeout,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data);

cdrom_request_t *cdrom_seek_async_internal(
    uint32_t sector, uint32_t timeout,
    cdrom_request_finalizer_t finalizer, void *finalizer_data,
    cdrom_request_callback_t callback, void *callback_data);

cdrom_stream_session_t *cdrom_stream_session_start_internal(
    uint32_t sector, size_t sector_count, size_t sector_size,
    size_t data_bytes, uint32_t start_timeout, uint32_t idle_timeout,
    cdrom_request_backend_t backend,
    gdrom_direct_sector_type_t direct_sector_type,
    cdrom_stream_session_finalizer_t finalizer, void *finalizer_data);

#endif /* __KERNEL_ARCH_DREAMCAST_HARDWARE_CDROM_REQUEST_H */
