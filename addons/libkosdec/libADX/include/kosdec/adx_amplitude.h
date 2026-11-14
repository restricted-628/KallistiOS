/* Original bounded PCM amplitude analyzer. No device or clock ownership. */
#ifndef KOSDEC_ADX_AMPLITUDE_H
#define KOSDEC_ADX_AMPLITUDE_H
#include <kosdec/adx.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_ADX_AMPLITUDE_BUDGET 512u
struct kosdec_adx_amplitude_record {
    uint64_t start_frame;
    uint32_t peak, sample_rate, frames;
};
struct kosdec_adx_amplitude_channel {
    uint64_t position;
    uint32_t used, peak, write_cursor, read_cursor;
};
struct kosdec_adx_amplitude {
    uint32_t magic, channels, sample_rate, window_frames, capacity, sealed, fault;
    struct kosdec_adx_amplitude_record *records;
    struct kosdec_adx_amplitude_channel channel[2];
};
struct kosdec_adx_amplitude_result {
    enum kosdec_adx_status status;
    size_t consumed; /* PCM frames from the specified channel. */
};
/* Storage consists of channels*capacity typed records, capacity per channel.
   Mono/stereo, power-of-two capacity2..65536. Zero means invalid arguments.
   This native sizing is not the SDK's raw work-buffer layout. */
size_t kosdec_adx_amplitude_work_size(unsigned channels, size_t capacity);
/* Caller owns disjoint, aligned state/records. Rate1..48000, window1..INT32_MAX
   PCM frames. DONE initializes state; BAD_ARGUMENT leaves it unchanged. Init
   does not initialize record bytes. Reinit only after producer join and consumer
   quiescence. Typical100ms window is4410 frames at44100Hz; no implicit rate or
   floating time conversion. State fields are implementation-owned. */
enum kosdec_adx_status kosdec_adx_amplitude_init(struct kosdec_adx_amplitude *,
    struct kosdec_adx_amplitude_record *, size_t record_count, unsigned channels,
    size_t capacity, uint32_t sample_rate, uint32_t window_frames);
/* One serialized producer feeds both channels; one consumer per channel calls
   read/select. PCM16 may have stride1 or2. At most512 frames consumed per call.
   Peak is max(abs(sample)), range0..32768, over complete nonoverlapping windows.
   Full record queue returns NEED_OUTPUT before consuming the next frame; retry
   the unconsumed suffix after consumer progress. MORE means accepted input (or
   budget exhausted), not source completion. No allocation, I/O or callbacks.
   Inputs/state/storage are disjoint; supplied PCM span must cover the stride. */
struct kosdec_adx_amplitude_result kosdec_adx_amplitude_feed(
    struct kosdec_adx_amplitude *, unsigned channel, const int16_t *,
    size_t frames, unsigned stride);
/* Optional adapter for kosdec_adx_set_filter/sequence_set_filter/joint reader.
   Never modifies PCM. On incomplete feed it latches a visible fault and stops
   analysis; playback continues. Provision/drain records so this cannot happen
   in normal use. status() reports the fault even while records remain queued.
   Configure before producer binding; keep state/storage alive until producer
   join. A custom PCM filter may call this adapter in its chosen processing order.
   The decoder has one filter slot: this is not an implicit second filter. */
void kosdec_adx_amplitude_filter(void *, unsigned, int16_t *, size_t, unsigned);
/* Producer-only, after final input. Discards each incomplete window, publishes
   sealed state. Does not join a producer or detach a filter. Further feed is
   BAD_ARGUMENT. Faults remain sticky until quiescent reinit. */
enum kosdec_adx_status kosdec_adx_amplitude_finish(struct kosdec_adx_amplitude *);
/* Any thread: MORE while accepting input, DONE once sealed, or sticky adapter
   fault. DONE here does not mean the record queues have been consumed. */
enum kosdec_adx_status kosdec_adx_amplitude_status(const struct kosdec_adx_amplitude *);
/* Consumer-only. DONE copies one complete record and removes it. Empty queue:
   NEED_INPUT before finish, NEED_END after finish, or sticky adapter fault.
   Output unchanged unless DONE. Output must be disjoint from state/storage.
   read and select share one consumer owner for that channel. */
enum kosdec_adx_status kosdec_adx_amplitude_read(struct kosdec_adx_amplitude *,
    unsigned channel, struct kosdec_adx_amplitude_record *);
/* Consumer-only retained selection, matching baseline record advancement:
   discard records with start_frame < requested while a newer queued record
   exists; return the remaining head without consuming it. Thus it selects the
   first available start at/after the request, or the latest available record.
   Already discarded records cannot be recovered by moving the request backward.
   Caller supplies source-frame time. This is NOT an audible-clock measurement;
   do not substitute copied/decoded frames for a qualified playback clock. */
enum kosdec_adx_status kosdec_adx_amplitude_select(struct kosdec_adx_amplitude *,
    unsigned channel, uint64_t requested, struct kosdec_adx_amplitude_record *);
#ifdef __cplusplus
}
#endif
#endif
