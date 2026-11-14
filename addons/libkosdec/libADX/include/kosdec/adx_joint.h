/* Original caller-owned compressed-byte chunk transport. */
#ifndef KOSDEC_ADX_JOINT_H
#define KOSDEC_ADX_JOINT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum kosdec_adx_joint_kind {
    KOSDEC_ADX_JOINT_MEMORY = 1, KOSDEC_ADX_JOINT_RING
};
enum kosdec_adx_joint_line {
    KOSDEC_ADX_JOINT_FREE, KOSDEC_ADX_JOINT_DATA
};
enum kosdec_adx_joint_status {
    KOSDEC_ADX_JOINT_OK, KOSDEC_ADX_JOINT_BAD_ARGUMENT,
    KOSDEC_ADX_JOINT_BUSY, KOSDEC_ADX_JOINT_STALE,
    KOSDEC_ADX_JOINT_LIMIT
};
struct kosdec_adx_joint;
/* A borrowed span, including its origin. Use split to create partial returns.
   DATA is read-only; only FREE may be written. No access after return to joint. */
struct kosdec_adx_chunk {
    uint8_t *data;
    size_t bytes;
    struct kosdec_adx_joint *owner;
    uint64_t token;
    enum kosdec_adx_joint_line line;
};
typedef void (*kosdec_adx_joint_error)(void *context,
                                      enum kosdec_adx_joint_status status);
struct kosdec_adx_joint {
    uint32_t magic;
    enum kosdec_adx_joint_kind kind;
    uint8_t *buffer;
    size_t capacity, extra, position[2], available[2];
    struct kosdec_adx_chunk borrowed[2];
    uint64_t next_token;
    kosdec_adx_joint_error error;
    void *error_context;
};
struct kosdec_adx_joint_info {
    enum kosdec_adx_joint_kind kind;
    size_t capacity, extra;
    size_t available[2], contiguous[2], borrowed[2];
};
/* State and backing storage are caller-owned, disjoint, and remain alive until
   successful close. Every call and buffer access is serialized by one owner;
   this is not a concurrent SPSC queue. At most one outstanding borrow per line.
   No device/DMA/IRQ ownership, allocation, global init, or scheduler dependency.
   State fields are implementation-owned; do not copy a live joint.

   Init accepts uninitialized state; use only initially or after successful close,
   never to bypass outstanding borrows. Invalid init leaves state unchanged.
   Memory starts entirely DATA, never offers FREE, and discards consumed spans.
   Its backing bytes must stay unchanged until close (reset replays them).
   Ring starts entirely FREE; backing allocation must hold capacity+extra bytes,
   0<=extra<=capacity, capacity+extra<=INT32_MAX. Extra mirrors the ring prefix
   when FREE chunks are published; uncommitted writes are never DATA.
   No padding is initialized or read until the corresponding bytes are published. */
enum kosdec_adx_joint_status kosdec_adx_joint_memory_init(
    struct kosdec_adx_joint *, void *data, size_t bytes);
enum kosdec_adx_joint_status kosdec_adx_joint_ring_init(
    struct kosdec_adx_joint *, void *buffer, size_t capacity, size_t extra);
/* Reset restores initial availability, preserves error hook and token sequence.
   Reset/close return BUSY unchanged while any nonempty borrow exists. Close
   retires only software borrows; callers must join any external provider first. */
enum kosdec_adx_joint_status kosdec_adx_joint_reset(struct kosdec_adx_joint *);
enum kosdec_adx_joint_status kosdec_adx_joint_close(struct kosdec_adx_joint *);
/* Errors are returned and optionally reported synchronously. Callback must not
   reenter/mutate joint, block/yield, retain spans, or alter FPU control modes.
   Hook/context remain borrowed through close; NULL detaches. No sticky errors. */
enum kosdec_adx_joint_status kosdec_adx_joint_set_error(
    struct kosdec_adx_joint *, kosdec_adx_joint_error, void *context);
enum kosdec_adx_joint_status kosdec_adx_joint_query(
    struct kosdec_adx_joint *, struct kosdec_adx_joint_info *);
/* Get borrows min(request, available contiguous bytes). Empty is OK, with a
   zeroed chunk. Another nonzero get on that line while borrowed returns BUSY.
   Empty DATA is temporary starvation, never an implicit EOF. Output unchanged
   on errors. Query contiguous is zero for a line with an outstanding borrow. */
enum kosdec_adx_joint_status kosdec_adx_joint_get(
    struct kosdec_adx_joint *, enum kosdec_adx_joint_line, size_t request,
    struct kosdec_adx_chunk *);
/* Put consumes a prefix of the remaining borrow, publishing it to the opposite
   line (memory DATA is discarded). Unget returns a suffix to its original line.
   Prefix/suffix may be returned in either order. Empty chunks are no-ops.
   Stale/foreign/double/overlapping/out-of-order returns fail unchanged.
   Do not retain copies for later reuse, including across close/reinitialization. */
enum kosdec_adx_joint_status kosdec_adx_joint_put(
    struct kosdec_adx_joint *, const struct kosdec_adx_chunk *);
enum kosdec_adx_joint_status kosdec_adx_joint_unget(
    struct kosdec_adx_joint *, const struct kosdec_adx_chunk *);
/* Pure split: prefix=min(request,bytes). Outputs must be distinct; either may
   alias input. Empty results are zeroed. Does not transfer ownership. */
enum kosdec_adx_joint_status kosdec_adx_chunk_split(
    const struct kosdec_adx_chunk *, size_t prefix,
    struct kosdec_adx_chunk *first, struct kosdec_adx_chunk *last);
#ifdef __cplusplus
}
#endif
#endif
