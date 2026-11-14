/* Original bounded RIFF/WAVE PCM and CRI packed4 reader; separate from the ADX codec. */
#ifndef KOSDEC_ADX_WAVE_H
#define KOSDEC_ADX_WAVE_H
#include <kosdec/adx_stream.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_wave_info {
    uint32_t sample_rate, total_frames, data_offset, data_bytes;
    uint8_t channels, sample_bits;
};
struct kosdec_adx_wave {
    uint32_t magic, input_bytes, offset, riff_end, next_chunk, frames_done;
    kosdec_adx_read_at read_at;
    void *context;
    struct kosdec_adx_wave_info info;
    enum kosdec_adx_status terminal;
    unsigned phase, used, need, frame_bytes;
    bool format_ready, header_ready, seeking;
    uint32_t seek_target;
    unsigned input_pos, cached_pos, cached_frames;
    int32_t previous[2], step[2];
    int16_t cached[8];
    kosdec_adx_pcm_filter filter;
    void *filter_context;
    uint8_t buffer[512];
};
struct kosdec_adx_wave_result { enum kosdec_adx_status status; size_t frames; };
/* Initially/after all prior users retire. Caller owns state/provider/context;
   source is immutable and input_bytes is its fixed extent. Same synchronous
   read_at contract as ADX streams: no retained destination/reentry, independent
   staging belongs to provider. Every operation is externally serialized; state,
   source/provider storage and PCM output are disjoint. No allocation/device I/O.
   Tag1, mono/stereo: unsignedPCM8, signedPCM16 or CRI packedADPCM4. Byte-aligned input accepted. RIFF/WAVE with
   word-padded chunks and fmt before data; validates rate/data bounds and PCM8/16 alignment/byte-rate hints.
   Packed4 ignores those two hints; data must contain whole four-frame groups
   (two bytes per channel). Channel bytes interleave; low nibble decodes first.
   Unknown chunks are skipped. No loop metadata, RIFX, extensible WAVE or standard tag0x20 Yamaha WAVE.
   Returns MORE, or initializes TRUNCATED for extent<12; invalidargs unchanged. */
enum kosdec_adx_status kosdec_adx_wave_init(struct kosdec_adx_wave *,
    kosdec_adx_read_at, void *context, uint32_t input_bytes);
/* At most one callback<=512bytes and at most32 output frames per call. Positive
   short input is retained; WAIT returns NEED_INPUT, never spins. Header steps
   return MORE with no PCM. Output is interleaved signed16; U8 maps(x-128)*256. Packed4 uses AICA
   predictor/quantizer rules; pending seek discards<=32frames with no filter/output.
   frames is capacity in frames, not scalar samples. Zero capacity returns
   NEED_OUTPUT with no I/O; output must be int16-aligned. Provider may block, so
   service on an appropriate thread/fiber, never audio IRQ. Completion means the
   selected data chunk was decoded, not validation of trailing chunks/source EOF.
   RIFF declared extent must fit input; trailing chunks/source bytes are ignored.
   Header/data failure and cancellation are sticky until eligible seek/reinit. */
struct kosdec_adx_wave_result kosdec_adx_wave_read(struct kosdec_adx_wave *,
                                                int16_t *output, size_t frames);
/* DONE publishes header info, even after later payload failure/cancel. Before
   data admission: NEED_INPUT or terminalerror. Invalidargs/failure leave out. */
enum kosdec_adx_status kosdec_adx_wave_get_info(const struct kosdec_adx_wave *,
                                             struct kosdec_adx_wave_info *);
/* Unbound/serialized owner only, after header admission, frame0..total_frames.
   Clears buffered input and terminal state, preserving filter. PCM8/16 seek is
   constant-time (DONE). Packed4 resets history and starts a bounded rescan for
   nonzero/non-EOF targets (MORE); call seek_step until DONE or bind and let read
   advance the seek. Zero/EOF targets return DONE immediately. No I/O in seek.
   Invalid position/state leaves reader unchanged. */
enum kosdec_adx_status kosdec_adx_wave_seek(struct kosdec_adx_wave *, uint32_t frame);
/* Unbound producer, or read advances this internally. One callback<=512 bytes,
   <=32 discarded frames, no filter. DONE when positioned, MORE/NEED_INPUT while
   pending, sticky error/cancel on failure. Before header admission BAD_ARGUMENT. */
enum kosdec_adx_status kosdec_adx_wave_seek_step(struct kosdec_adx_wave *);
void kosdec_adx_wave_cancel(struct kosdec_adx_wave *);
/* Unbound, serialized producer setup; same per-channel/stride/lifetime contract
   as ADX PCM filters. Runs after exact PCM conversion, before publication.
   Cannot attach after terminal state without a successful seek/reinit. */
enum kosdec_adx_status kosdec_adx_wave_set_filter(struct kosdec_adx_wave *,
    kosdec_adx_pcm_filter, void *context);
#ifdef __cplusplus
}
#endif
#endif
