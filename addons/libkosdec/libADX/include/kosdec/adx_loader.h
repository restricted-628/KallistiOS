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
    uint64_t completed_passes;
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
    size_t capacity, flow_limit, source_flow_limit;
    unsigned head, tail, count;
    enum kosdec_adx_loader_state state;
    enum kosdec_adx_request_status error;
    bool paused, active, sector_policy, lsc_flow, repeat;
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
/* One<=512-byte provider call, at most one request/entry completion.
   WAIT/paused/stopped returns OK without source work. Start a new sector only
   when DATA bytes (including outstanding DATA borrow) <flow_limit by default.
   A zero default-policy limit disables the threshold. An active request finishes
   independently, respecting backpressure. LSC mode may request several sectors.
   NEED_INPUT/OUTPUT must be rescheduled, not spun. Provider may block; use an
   appropriate thread/fiber, never audio IRQ. Empty queue is WAIT, not sealed EOF.
   Error is sticky, with all ownership retained until checked stop/close. */
enum kosdec_adx_loader_status kosdec_adx_loader_step(struct kosdec_adx_loader *);
/* Optional split service for a caller-owned loader. STREAM progresses only an
   already admitted read, with at most one provider call. Published DATA and
   byte counts are immediately visible on the serialized owner. A completed
   request remains current until RELOAD. RELOAD completes at most one entry
   and may admit the next request. It never calls the provider. Idle or paused
   returns OK. Use these calls in place of the combined step for this loader.
   Both phases, the DATA consumer and controls require the same serialized
   owner. A loader bound to a private player producer stays with that producer. */
enum kosdec_adx_loader_status kosdec_adx_loader_step_stream(struct kosdec_adx_loader *);
enum kosdec_adx_loader_status kosdec_adx_loader_step_reload(struct kosdec_adx_loader *);
enum kosdec_adx_loader_status kosdec_adx_loader_pause(struct kosdec_adx_loader *, bool);
/* Rotate completed source descriptions to the queue tail at transport EOF.
   Repeat defaults off. The original source ID remains pending and its context
   stays borrowed across passes. Disabling repeat lets the pending queue drain.
   read_sectors and published_bytes describe the current pass. completed_passes
   counts fully published passes modulo 2^64. Stop preserves repeat policy. */
enum kosdec_adx_loader_status kosdec_adx_loader_set_repeat(struct kosdec_adx_loader *, bool);
/* Bytes0..initial joint capacity; no I/O. Restores immediate byte flow. Changes
   affect new sector admission and an already admitted request can finish. */
enum kosdec_adx_loader_status kosdec_adx_loader_set_flow(struct kosdec_adx_loader *, size_t);
/* Select baseline LSC refill policy while STOPPED with at least one sector of joint
   capacity. Once selected, update the requested byte limit at any time on the
   serialized owner. get_info reports that requested value. Each nonempty
   source/pass captures it on first service before checking refill eligibility.
   QUEUED DATA >= captured limit prevents a new refill, including zero on an
   empty joint. Changing a captured zero cannot resume that source. Checked stop
   preserves the requested policy for the next start. set_flow and the reload
   setters select immediate policies instead.
   Admit up to512 sectors at once with a separate bounded provider step, fitting
   the native joint and exact source extent. Rebuild consumers for loader layout. */
enum kosdec_adx_loader_status kosdec_adx_loader_set_lsc_flow(
    struct kosdec_adx_loader *, size_t);
/* ADX input-buffer policy, opt-in instead of the generic byte-flow policy.
   Same serialized producer owner as step/read/joint operations; configure before
   binding to a player, or from its producer feed. Never query/mutate concurrently
   from the player controller. No allocation, I/O, timer or device operation.
   Capacity must contain 1..32767 whole 2048-byte sectors. Sector threshold is
   0..floor(capacity/2048). Zero blocks NEW sector requests; an active request
   finishes. set_flow restores its legacy policy (zero disables that gate).
   The ADX policy compares QUEUED DATA only, excluding outstanding DATA borrows.
   These setters affect next sector admission, not cancellation or initial
   preloading. Rebuild consumers for the extended implementation-owned state. */
enum kosdec_adx_loader_status kosdec_adx_loader_set_reload_sectors(
    struct kosdec_adx_loader *, uint32_t sectors);
/* Default is trunc(binary32(whole_capacity_sectors)*0.85f). */
enum kosdec_adx_loader_status kosdec_adx_loader_set_reload_default(
    struct kosdec_adx_loader *);
/* seconds finite and >=0, channels1/2, rate1..48000. First round rate*seconds
   to binary32, truncate samples, take whole32-sample groups, multiply by18 bytes
   per channel, then take whole2048-byte sectors and clamp to capacity. Huge
   finite times saturate safely. Sub-sector times can produce a zero threshold.
   Invalid arguments leave state unchanged; exact supplied-format units, not
   automatic metadata selection or an estimate based on output pitch. */
enum kosdec_adx_loader_status kosdec_adx_loader_set_reload_time(
    struct kosdec_adx_loader *, float seconds, unsigned channels, unsigned rate);
struct kosdec_adx_loader_buffer_info {
    size_t queued_bytes, held_bytes, reload_bytes;
    uint32_t sectors;
    float seconds;
    bool sector_policy, safe;
};
/* Queued bytes/2048 rounded down; seconds = float(floor(queued/(18*channels))*32)
   /float(rate). channels1/2 and rate1..48000 must describe the ADX source.
   Counts all queued bytes, including any header/footer: this is a nominal
   compressed-data estimate, not promised playable PCM or an audible clock.
   Held input, decoded PCM and sound RAM are excluded. safe means queued bytes
   >=reload_bytes (including equality and zero). In LSC mode this threshold is
   the current source's captured value once service has selected it. Generic byte
   flow counts held DATA for admission, and zero disables its gate. This snapshot
   is not permission to monopolize the disc/DMA engine.
   Output remains unchanged on invalid/stale joint or format arguments. */
bool kosdec_adx_loader_get_buffer_info(const struct kosdec_adx_loader *,
    unsigned channels, unsigned rate, struct kosdec_adx_loader_buffer_info *);
/* Input-joint identity; NULL for invalid/closed loader. No ownership transfer or
   additional concurrency rights: loader still owns FREE production, and the
   serialized reader owns DATA. Keep both users joined before reset/close. */
struct kosdec_adx_joint *kosdec_adx_loader_get_input_joint(
    const struct kosdec_adx_loader *);
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
