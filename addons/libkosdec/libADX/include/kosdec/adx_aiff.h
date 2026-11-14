/* Original bounded FORM/AIFF PCM reader; separate from the ADX codec. */
#ifndef KOSDEC_ADX_AIFF_H
#define KOSDEC_ADX_AIFF_H
#include <kosdec/adx_stream.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_aiff_info {
    uint32_t sample_rate, total_frames, data_offset, data_bytes;
    uint8_t channels, sample_bits;
};
struct kosdec_adx_aiff {
    uint32_t magic, input_bytes, offset, form_end, next_chunk, frames_done, sound_bytes;
    kosdec_adx_read_at read_at;
    void *context;
    struct kosdec_adx_aiff_info info;
    enum kosdec_adx_status terminal;
    unsigned phase, used, need, frame_bytes;
    bool format_ready, sound_ready, header_ready;
    kosdec_adx_pcm_filter filter;
    void *filter_context;
    uint8_t buffer[512];
};
struct kosdec_adx_aiff_result { enum kosdec_adx_status status; size_t frames; };
/* Initially/after all prior users retire. Caller owns state/provider/context;
   source is immutable and input_bytes is its fixed extent. Same synchronous
   read_at contract as ADX streams: no retained destination/reentry, independent
   staging belongs to provider. Every operation is externally serialized; state,
   source/provider storage and PCM output are disjoint. No allocation/device I/O.
   FORM AIFF signedPCM8/BE16 mono/stereo only. COMM/SSND in either order,
   word-padded chunks, byte-aligned input. Unknown chunks/INST/MARK are skipped;
   no embedded-loop interpretation or AIFF-C support. SSND offset and trailing
   alignment padding are bounded; blockSize does not change sample packing.
   Empty COMM may omit SSND. Rates must be positive normalized integral80-bit
   values in1..UINT32_MAX; fractional/out-of-range rates are UNSUPPORTED.
   Returns MORE, or initializes TRUNCATED for extent<12; invalidargs unchanged. */
enum kosdec_adx_status kosdec_adx_aiff_init(struct kosdec_adx_aiff *,
    kosdec_adx_read_at, void *context, uint32_t input_bytes);
/* At most one callback<=512bytes and at most32 output frames per call. Positive
   short input is retained; WAIT returns NEED_INPUT, never spins. Header steps
   return MORE with no PCM. Output is interleaved signed16; signed8 maps x*256.
   frames is capacity in frames, not scalar samples. Zero capacity returns
   NEED_OUTPUT with no I/O; output must be int16-aligned. Provider may block, so
   service on an appropriate thread/fiber, never audio IRQ. Completion means the
   declared sample frames were decoded, not validation of trailing chunks/source EOF.
   FORM declared extent must fit input; after selecting COMM/SSND, trailing
   chunks/source bytes and sound-alignment padding are ignored.
   Header/data failure and cancellation are sticky until eligible seek/reinit. */
struct kosdec_adx_aiff_result kosdec_adx_aiff_read(struct kosdec_adx_aiff *,
                                                int16_t *output, size_t frames);
/* DONE publishes header info, even after later payload failure/cancel. Before
   data admission: NEED_INPUT or terminalerror. Invalidargs/failure leave out. */
enum kosdec_adx_status kosdec_adx_aiff_get_info(const struct kosdec_adx_aiff *,
                                             struct kosdec_adx_aiff_info *);
/* Unbound/serialized owner only. After header admission, frame0..total_frames.
   Clears buffered input and terminal state (including cancellation/error), with
   no provider call. Returns DONE for successful repositioning; next read returns
   PCM or DONE at EOF. Invalid position/state leaves reader unchanged. */
enum kosdec_adx_status kosdec_adx_aiff_seek(struct kosdec_adx_aiff *, uint32_t frame);
void kosdec_adx_aiff_cancel(struct kosdec_adx_aiff *);
/* Unbound, serialized producer setup; same per-channel/stride/lifetime contract
   as ADX PCM filters. Runs after exact PCM conversion, before publication.
   Cannot attach after terminal state without a successful seek/reinit. */
enum kosdec_adx_status kosdec_adx_aiff_set_filter(struct kosdec_adx_aiff *,
    kosdec_adx_pcm_filter, void *context);
#ifdef __cplusplus
}
#endif
#endif
