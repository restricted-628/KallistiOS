/* Original bounded compressed-input reader, independent of any transport. */
#ifndef KOSDEC_ADX_STREAM_H
#define KOSDEC_ADX_STREAM_H
#include <kosdec/adx.h>
#ifdef __cplusplus
extern "C" {
#endif
enum kosdec_adx_input_status {
    KOSDEC_ADX_INPUT_DATA, KOSDEC_ADX_INPUT_WAIT,
    KOSDEC_ADX_INPUT_END, KOSDEC_ADX_INPUT_ERROR
};
struct kosdec_adx_input_result {
    enum kosdec_adx_input_status status;
    size_t bytes;
};
/* Copy bytes at absolute offset into dst, synchronously. DATA requires 1..capacity
   bytes; every other result requires zero bytes. WAIT means try later, END means
   EOF. Never retain dst or access it after return, including WAIT/error returns.
   No reentry into this stream. Immutable source and fixed extent are required.
   Providers with async work must own separate staging and retirement themselves. */
typedef struct kosdec_adx_input_result (*kosdec_adx_read_at)(
    void *context, uint32_t offset, uint8_t *dst, size_t capacity);
struct kosdec_adx_stream {
    uint32_t magic;
    struct kosdec_adx_decoder decoder;
    kosdec_adx_read_at read_at;
    void *context;
    uint32_t input_bytes, byte_position, source_frame, seek_frame;
    uint64_t returned_frames, wraps;
    unsigned input_used, input_count, cache_position, cache_frames;
    bool repeat, paused, prepared, seeking;
    enum kosdec_adx_status terminal;
    uint8_t input[512];
    int16_t pcm[64];
};
struct kosdec_adx_stream_result {
    enum kosdec_adx_status status;
    size_t frames;
};
/* Caller owns disjoint state/context/output and serializes every API call.
   Context must stay alive until terminal/cancel/reinitialization. Control calls
   are between reads; cancellation cannot interrupt a running provider callback.
   No allocation or scheduler dependency. Fields are implementation-owned.
   Same admitted formats and carried full-block history policy as memory reader. */
void kosdec_adx_stream_init(struct kosdec_adx_stream *, kosdec_adx_read_at,
                          void *context, uint32_t input_bytes, bool repeat);
void kosdec_adx_stream_pause(struct kosdec_adx_stream *, bool paused);
void kosdec_adx_stream_release_loop(struct kosdec_adx_stream *);
void kosdec_adx_stream_cancel(struct kosdec_adx_stream *);
/* Serialized, unbound-source seek. begin resets decoding to the start with the
   same borrowed provider/extent and optional PCM filter, and unpauses; no I/O.
   Discarded seek frames do not invoke the filter. Repeating readers
   return UNSUPPORTED unchanged: release the loop before seeking. Known out-of-
   range targets return BAD_ARGUMENT unchanged; unknown bounds are checked when
   the header is parsed. Terminal/cancelled readers may begin again. Never call
   while a player/service owns the source; cancel/release and join it first.
   step reconstructs history by discarding <=32 frames, with <=1 provider call
   (<=512 bytes), preserving full-group arithmetic. O(target) work, no index.
   MORE/NEED_INPUT mean continue; DONE means positioned, not validated EOF.
   Target==total_frames still needs a normal read to validate the footer.
   Failures are sticky until a new begin/init. No PCM is returned during seek;
   returned_frames/wraps reset and exclude discarded PCM. Normal read rejects
   a pending seek. A pipe/service automatically steps a bound pending seek.
   Provider ownership/retirement is unchanged; no async staging is freed. */
enum kosdec_adx_status kosdec_adx_stream_seek_begin(struct kosdec_adx_stream *, uint32_t frame);
enum kosdec_adx_status kosdec_adx_stream_seek_step(struct kosdec_adx_stream *);
/* At most one provider call (up to 512 bytes), one decoded 32-frame group, or
   512 metadata bytes per call. May block inside provider; never spins on WAIT.
   NEED_INPUT asks caller to retry later; MORE may return zero PCM after partial
   input/metadata progress. Paused returns MORE/0; zero capacity suppresses I/O
   and returns NEED_OUTPUT even before header/footer work. Output is interleaved
   PCM16, capacity in frames, naturally aligned, frames*channels elements.
   At loop endpoint next nonempty read commits rewind before requesting bytes;
   wraps counts this transition even if that request waits/fails. Releasing after
   it continues the new iteration to EOF. Cached tail survives earlier release.
   source_frame is consumed PCM source position before any pending rewind.
   returned_frames/wraps are modulo-2^64 counters, not a hardware playback clock.
   DONE validates footer through the declared immutable extent. Early EOF is
   TRUNCATED, provider errors/contract violations IO_ERROR. Terminals are sticky;
   BAD_ARGUMENT on read does not mutate initialized state. */
struct kosdec_adx_stream_result kosdec_adx_stream_read(
    struct kosdec_adx_stream *, int16_t *output, size_t output_frames);
#ifdef __cplusplus
}
#endif
#endif
