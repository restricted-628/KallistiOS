/* Original resident AFS directory and caller-owned partition registry. */
#ifndef KOSDEC_ADX_PARTITION_H
#define KOSDEC_ADX_PARTITION_H
#include <kosdec/adx_index.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_ADX_PARTITION_IDS 256u
#define KOSDEC_ADX_PARTITION_FILES 65536u
struct kosdec_adx_range { uint32_t offset, bytes; };
struct kosdec_adx_partition {
    uint32_t magic, input_bytes, count, loaded, registrations;
    size_t capacity;
    kosdec_adx_read_at read_at;
    void *context;
    struct kosdec_adx_range *entries;
    enum kosdec_adx_status terminal;
    unsigned used;
    bool header_ready;
    uint8_t scratch[512];
};
struct kosdec_adx_partition_info {
    uint32_t input_bytes, count, loaded, registrations;
    size_t work_bytes;
    bool header_ready;
    enum kosdec_adx_status status;
};
enum kosdec_adx_partition_status {
    KOSDEC_ADX_PARTITION_OK, KOSDEC_ADX_PARTITION_BAD_ARGUMENT,
    KOSDEC_ADX_PARTITION_BUSY, KOSDEC_ADX_PARTITION_NOT_READY,
    KOSDEC_ADX_PARTITION_NOT_FOUND, KOSDEC_ADX_PARTITION_LIMIT
};
struct kosdec_adx_partition_registry {
    uint32_t magic;
    struct kosdec_adx_partition *slots[KOSDEC_ADX_PARTITION_IDS];
};
/* All calls on a partition and its registries are externally serialized.
   No allocation, locks, global state, scheduler or implicit file/driver calls.
   Typed state/workspace/input/output objects are disjoint; do not copy live state.
   Work bytes for count0..65536. Returns0 for zero or unsupported counts. */
size_t kosdec_adx_partition_work_size(uint32_t count);
/* Initially or after successful close only. Invalid arguments leave state intact.
   entries holds capacity typed ranges, capacity0..65536; NULL only with capacity0.
   Callback uses existing synchronous read_at rules (no retained destination,
   reentry or mutation). Source/context remain alive through loading/cancel/close;
   callback may block, so use an appropriate owner thread/fiber, never audio IRQ.
   AFS-NUL only. At most65536 members; no optional names or append-overlay format.
   MORE initializes, TRUNCATED initializes a terminal state when input_bytes<8.
   Workspace is private/partially written until DONE; preserve through close.
   Capacity0 probes required sizing: after a valid nonempty header, step returns
   sticky NEED_OUTPUT. Read info, close, then reinitialize with enough storage. */
enum kosdec_adx_status kosdec_adx_partition_init(struct kosdec_adx_partition *,
    kosdec_adx_read_at, void *context, uint32_t input_bytes,
    struct kosdec_adx_range *entries, size_t capacity);
/* One provider call<=512 bytes and at most64 entries per step. Positive short
   reads return MORE; WAIT returns NEED_INPUT without losing progress. DONE only
   after every range fits the source and every nonempty range follows the table.
   Empty/overlapping immutable ranges are retained; payloads are not validated.
   All errors, DONE and insufficient-capacity NEED_OUTPUT are sticky. No implicit
   provider cancellation: callback destinations are retired when each call returns. */
enum kosdec_adx_status kosdec_adx_partition_step(struct kosdec_adx_partition *);
void kosdec_adx_partition_cancel(struct kosdec_adx_partition *);
/* Snapshots may be read by the serialized owner while loading. count/work_bytes
   are valid only with header_ready; loaded entries remain private until DONE.
   No partial range is exposed by get_range/open_view/registry binding. */
bool kosdec_adx_partition_get_info(const struct kosdec_adx_partition *,
                                 struct kosdec_adx_partition_info *);
enum kosdec_adx_partition_status kosdec_adx_partition_get_range(
    const struct kosdec_adx_partition *, uint32_t file_id, struct kosdec_adx_range *);
/* Copy a selected range into an independent input view. Explicit source callback
   MUST address the same immutable archive used to load the directory. Provider
   lifetime/serialization continues through all consumers, independently of this
   partition. View does not borrow directory/workspace and survives its close.
   No descriptor open, sector rounding, filename lookup or transport retirement. */
enum kosdec_adx_partition_status kosdec_adx_partition_open_view(
    const struct kosdec_adx_partition *, uint32_t file_id, kosdec_adx_read_at,
    void *context, struct kosdec_adx_view *);
/* BUSY while registered anywhere, unchanged. Otherwise invalidate state, release
   workspace/callback borrowing, never free caller memory or close a provider. */
enum kosdec_adx_partition_status kosdec_adx_partition_close(struct kosdec_adx_partition *);
/* Init only initially/after close. Registry and each bound directory/workspace
   remain alive until unbound/closed. Bind accepts DONE only, replaces an occupied
   ID transactionally, and counts aliases (including across registries). This is
   ordinary registration/replacement, NOT ADXF_AddPartition overlay merging. */
void kosdec_adx_partition_registry_init(struct kosdec_adx_partition_registry *);
enum kosdec_adx_partition_status kosdec_adx_partition_bind(
    struct kosdec_adx_partition_registry *, unsigned id, struct kosdec_adx_partition *);
enum kosdec_adx_partition_status kosdec_adx_partition_unbind(
    struct kosdec_adx_partition_registry *, unsigned id);
/* Returns borrowed metadata pointer valid until next registry mutation/close;
   make a copied range/view for longer use. Failure leaves output unchanged. */
enum kosdec_adx_partition_status kosdec_adx_partition_lookup(
    const struct kosdec_adx_partition_registry *, unsigned id,
    const struct kosdec_adx_partition **);
/* Release every registration and invalidate registry; does not close directories
   or any payload source. Other registries may still keep directories BUSY. */
enum kosdec_adx_partition_status kosdec_adx_partition_registry_close(
    struct kosdec_adx_partition_registry *);
#ifdef __cplusplus
}
#endif
#endif
