#ifndef MEDIA_ADX_SERVICE_INTERNAL_H
#define MEDIA_ADX_SERVICE_INTERNAL_H

#include <kosdec/adx_service.h>

/* Joint capacity is at most INT32_MAX bytes. Publish format readiness
   and the available DATA count together at the producer boundary. */
#define MEDIA_ADX_INPUT_FORMAT_READY UINT32_C(0x80000000)

/* One producer step. A nonzero, block-aligned limit caps an individual
   PCM reservation. Zero selects the normal workspace limit. */
struct kosdec_adx_stream_result media_adx_pipe_pump_limited(
    struct kosdec_adx_pipe *, size_t frame_limit, bool separate_transport);
/* Owner-side commit of a staged decoded-only discard. Terminal and both
   captured cursors must still match. A rejected commit leaves PCM intact. */
bool media_adx_pipe_discard_done(struct kosdec_adx_pipe *,
    uint32_t read, uint32_t write, uint32_t frames);
/* While awaiting DECINFO, allow header and control steps. Once the format
   is published, wait for the owner to admit the first PCM reservation. */
struct kosdec_adx_stream_result media_adx_service_step_limited(
    struct kosdec_adx_service *, size_t frame_limit, bool await_format_admission,
    bool separate_transport);
/* Serialized with decoder steps in the owner-driven compatibility binding. */
enum kosdec_adx_status media_adx_service_step_transport(struct kosdec_adx_service *, bool reload);

#endif
