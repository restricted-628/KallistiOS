/* Original ordered ADX PCM service, independent of output devices. */
#ifndef KOSDEC_ADX_SEQUENCE_H
#define KOSDEC_ADX_SEQUENCE_H
#include <kosdec/adx_stream.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_ADX_SEQUENCE_CAPACITY 16u
struct kosdec_adx_source {
    kosdec_adx_read_at read_at;
    void *context;
    uint32_t bytes, tag;
};
struct kosdec_adx_sequence {
    uint32_t magic, sample_rate;
    unsigned head, count, channels;
    bool started, active, admitted, format_ready, repeat, paused;
    enum kosdec_adx_status terminal;
    uint64_t returned_frames, completed_entries, requeues;
    struct kosdec_adx_source queue[KOSDEC_ADX_SEQUENCE_CAPACITY];
    struct kosdec_adx_stream reader;
    kosdec_adx_pcm_filter filter;
    void *filter_context;
};
struct kosdec_adx_sequence_result {
    enum kosdec_adx_status status;
    size_t frames;
    uint32_t tag, source_frame; /* Identity/first frame of this call's PCM. */
};
/* Caller owns disjoint state/provider/output storage and serializes all calls.
   Sources are immutable, fixed-extent inputs; descriptions are copied, contexts
   borrowed. Keep each context alive through every queued/active/repeated use.
   Fields are inspectable but implementation-owned. No allocation, scheduler,
   transport close/cancel or audio-device calls. Reinit drops descriptions only;
   it does not retire any outstanding I/O owned by their providers. */
void kosdec_adx_sequence_init(struct kosdec_adx_sequence *, bool repeat);
/* Same callback/lifetime contract as decoder set_filter; applies to every
   emitted span across all entries/repeats. NULL detaches; init clears it.
   DONE on success, BAD_ARGUMENT for invalid/completed state, sticky error otherwise.
   Configure under serialized ownership before binding or after producer join. */
enum kosdec_adx_status kosdec_adx_sequence_set_filter(struct kosdec_adx_sequence *,
    kosdec_adx_pcm_filter, void *context);
/* MORE: queued. NEED_OUTPUT: all16 slots occupied, no mutation. A duplicate
   descriptor is legal. Enqueue during playback is allowed until a terminal.
   Descriptors require non-null read_at and nonzero bytes. */
enum kosdec_adx_status kosdec_adx_sequence_enqueue(
    struct kosdec_adx_sequence *, const struct kosdec_adx_source *);
/* Explicit start; no source calls. Empty queue returns NEED_INPUT, stays unstarted.
   A second start is BAD_ARGUMENT; terminals remain sticky. */
enum kosdec_adx_status kosdec_adx_sequence_start(struct kosdec_adx_sequence *);
void kosdec_adx_sequence_pause(struct kosdec_adx_sequence *, bool paused);
/* Repeat appends each completed entry to the queue tail. Disabling it leaves
   already queued entries to finish. Requeue happens at validated decoded EOF,
   not compressed-read completion or hardware playback completion. */
void kosdec_adx_sequence_repeat(struct kosdec_adx_sequence *, bool repeat);
void kosdec_adx_sequence_cancel(struct kosdec_adx_sequence *);
/* At most one stream step: one provider call<=512 bytes and one32-frame group
   or512 metadata bytes. Never crosses entries in one call. MORE/0 is possible
   at headers/footers/transitions; caller buffers/schedules to hide those waits.
   No artificial silence or final-group padding is returned. Every entry starts
   with zero predictor history, including duplicates/repeats. Embedded loops
   and changes from the first entry's rate/channel count are UNSUPPORTED before
   that entry emits PCM. Other profile restrictions are the stream's.
   Output is interleaved PCM16; reserve capacity*channels elements (max2channels).
   Nonempty output must be naturally aligned. Before start is BAD_ARGUMENT;
   paused returns MORE/0, zero capacity NEED_OUTPUT/0, all without I/O. Bad read
   arguments do not mutate state. DONE means all queued footers validated, not
   output consumed or provider/DMA retirement. Errors and cancel are sticky.
   Counters wrap modulo2^64 and count returned PCM, validated entries/requeues.
   count includes the active entry. Format is valid only after format_ready.
   tag is caller metadata; duplicates need not have distinct tags. */
struct kosdec_adx_sequence_result kosdec_adx_sequence_read(
    struct kosdec_adx_sequence *, int16_t *output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
