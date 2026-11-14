/* Original serialized loading controller over borrowed providers and a joint. */
#ifndef KOSDEC_ADX_LOADER_H
#define KOSDEC_ADX_LOADER_H
#include <kosdec/adx_request.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_ADX_LOADER_SOURCES 16u
#define KOSDEC_ADX_LOADER_NAME_BYTES 40u
enum kosdec_adx_loader_state { KOSDEC_ADX_LOADER_STOPPED, KOSDEC_ADX_LOADER_WAIT,
    KOSDEC_ADX_LOADER_EXEC, KOSDEC_ADX_LOADER_FAILED };
enum kosdec_adx_loader_status { KOSDEC_ADX_LOADER_OK, KOSDEC_ADX_LOADER_NEED_INPUT,
    KOSDEC_ADX_LOADER_NEED_OUTPUT, KOSDEC_ADX_LOADER_BAD_ARGUMENT,
    KOSDEC_ADX_LOADER_LIMIT, KOSDEC_ADX_LOADER_BUSY, KOSDEC_ADX_LOADER_IO_ERROR };
enum kosdec_adx_loader_source_state { KOSDEC_ADX_LOADER_QUEUED,
    KOSDEC_ADX_LOADER_READING, KOSDEC_ADX_LOADER_END, KOSDEC_ADX_LOADER_CANCELLED };
struct kosdec_adx_loader_source_info {
    uint64_t id;
    char name[KOSDEC_ADX_LOADER_NAME_BYTES];
    uint32_t start_sector, sectors, data_bytes, read_sectors, published_bytes;
    enum kosdec_adx_loader_source_state state;
};
struct kosdec_adx_loader_entry {
    struct kosdec_adx_loader_source_info info;
    kosdec_adx_read_at read_at;
    void *context;
    uint32_t input_bytes, completed_sectors, completed_bytes;
};
typedef void (*kosdec_adx_loader_error)(void *, uint64_t source_id,
                                       enum kosdec_adx_request_status);
struct kosdec_adx_loader {
    uint32_t magic;
    struct kosdec_adx_joint *joint;
    struct kosdec_adx_request request;
    struct kosdec_adx_loader_entry entries[KOSDEC_ADX_LOADER_SOURCES];
    uint64_t next_id;
    size_t capacity, flow_limit;
    unsigned head, tail, count;
    enum kosdec_adx_loader_state state;
    enum kosdec_adx_request_status error;
    bool paused, active;
    kosdec_adx_loader_error error_fn;
    void *error_context;
};
struct kosdec_adx_loader_info {
    enum kosdec_adx_loader_state state;
    enum kosdec_adx_request_status error;
    unsigned pending;
    size_t flow_limit;
    bool paused;
};
/* Initially/after successful close only. Caller owns disjoint state, joint,
   backing and provider storage. RING/QUEUE, unsealed, no FREE borrow. Existing
   DATA is preserved. No allocation, I/O, global registry or hidden worker.
   All controller/joint operations and DATA consumption are serialized by one
   producer owner. No other FREE producer, reset/seal/reinit/capacity mutation
   while live. Provider retirement remains independent of CPU source ownership.
   Default flow limit=floor(capacity*8/10) bytes. Invalid init leaves state intact. */
enum kosdec_adx_loader_status kosdec_adx_loader_init(struct kosdec_adx_loader *,
                                                   struct kosdec_adx_joint *);
/* Copy a label of0..39 bytes plus NUL and borrow an immutable fixed-extent source.
   No filename open: resolve file/AFS/disc providers separately. start/count are
   2048-byte sectors, clipped to EOF, exact bytes published without padding.
   At most16 pending sources. IDs are nonzero64-bit, not reused within lifetime;
   full/exhausted queue leaves state/id output intact. Providers/context must
   survive until sourceEND or successful stop, plus own backend retirement.
   Labels/source/id output/state/backing are disjoint. Invalid args unchanged. */
enum kosdec_adx_loader_status kosdec_adx_loader_enqueue(struct kosdec_adx_loader *,
    const char *name, kosdec_adx_read_at, void *context, uint32_t input_bytes,
    uint32_t start_sector, uint32_t sectors, uint64_t *id);
/* Start only from STOPPED; empty queue enters WAIT, new entry resumes EXEC.
   Existing running state returns BUSY; no implicit destructive restart. */
enum kosdec_adx_loader_status kosdec_adx_loader_start(struct kosdec_adx_loader *);
/* One<=512-byte provider call, at most one sector request/entry completion.
   WAIT/paused/stopped returns OK without source work. Start a new sector only
   when DATA bytes (including outstanding DATA borrow) <flow_limit;0 disables
   that threshold. Active sector finishes independently, respecting backpressure.
   NEED_INPUT/OUTPUT must be rescheduled, not spun. Provider may block; use an
   appropriate thread/fiber, never audio IRQ. Empty queue is WAIT, not sealed EOF.
   Error is sticky, with all ownership retained until checked stop/close. */
enum kosdec_adx_loader_status kosdec_adx_loader_step(struct kosdec_adx_loader *);
enum kosdec_adx_loader_status kosdec_adx_loader_pause(struct kosdec_adx_loader *, bool);
/* Bytes0..initial joint capacity; no I/O. Changes affect new sector admission. */
enum kosdec_adx_loader_status kosdec_adx_loader_set_flow(struct kosdec_adx_loader *, size_t);
/* Snapshot helpers leave output unchanged on failure. Entry metadata persists
   after END/CANCELLED until its slot is reused. ordinal addresses pending order. */
bool kosdec_adx_loader_get_info(const struct kosdec_adx_loader *, struct kosdec_adx_loader_info *);
bool kosdec_adx_loader_get_source(const struct kosdec_adx_loader *, uint64_t,
                                 struct kosdec_adx_loader_source_info *);
bool kosdec_adx_loader_get_id(const struct kosdec_adx_loader *, unsigned ordinal, uint64_t *);
/* Serialized setup; callback runs once on first service failure, after latching
   FAILED. It may log only, never reenter or mutate controller/joint/provider. */
enum kosdec_adx_loader_status kosdec_adx_loader_set_error(struct kosdec_adx_loader *,
    kosdec_adx_loader_error, void *);
/* Return held FREE span before clearing pending queue; preserve publishedDATA.
   Successful stop cancels pending sources, clears error/pause, keeps ID monotonic.
   Failed stop retains ownership and may be retried; never free on that result.
   Neither stop nor close closes providers or certifies DMA/device retirement.
   Close stops then invalidates; after successful close caller may seal/reset joint. */
enum kosdec_adx_loader_status kosdec_adx_loader_stop(struct kosdec_adx_loader *);
enum kosdec_adx_loader_status kosdec_adx_loader_close(struct kosdec_adx_loader *);
#ifdef __cplusplus
}
#endif
#endif
