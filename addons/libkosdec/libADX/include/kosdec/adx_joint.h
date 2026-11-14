/* Original caller-owned compressed-byte chunk transport. */
#ifndef KOSDEC_ADX_JOINT_H
#define KOSDEC_ADX_JOINT_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

enum kosdec_adx_joint_kind {
    KOSDEC_ADX_JOINT_MEMORY = 1, KOSDEC_ADX_JOINT_RING, KOSDEC_ADX_JOINT_QUEUE
};
enum kosdec_adx_joint_line {
    KOSDEC_ADX_JOINT_FREE, KOSDEC_ADX_JOINT_DATA,
    KOSDEC_ADX_JOINT_AUX2, KOSDEC_ADX_JOINT_AUX3
};
enum kosdec_adx_joint_status {
    KOSDEC_ADX_JOINT_OK, KOSDEC_ADX_JOINT_BAD_ARGUMENT,
    KOSDEC_ADX_JOINT_BUSY, KOSDEC_ADX_JOINT_STALE,
    KOSDEC_ADX_JOINT_LIMIT, KOSDEC_ADX_JOINT_SEALED
};
struct kosdec_adx_joint;
/* A borrowed span, including its origin. Use split to create partial returns.
   DATA is read-only; FREE may be written. Queue auxiliary line access roles
   are caller-defined. No access after return to joint. */
struct kosdec_adx_chunk {
    uint8_t *data;
    size_t bytes;
    struct kosdec_adx_joint *owner;
    uint64_t token;
    enum kosdec_adx_joint_line line;
};
typedef void (*kosdec_adx_joint_error)(void *context,
                                      enum kosdec_adx_joint_status status);
enum kosdec_adx_joint_mode {
    KOSDEC_ADX_JOINT_SEPARATE, KOSDEC_ADX_JOINT_JOIN
};
/* Caller-provided typed workspace; contents are private while joint is live. */
struct kosdec_adx_joint_node {
    struct kosdec_adx_joint_node *next;
    uint8_t *data;
    size_t bytes;
};
struct kosdec_adx_joint {
    uint32_t magic;
    enum kosdec_adx_joint_kind kind;
    uint8_t *buffer;
    size_t capacity, extra, position[4], available[4];
    struct kosdec_adx_chunk borrowed[4];
    uint64_t next_token;
    kosdec_adx_joint_error error;
    void *error_context;
    enum kosdec_adx_joint_mode mode;
    struct kosdec_adx_joint_node *nodes, *spare, *head[4], *tail[4], *reserved[4];
    size_t node_count, spare_count, chunks[4];
    bool sealed;
};
struct kosdec_adx_joint_info {
    enum kosdec_adx_joint_kind kind;
    size_t capacity, extra;
    size_t available[4], contiguous[4], borrowed[4];
    size_t chunks[4], spare_nodes, total_nodes;
    enum kosdec_adx_joint_mode mode;
    bool sealed;
};
/* State, workspace, backing storage and output descriptors are disjoint,
   caller-owned, and remain alive until
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
/* Append zero-filled encoding3/block18/4-bit groups to a RING input joint.
   This is the baseline compressed "silence" operation: it does not clear ADX
   predictor history, so transient decoded PCM may be nonzero. Caller must insert
   at a compressed group boundary after the header, before the endcode, and keep
   the declared total/loop offsets consistent. No header rewrite or SFD demux.
   channels1/2; requested PCM frames are rounded down to a multiple of32. At most
   512 compressed bytes are written per call; inserted receives actual frames.
   Insufficient space for a whole group is OK with zero inserted. Ring wrap and
   mirror maintenance preserve whole-group publication, including with extra0.
   Same serialized producer ownership as get/put; an outstanding DATA borrow is
   allowed. FREE borrow returns BUSY, sealed returns SEALED, non-ring/invalid
   arguments return BAD_ARGUMENT, all without mutation or changing inserted.
   inserted must be disjoint from state/backing storage. No allocation, callbacks
   other than the existing error hook, codec history changes or hardware access. */
enum kosdec_adx_joint_status kosdec_adx_joint_insert_silence(
    struct kosdec_adx_joint *, unsigned channels, uint32_t requested_frames,
    uint32_t *inserted);
/* Queue starts empty, with four independent FIFO lines and caller-owned nodes.
   SEPARATE get returns a whole head chunk only when request>=head bytes; JOIN
   permits partial gets and merges adjacent spans on Put/Unget/seed. Adjacent
   spans in JOIN mode MUST belong to the same original backing allocation.
   No copy occurs. All ranges remain caller-owned through reset/close. Query
   capacity is total seeded bytes, contiguous is head length, and node counts
   exclude borrowed reserved nodes. A partial get/return can require a spare
   node: LIMIT leaves ownership/state unchanged. Each borrow reserves a node,
   guaranteeing its full unget even after other admissions exhaust the pool.
   Reset empties all lines, forgets seeded ranges and restores the node pool.
   Rebuild consumers when the public state structure changes. */
enum kosdec_adx_joint_status kosdec_adx_joint_queue_init(
    struct kosdec_adx_joint *, struct kosdec_adx_joint_node *, size_t node_count,
    enum kosdec_adx_joint_mode);
/* Seed appends a new, disjoint byte range to a queue line. Overlap with any
   queued/borrowed range, state or node workspace is rejected. Total seeded
   bytes<=INT32_MAX. Zero bytes is a no-op. Cannot seed memory/ring joints. */
enum kosdec_adx_joint_status kosdec_adx_joint_seed(
    struct kosdec_adx_joint *, enum kosdec_adx_joint_line, void *, size_t bytes);
/* Queue-only Put with an explicit destination line (may equal source line).
   Ordinary put maps FREE<->DATA and AUX2<->AUX3. */
enum kosdec_adx_joint_status kosdec_adx_joint_move(
    struct kosdec_adx_joint *, enum kosdec_adx_joint_line destination,
    const struct kosdec_adx_chunk *);
/* Queue-only: return a borrowed prefix to caller ownership, without enqueueing.
   It no longer contributes to capacity or overlap checks. No payload is freed;
   caller may reuse/reseed it or move it to another joint. */
enum kosdec_adx_joint_status kosdec_adx_joint_detach(
    struct kosdec_adx_joint *, const struct kosdec_adx_chunk *);
/* Mark DATA input final. Caller must first join external writers. BUSY while
   any FREE/auxiliary borrow remains; a DATA borrow may remain with its reader.
   Until reset, new seeds, nonempty DATA publication and non-DATA gets fail
   SEALED. DATA consumption/unget and queue detach still work. Idempotent;
   no waiting/transport action. Sealed plus no queued/borrowed DATA means EOF,
   not successful codec validation or audible completion. */
enum kosdec_adx_joint_status kosdec_adx_joint_finish(struct kosdec_adx_joint *);
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
/* Get on memory/ring borrows min(request, available contiguous bytes).
   Empty is OK, with a zeroed chunk. Another nonzero get on that line while borrowed returns BUSY.
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
