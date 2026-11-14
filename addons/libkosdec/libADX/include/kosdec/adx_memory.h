/* Original bounded resident-input PCM reader. No audio device ownership. */
#ifndef KOSDEC_ADX_MEMORY_H
#define KOSDEC_ADX_MEMORY_H
#include <kosdec/adx.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_memory {
    uint32_t magic;
    struct kosdec_adx_decoder decoder;
    const uint8_t *input;
    size_t input_bytes, byte_position;
    uint32_t source_frame;
    uint64_t returned_frames, wraps;
    unsigned cache_position, cache_frames;
    bool repeat, paused, prepared;
    enum kosdec_adx_status terminal;
    int16_t pcm[64];
};
struct kosdec_adx_memory_result {
    enum kosdec_adx_status status;
    size_t frames;
};
/* Borrow immutable whole-file input until DONE/cancel/reinitialization. No input
   copies or allocations. State/input/output must be disjoint; serialize calls.
   A requested repeat requires a supported loop record. Release is irreversible
   until reinitialization. Fields are inspectable, implementation-owned state.
   source_frame is the next returned source frame, not the audio device clock.
   returned_frames/wraps are cumulative unsigned counters (modulo 2^64). */
void kosdec_adx_memory_init(struct kosdec_adx_memory *reader,
                          const uint8_t *input, size_t bytes, bool repeat);
void kosdec_adx_memory_pause(struct kosdec_adx_memory *reader, bool paused);
void kosdec_adx_memory_release_loop(struct kosdec_adx_memory *reader);
void kosdec_adx_memory_cancel(struct kosdec_adx_memory *reader);
/* Returns at most one cached group, decoding at most 32 frames/channel, or
   parsing at most 512 metadata bytes. Output capacity is frames*channels PCM16
   elements. Paused calls return MORE/0 without progress; zero capacity returns
   NEED_OUTPUT after preparation, without wrapping. BAD_ARGUMENT never mutates.
   At a loop endpoint the next nonempty read wraps and carries full-block history.
   Releasing before that read retains the cached tail and continues to file end.
   Full groups, including padding, advance private predictor history; padding is
   never returned. DONE follows footer validation, possibly after the last PCM
   call. Terminal statuses are sticky. The optional decoder PCM filter runs only
   for returned frames; no I/O, yields or device work. */
struct kosdec_adx_memory_result kosdec_adx_memory_read(
    struct kosdec_adx_memory *reader, int16_t *output, size_t output_frames);
#ifdef __cplusplus
}
#endif
#endif
