/* Original forward-only joint-to-PCM reader. */
#ifndef KOSDEC_ADX_JOINT_READER_H
#define KOSDEC_ADX_JOINT_READER_H
#include <kosdec/adx.h>
#include <kosdec/adx_joint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum kosdec_adx_joint_feed_status {
    KOSDEC_ADX_JOINT_FEED_MORE, KOSDEC_ADX_JOINT_FEED_WAIT,
    KOSDEC_ADX_JOINT_FEED_END, KOSDEC_ADX_JOINT_FEED_ERROR
};
/* Optional input step on the serialized reader/pipe producer, before decoding.
   May supply bytes via joint API; END requests checked joint_finish. WAIT may
   still leave queued DATA readable. No reentry into reader or consumer/audio
   APIs. At most one call per nonterminal read with nonzero output capacity.
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
   Joint must begin at an ADX header. No looping, seeking or player binding here.
   Metadata can be queried from reader.decoder after header_ready; fields remain
   implementation-owned. Reinitialization cannot replace close. */
enum kosdec_adx_joint_status kosdec_adx_joint_reader_init(
    struct kosdec_adx_joint_reader *, struct kosdec_adx_joint *);
enum kosdec_adx_status kosdec_adx_joint_reader_set_filter(
    struct kosdec_adx_joint_reader *, kosdec_adx_pcm_filter, void *context);
/* Configure only while unbound and serialized; NULL detaches. */
enum kosdec_adx_status kosdec_adx_joint_reader_set_feed(
    struct kosdec_adx_joint_reader *, kosdec_adx_joint_feed, void *context);
/* One existing decoder call: <=32 PCM frames or512 metadata bytes. Output
   interleaved PCM16 with naturally aligned space for output_frames*channels
   (reserve two channels until metadata is known). Zero capacity does no work.
   Output must fit a logical group as required by kosdec_adx_decode.
   At most one feed callback and chunk admission/return; no hidden copies/yields.
   Retains the whole borrow until consumed; queues in either mode can retain a
   large head across many bounded calls. Whole-head admission needs no spare
   node. Joint failures map to sticky IO_ERROR with the original joint status.
   Empty unsealed DATA is starvation (NEED_INPUT/NEED_END); only joint_finish
   permits final_input. DONE validates declared samples/footer and sealed EOF,
   not output playback. Consumed count excludes discarded close/cancel input.
   Codec terminals are sticky; PCM filter follows the standalone decoder rules. */
struct kosdec_adx_joint_read_result kosdec_adx_joint_reader_read(
    struct kosdec_adx_joint_reader *, int16_t *output, size_t output_frames);
/* Cancel stops decoding, retaining any borrow until checked close. Close may
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
