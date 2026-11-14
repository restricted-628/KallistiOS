/* Original PCM bridge; concurrency contract follows the licensed KOS pipe. */
#ifndef KOSDEC_ADX_PIPE_H
#define KOSDEC_ADX_PIPE_H
#include <kosdec/adx_sequence.h>
#include <kosdec/adx_joint_reader.h>
#include <kosdec/adx_wave.h>
#include <kosdec/adx_au.h>
#include <kosdec/adx_aiff.h>
#include <kosdec/adx_spsd.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_pipe {
    uint32_t magic, capacity, write_cursor, read_cursor;
    uint32_t ready, terminal, cancel_requested, sample_rate, channels, decoded_loops;
    int16_t *pcm;
    struct kosdec_adx_stream *stream;
    struct kosdec_adx_sequence *sequence;
    struct kosdec_adx_joint_reader *joint;
    struct kosdec_adx_wave *wave;
    struct kosdec_adx_au *au;
    struct kosdec_adx_aiff *aiff;
    struct kosdec_adx_spsd *spsd;
};
struct kosdec_adx_pipe_result {
    enum kosdec_adx_status producer; /* MORE until terminal, not last pump status. */
    size_t frames, queued_frames;
    bool drained, starved;
};
/* Initialize before concurrency. Exactly one of stream/sequence must be nonnull,
   initialized and exclusively owned by the producer while bound. Start a sequence
   before pumping. No reset/rebind/reinit of the source while bound. Producer may
   use its existing pause/repeat/enqueue controls BETWEEN pumps. Metadata is fixed
   after first publication. Consumer never touches source state.
   capacity is power-of-two frames32..1048576, samples>=2*capacity even for mono.
   State, source, PCM, provider and output storage must be disjoint and alive;
   PCM/output naturally aligned, ordinary coherent CPU storage, no DMA to ring.
   Invalid init leaves state unchanged. No allocation or ownership transfer. */
enum kosdec_adx_status kosdec_adx_pipe_init(struct kosdec_adx_pipe *,
    struct kosdec_adx_stream *, struct kosdec_adx_sequence *,
    int16_t *pcm, size_t samples, size_t capacity);
/* Alternative joint source. Same PCM ownership/concurrency rules. Joint and
   reader become producer-only; live input must use the reader's feed callback,
   never concurrent application/IRQ publication. Join producer/consumer before
   reader close, then separately retire external feed providers. */
enum kosdec_adx_status kosdec_adx_pipe_init_joint(struct kosdec_adx_pipe *,
    struct kosdec_adx_joint_reader *, int16_t *pcm, size_t samples, size_t capacity);
/* PCM WAV alternative, same producer-only lifetime and SPSC rules. Seek,
   reinit and filter changes require unbinding/join first. No embedded loops. */
enum kosdec_adx_status kosdec_adx_pipe_init_wave(struct kosdec_adx_pipe *,
    struct kosdec_adx_wave *, int16_t *pcm, size_t samples, size_t capacity);
/* AU PCM/mu-law alternative, same producer-only lifetime and SPSC rules. Seek,
   reinit and filter changes require unbinding/join first. No embedded loops. */
enum kosdec_adx_status kosdec_adx_pipe_init_au(struct kosdec_adx_pipe *,
    struct kosdec_adx_au *, int16_t *pcm, size_t samples, size_t capacity);
/* AIFF PCM alternative, same producer-only lifetime and SPSC rules. Seek,
   reinit and filter changes require unbinding/join first. No embedded loops. */
enum kosdec_adx_status kosdec_adx_pipe_init_aiff(struct kosdec_adx_pipe *,
    struct kosdec_adx_aiff *, int16_t *pcm, size_t samples, size_t capacity);
/* SPSD mono PCM16; same source lifetime and output rules. */
enum kosdec_adx_status kosdec_adx_pipe_init_spsd(struct kosdec_adx_pipe *,
    struct kosdec_adx_spsd *, int16_t *pcm, size_t samples, size_t capacity);
/* One producer, one consumer may run concurrently. Pump is producer-only:
   at most one bounded source read, one32-frame decode and two PCM copies.
   Any configured source PCM filter runs on this producer before publication;
   its work contributes to the pump budget. Consumer reads never invoke it.
   Requires at least1 free frame; source output is capped to min(free,32). A
   pending stream seek advances without publishing discarded PCM. NEED_OUTPUT
   means no source advance; NEED_INPUT means retry later. No waiting/spinning.
   May block inside a synchronous source provider. Source terminal is sticky in
   pipe, including BAD_ARGUMENT for a misconfigured borrowed source.
   Pause stops production only; already queued PCM remains readable. */
struct kosdec_adx_stream_result kosdec_adx_pipe_pump(struct kosdec_adx_pipe *);
/* Consumer-only. Copy at most capacity frames in two spans, then release space.
   Reserve capacity*channels samples, or2*capacity before get_format succeeds.
   Zero capacity is a non-consuming status poll; no silence is inserted.
   queued_frames is a snapshot after this copy; producer may subsequently append.
   drained means terminal AND empty software ring, never audio/DMA completion.
   starved means live producer and fewer frames than requested. Errors/cancel
   preserve previously published PCM for the consumer to drain or discard after
   users have stopped. Invalid arguments return BAD_ARGUMENT with no mutation. */
struct kosdec_adx_pipe_result kosdec_adx_pipe_read(
    struct kosdec_adx_pipe *, int16_t *output, size_t capacity);
/* Any thread; false until format published, leaves outputs unchanged on false. */
bool kosdec_adx_pipe_get_format(const struct kosdec_adx_pipe *,
                              uint32_t *sample_rate, unsigned *channels);
/* Any thread: completed source rewinds, modulo2^32. Zero before decoding and
   for sequence/joint/WAV sources (playlist repeats are not embedded ADX loops).
   Published after each producer step, including a rewind followed by input
   WAIT/error. Decode read-ahead may lead playback: not an audible-loop clock.
   Release/pause/cancel/terminal retain the last count; init/rebind resets it.
   Independent atomic snapshot, not a transaction with PCM position/status.
   False on invalid arguments leaves output unchanged. */
bool kosdec_adx_pipe_get_loop_count(const struct kosdec_adx_pipe *, uint32_t *loops);
/* Any thread; caller must wake a parked producer. Next pump acknowledges even
   when full/paused. Does not interrupt an in-progress provider/read, stop a
   consumer, join a worker or retire I/O. Terminal is NOT a producer-return/join
   certificate. Before reclaiming/reinitializing anything, join/stop both users
   and separately retire providers, callbacks and any downstream output. */
void kosdec_adx_pipe_request_cancel(struct kosdec_adx_pipe *);
#ifdef __cplusplus
}
#endif
#endif
