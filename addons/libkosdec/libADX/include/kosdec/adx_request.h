/* Original caller-serviced sector requests over an immutable byte provider. */
#ifndef KOSDEC_ADX_REQUEST_H
#define KOSDEC_ADX_REQUEST_H
#include <kosdec/adx_stream.h>
#include <kosdec/adx_joint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_ADX_REQUEST_SECTOR_BYTES 2048u
enum kosdec_adx_request_status {
    KOSDEC_ADX_REQUEST_STOPPED, KOSDEC_ADX_REQUEST_READING,
    KOSDEC_ADX_REQUEST_READEND, KOSDEC_ADX_REQUEST_WAIT,
    KOSDEC_ADX_REQUEST_BUSY, KOSDEC_ADX_REQUEST_BAD_ARGUMENT,
    KOSDEC_ADX_REQUEST_IO_ERROR, KOSDEC_ADX_REQUEST_TRUNCATED,
    KOSDEC_ADX_REQUEST_NEED_OUTPUT
};
enum kosdec_adx_request_origin {
    KOSDEC_ADX_REQUEST_SET, KOSDEC_ADX_REQUEST_CUR, KOSDEC_ADX_REQUEST_END
};
struct kosdec_adx_request {
    uint32_t magic, input_bytes, sectors, cursor, start, requested;
    uint32_t source_bytes, completed;
    size_t written, output_bytes;
    struct kosdec_adx_joint *joint;
    struct kosdec_adx_chunk held;
    size_t held_used;
    uint8_t *output;
    kosdec_adx_read_at read_at;
    void *context;
    enum kosdec_adx_request_status state;
};
struct kosdec_adx_request_info {
    uint32_t input_bytes, sectors, cursor, start, requested, completed;
    uint32_t source_bytes;
    size_t output_bytes;
    enum kosdec_adx_request_status state;
};
/* Initial/after close only. No allocation, provider calls, locks or scheduler.
   State, output and provider storage are disjoint; never copy live state. All
   operations externally serialized, including with other users of the provider.
   Same synchronous read_at contract as streams: never retain destination or
   reenter; provider owns and retires any independent staging/DMA. Provider and
   context stay alive through request close AND their own backend retirement.
   Extent0..UINT32_MAX; sector count rounds up. Returns STOPPED on success.
   Invalid arguments leave state unchanged. */
enum kosdec_adx_request_status kosdec_adx_request_init(struct kosdec_adx_request *,
    kosdec_adx_read_at, void *context, uint32_t input_bytes);
/* Admit min(sectors, remaining sectors) at current cursor. Output capacity must
   fit all admitted sectors, overflow checked; NULL allowed for zero admission.
   Byte alignment is sufficient for this copied-input API (not a DMA endpoint).
   No I/O or cursor advance. Returns READING, or READEND for zero/EOF. While
   READING returns BUSY unchanged. Other invalid arguments also leave state intact.
   Output remains borrowed until terminal step, stop, valid seek or close; do not
   read partially written data concurrently. No trailing file bytes are exposed:
   the final partial source sector is zero-padded by bounded service steps. */
enum kosdec_adx_request_status kosdec_adx_request_start(struct kosdec_adx_request *,
    uint32_t sectors, uint8_t *output, size_t capacity);
/* Stream a sector range into a RING or QUEUE joint. Same EOF-clipped admission,
   but publish only exact source bytes: no last-sector padding. READEND counts
   that short final sector as completed. Joint capacity need not be sector-sized.
   Joint/backing/workspace remain borrowed while active; caller serializes all
   DATA consumption and must not reset, close, seal, reinit or use another FREE
   producer until this request is stopped/complete. DATA consumption is allowed.
   Existing FREE borrow returns BUSY; sealed/memory/invalid joints are rejected.
   The request may hold one FREE span across steps. Ring/JOIN spans<=512; a
   SEPARATE head is filled in <=512-byte steps and published as a whole chunk.
   Tail splitting may need a spare queue node; NEED_OUTPUT retains ownership
   until DATA recycling makes room or caller stops. Provision spare nodes for
   partial SEPARATE tails. No hidden copy/worker or automatic seal. Explicitly
   finish the joint only after the last request succeeds; stop leaves already
   published DATA intact (reset/drain explicitly before replay if appropriate).
   Rebuild consumers: request state grows to hold a destination borrow. */
enum kosdec_adx_request_status kosdec_adx_request_start_joint(
    struct kosdec_adx_request *, uint32_t sectors, struct kosdec_adx_joint *);
/* One provider call<=512 bytes OR <=512 bytes of zero padding. Callback may
   block; run on a suitable owner thread/fiber, never audio IRQ. WAIT makes no
   progress and must be rescheduled, not spun. Positive short reads are progress.
   END before declared extent is TRUNCATED; inconsistent replies are IO_ERROR.
   Completed sectors=floor(published output/2048), except a completed joint
   request includes its last short sector. source_bytes excludes padding and
   may include bytes in a held, unpublished FREE span. Joint NEED_OUTPUT means
   no FREE bytes or queue nodes; it makes no new provider call while blocked.
   Cursor stays at request start while reading and advances once by completed
   sectors on READEND/error. Terminal status is sticky until stop/start/seek.
   On a terminal state transition the output borrow has ended. A violated joint
   contract preventing return reports IO_ERROR but retains READING/ownership;
   successful stop/close is required before storage reuse. No provider close
   or cancellation/retirement of independent backend staging is implied. */
enum kosdec_adx_request_status kosdec_adx_request_step(struct kosdec_adx_request *);
/* Stop releases output borrowing, retains progress for info, and leaves cursor
   unchanged while reading (restart rereads from request start). It does not
   rewind a cursor already advanced by READEND/error. Joint stop returns the
   whole held FREE span, discarding unpublished bytes. No provider I/O. If a
   violated joint ownership contract prevents return, IO_ERROR retains that
   borrow/state: do not free storage. Otherwise idempotent. */
enum kosdec_adx_request_status kosdec_adx_request_stop(struct kosdec_adx_request *);
/* Valid seek stops reading, then clamps signed sector offset from SET/CUR/END
   to[0,sectors], including INT64_MIN/MAX without overflow. Returns STOPPED.
   Invalid origin/state leaves active request intact. Progress remains the last
   request's snapshot; use cursor for current position. No provider seek call. */
enum kosdec_adx_request_status kosdec_adx_request_seek(struct kosdec_adx_request *,
    int64_t offset, enum kosdec_adx_request_origin);
/* Failure leaves output unchanged; caller serializes snapshot with servicing. */
bool kosdec_adx_request_get_info(const struct kosdec_adx_request *,
                               struct kosdec_adx_request_info *);
/* Stop and invalidate. Releases only this object's borrows, never owns/closes
   a descriptor or retires backend staging. Reinit is then permitted. */
enum kosdec_adx_request_status kosdec_adx_request_close(struct kosdec_adx_request *);
#ifdef __cplusplus
}
#endif
#endif
