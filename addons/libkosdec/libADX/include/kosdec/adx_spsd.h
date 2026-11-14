/* Original bounded SPSD audio reader; separate from the ADX codec. */
#ifndef KOSDEC_ADX_SPSD_H
#define KOSDEC_ADX_SPSD_H
#include <kosdec/adx_stream.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_spsd_info {
    uint32_t sample_rate, total_frames, data_offset, data_bytes;
    uint8_t channels, sample_bits;
};
struct kosdec_adx_spsd {
    uint32_t magic, input_bytes, offset, frames_done;
    kosdec_adx_read_at read_at;
    void *context;
    struct kosdec_adx_spsd_info info;
    enum kosdec_adx_status terminal;
    unsigned used, frame_bytes;
    bool header_ready;
    kosdec_adx_pcm_filter filter;
    void *filter_context;
    uint8_t buffer[512];
};
struct kosdec_adx_spsd_result { enum kosdec_adx_status status; size_t frames; };
/* Caller owns state, immutable source and synchronous read_at provider/context;
   initialize only after all prior users retire. All operations serialized;
   state, source and PCM output disjoint. Provider may not retain destination or
   reenter; backend staging/retirement remains independently owned by provider.
   SPSD64-byte versions01010004/00010004; codec0, mono, interleave index0.
   Signed16 little-endian contiguous payload only, even data bytes and nonzero
   rate. Other codecs/channels/layouts are unsupported. Loop/config fields and
   trailing bytes are ignored; no embedded repeat. MORE on success;
   extent<64 initializes TRUNCATED; bad arguments unchanged. */
enum kosdec_adx_status kosdec_adx_spsd_init(struct kosdec_adx_spsd *,
    kosdec_adx_read_at, void *context, uint32_t input_bytes);
/* One callback<=512 bytes and <=32 output frames per call. WAIT returns
   NEED_INPUT; positive partial input retained. Interleaved signed16 output,
   capacity in frames, int16 alignment required. Zero capacity performs no I/O.
   Provider may block: use a suitable producer thread/fiber, never audio IRQ.
   DONE means selected payload decoded, not validation of trailing bytes/EOF.
   Errors/cancellation sticky until eligible seek/reinit. */
struct kosdec_adx_spsd_result kosdec_adx_spsd_read(struct kosdec_adx_spsd *,
                                            int16_t *output, size_t frames);
/* DONE publishes admitted metadata even after subsequent payload failure.
   Before admission: NEED_INPUT or terminal error. Failure leaves output intact. */
enum kosdec_adx_status kosdec_adx_spsd_get_info(const struct kosdec_adx_spsd *,
                                           struct kosdec_adx_spsd_info *);
/* Unbound/serialized owner only: frame0..total_frames after header admission.
   Clears buffered input and terminal state without I/O; preserves filter.
   Invalid position/state leaves reader unchanged. */
enum kosdec_adx_status kosdec_adx_spsd_seek(struct kosdec_adx_spsd *, uint32_t frame);
void kosdec_adx_spsd_cancel(struct kosdec_adx_spsd *);
/* Unbound producer setup, same per-channel/stride/lifetime contract as ADX PCM
   filters. Cannot attach after terminal state without successful seek/reinit. */
enum kosdec_adx_status kosdec_adx_spsd_set_filter(struct kosdec_adx_spsd *,
    kosdec_adx_pcm_filter, void *context);
#ifdef __cplusplus
}
#endif
#endif
