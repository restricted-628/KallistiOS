/* Original forward-only joint-to-PCM reader. */
#ifndef KOSDEC_ADX_JOINT_READER_H
#define KOSDEC_ADX_JOINT_READER_H
#include <kosdec/adx.h>
#include <kosdec/adx_joint.h>
#include <kosdec/adx_request.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_loader;
enum kosdec_adx_joint_feed_status {
    KOSDEC_ADX_JOINT_FEED_MORE, KOSDEC_ADX_JOINT_FEED_WAIT,
    KOSDEC_ADX_JOINT_FEED_END, KOSDEC_ADX_JOINT_FEED_ERROR
};
/* Optional input step on the serialized reader/pipe producer, before decoding.
   May supply bytes via joint API; END requests checked joint_finish. WAIT may
   still leave queued DATA readable. No reentry into reader or consumer/audio
   APIs. At most one call per nonterminal read. With no output capacity,
   only a linked reader advances the feed.
   Bounded nonblocking steps are required on a shared executor; a synchronous
   blocking step contributes to read/pump latency. External DMA/IRQ callbacks
   must stage/wake, never mutate the joint concurrently. Context and staging stay
   borrowed until producer join and separate provider retirement. Cancellation
   stops future calls but does not cancel/join that external provider. */
typedef enum kosdec_adx_joint_feed_status (*kosdec_adx_joint_feed)(
    void *context, struct kosdec_adx_joint *);
struct kosdec_adx_joint_reader {
    uint32_t magic;
    struct kosdec_adx_decoder decoder;
    struct kosdec_adx_joint *joint;
    struct kosdec_adx_chunk held;
    size_t used;
    uint64_t consumed_bytes;
    enum kosdec_adx_status terminal;
    enum kosdec_adx_joint_status joint_error;
    kosdec_adx_joint_feed feed;
    void *feed_context;
    bool sfa_live, sfa_length_known;
    uint32_t sfa_frames;
    struct {
        struct kosdec_adx_loader *loader;
        struct kosdec_adx_chunk next;
        uint64_t completed_records;
        size_t scan_offset;
        uint32_t sample_rate;
        unsigned channels;
        bool enabled, follow, released, seal_when_empty, registration_closed;
        bool scan, scan_active, scan_second, header_selected;
    } linked;
    struct {
        int16_t *pcm;
        unsigned maximum, extra;
        uint32_t first_frame, wraps;
        bool repeat, trap;
    } job;
    struct {
        struct kosdec_adx_request request;
        struct kosdec_adx_chunk held;
        uint32_t eof_sector, padding, sectors_per_read;
        bool configured;
    } sector;
};
struct kosdec_adx_joint_read_result {
    enum kosdec_adx_status status;
    enum kosdec_adx_joint_status joint_status;
    size_t frames;
};
/* Initializes unused/closed caller state, requiring no current DATA borrow.
   Reader/state/output and joint/workspace/input must be disjoint. Caller
   serializes producer operations and every reader call on one owner; no ISR,
   concurrent SPSC or implicit fiber. Keep joint/input alive through close.
   Reader owns DATA acquisition until closed; producer only supplies other lines.
   Joint must begin at an ADX header. Job/linked configuration is separate.
   Metadata can be queried from reader.decoder after header_ready; fields remain
   implementation-owned. Reinitialization cannot replace close. */
enum kosdec_adx_joint_status kosdec_adx_joint_reader_init(
    struct kosdec_adx_joint_reader *, struct kosdec_adx_joint *);
enum kosdec_adx_status kosdec_adx_joint_reader_set_filter(
    struct kosdec_adx_joint_reader *, kosdec_adx_pcm_filter, void *context);
/* Configure only while unbound and serialized; NULL detaches. */
enum kosdec_adx_status kosdec_adx_joint_reader_set_feed(
    struct kosdec_adx_joint_reader *, kosdec_adx_joint_feed, void *context);
/* Before reading: accept adjacent, complete same-format ADX records with no
   embedded loop. Both strict and job reads validate the full footer and reset
   predictor history at each record. Link mode can advance the producer feed
   while PCM is full. Incompatible with live SFA and the direct sector source. */
enum kosdec_adx_status kosdec_adx_joint_reader_enable_linked(struct kosdec_adx_joint_reader *);
/* Optional baseline buffered-header policy on an enabled linked reader. At the
   validated footer, snapshot up to two contiguous DATA loans. Look for the
   next header at halfword offsets within each span, advancing at most 512
   bytes per scan step. No feed runs while scanning. If no full header extent
   was buffered, stop following and leave the post-footer DATA readable.
   The selected record still undergoes strict header/format validation.
   Call before starting reads or binding to a service. */
enum kosdec_adx_status kosdec_adx_joint_reader_enable_link_scan(struct kosdec_adx_joint_reader *);
/* Bind the serialized loader as the exclusive feed of a fresh linked reader.
   The loader and reader must share an unsealed joint. Start/pause/repeat/stop
   the loader with its own API. If seal_when_empty is true, reaching loader
   WAIT seals the joint after all pending input is published. With false, WAIT
   allows another enqueue. To end a manually driven stream, join the producer,
   stop/close the loader, then seal the joint. Reader close never retires the
   loader or its providers. Keep them alive through checked producer join. */
enum kosdec_adx_status kosdec_adx_joint_reader_set_loader(struct kosdec_adx_joint_reader *,
    struct kosdec_adx_loader *, bool seal_when_empty);
/* Change following before the next admission or terminal cutoff. This does
   not resume a released or terminal reader, change loader policy or retire
   loans. A selected next header still finishes its record. Producer only. */
enum kosdec_adx_status kosdec_adx_joint_reader_set_link_follow(
    struct kosdec_adx_joint_reader *, bool);
/* Finish the current record or stop at the existing record boundary.
   This does not change loader repeat policy or retire already prefetched DATA. */
enum kosdec_adx_status kosdec_adx_joint_reader_release_link(struct kosdec_adx_joint_reader *);
/* Select baseline-style decode jobs before the first read or producer start.
   Supports memory, mirrored rings with at least 35 extra bytes, and queued
   input. Queue jobs use one head per call and require whole compressed groups
   within that head. A short head waits for a contiguous JOIN append or reports
   TRUNCATED after seal. A partial queuing return can require a spare node.
   Repeat is available for memory. The first call must start at the
   header. max_frames is 32..8192. pcm holds at least 2*max_frames aligned
   samples and is exclusively borrowed until close, disjoint from source,
   output and pipe storage. pipe_extra_frames (0..31) sets the wrap extent
   used for job admission on a bound pipe. Allow 31 for arbitrary starts.
   DONE means the mode was selected. Rejected configuration changes nothing.
   The playback mode accepts an end marker or the declared sample limit as
   the end of playback. A paired live SFA writer keeps the late length,
   footer and sealed-EOF checks. Use the ordinary reader for complete-record
   checks on other inputs. */
enum kosdec_adx_status kosdec_adx_joint_reader_enable_jobs(
    struct kosdec_adx_joint_reader *, int16_t *pcm, size_t samples,
    unsigned max_frames, unsigned pipe_extra_frames, bool repeat);
/* Attach a borrowed sector-addressed source to a fresh job-enabled ring
   reader. Cannot be combined with external feed or a live SFA writer.
   Ring must be empty with no loans,
   sector-aligned cursors and a capacity of at least two whole sectors.
   Read batch is in 2048-byte sectors
   and at most the ring capacity. Use a record-relative provider and a
   declared byte extent. The v3 loop start offset is rounded down to the
   sector containing the corresponding first compressed group. Metadata
   retains the unrounded offset. A nonaligned logical start is supported
   when the logical interval consumes the entire encoded loop span.
   Completed sector requests publish atomically; the final sector is zero
   padded to its end. At most one <=512-byte provider call per producer step,
   including with zero PCM capacity. Keep provider storage alive through
   close and separately retire any external transport resources. */
enum kosdec_adx_status kosdec_adx_joint_reader_set_sector_source(
    struct kosdec_adx_joint_reader *, kosdec_adx_read_at, void *context,
    uint32_t bytes, uint32_t sectors_per_read, bool repeat);
/* Producer-only request. Memory releases at the active sample trap.
   Sector-backed input releases at its next transport end-of-range event;
   previously queued loop data keeps its FIFO order. */
enum kosdec_adx_status kosdec_adx_joint_reader_release_loop(
    struct kosdec_adx_joint_reader *);
/* One existing decoder call: <=32 PCM frames or512 metadata bytes. Output
   interleaved PCM16 with naturally aligned space for output_frames*channels
   (reserve two channels until metadata is known). Zero capacity may advance
   an attached sector provider or linked feed.
   Output must fit a logical group as required by kosdec_adx_decode.
   At most one feed callback and chunk admission/return; no hidden copies/yields.
   Retains the whole borrow until consumed; queues in either mode can retain a
   large head across many bounded calls. Whole-head admission needs no spare
   node. Joint failures map to sticky IO_ERROR with the original joint status.
   Empty unsealed DATA is starvation (NEED_INPUT/NEED_END); only joint_finish
   permits final_input. DONE validates declared samples/footer and sealed EOF,
   not output playback. Consumed count excludes discarded close/cancel input.
   Codec terminals are sticky; PCM filter follows the standalone decoder rules.
   In job mode, capacity must be at least32. One job may return up to the
   configured service budget. Input and PCM are committed independently. */
struct kosdec_adx_joint_read_result kosdec_adx_joint_reader_read(
    struct kosdec_adx_joint_reader *, int16_t *output, size_t output_frames);
/* Cancel stops decoding and provider calls, retaining any borrow until
   checked close. Close returns an unfinished sector write to FREE. Close may
   also abandon an active reader: returns its entire held chunk, discarding any
   unread tail. Never resets/frees/seals joint or joins producer/DMA. Success
   clears reader; failure retains state/borrow for retry. Joint must be reset or
   reseeded at a fresh header before starting a new independent ADX file. */
void kosdec_adx_joint_reader_cancel(struct kosdec_adx_joint_reader *);
enum kosdec_adx_joint_status kosdec_adx_joint_reader_close(
    struct kosdec_adx_joint_reader *);
#ifdef __cplusplus
}
#endif
#endif
