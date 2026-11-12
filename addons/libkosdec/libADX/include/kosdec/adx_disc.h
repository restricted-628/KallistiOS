/* Original KOS Experimental direct-disc input binding. */
#ifndef KOSDEC_ADX_DISC_H
#define KOSDEC_ADX_DISC_H
#include <kosdec/adx_stream.h>
#include <dc/cdrom.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_ADX_DISC_SECTOR_BYTES 2048u
#define KOSDEC_ADX_DISC_MAX_BUFFER_BYTES 32768u

/* Zero-initialize before first init. Fields are implementation-owned, except
   io_errno may be read for diagnostics. All calls are serialized, in thread
   context. No concurrent range/cursor access, including by another reader.
   State, staging, output and range storage must be disjoint. State, staging
   and range stay alive until close returns DONE. */
struct kosdec_adx_disc {
    uint32_t magic;
    cdrom_sector_range_t *range;
    cdrom_request_t *request;
    uint8_t *buffer;
    size_t buffer_bytes, cache_bytes, pending_bytes;
    uint64_t source_offset, cache_offset, pending_offset;
    uint32_t input_bytes, timeout;
    int io_errno;
    bool closing, closed, cancel_requested;
};

/* Borrow an idle DIRECT sector range and a 32-byte-aligned staging buffer of
   2048..32768 bytes, a multiple of 2048. byte_offset is relative to the range;
   input_bytes is the exact immutable logical source length, not sector padding.
   timeout is nonzero, in the driver's request-timeout units (milliseconds).
   No I/O is submitted here. Nonempty source must fit the range. Init failure
   borrows nothing; reinitialization requires successful close first.
   Keep the disc unchanged. Range/media discovery is the application's job. */
enum kosdec_adx_status kosdec_adx_disc_init(
    struct kosdec_adx_disc *, cdrom_sector_range_t *, uint64_t byte_offset,
    uint32_t input_bytes, void *buffer, size_t buffer_bytes, uint32_t timeout);

/* Provider for stream or ACX/AFS lookup. At most one async submission OR one
   status/retirement attempt per call. WAIT copies nothing and retains no dst.
   Read-ahead uses the separate staging buffer; successful payload is copied
   only after request destruction. Copies at most 512 bytes, within logical EOF.
   A different requested offset during I/O waits for that request to retire;
   later calls may replace the cache. Errors are sticky and discard prefixes.
   Submission takes driver locks/allocates bookkeeping; this is not an IRQ or
   hard-realtime audio callback API. The caller schedules retries/yields. */
struct kosdec_adx_input_result kosdec_adx_disc_read_at(
    void *, uint32_t offset, uint8_t *dst, size_t capacity);

/* Stop admission, request cancellation and attempt retirement without waiting.
   DONE alone proves this binding no longer owns a request or staging access;
   it does NOT mean the I/O succeeded. Range/buffer remain caller-owned and are
   never freed/closed here. NEED_INPUT means retry later. IO_ERROR also retains
   ownership: diagnose io_errno and retry close until DONE. Even a cancelled or
   failed decoder must close its transport. Repeated close after DONE is safe.
   No fiber is destroyed, thread joined or scheduler driven by this library. */
enum kosdec_adx_status kosdec_adx_disc_close(struct kosdec_adx_disc *);
#ifdef __cplusplus
}
#endif
#endif
