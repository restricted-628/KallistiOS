#ifndef MEDIA_ADX_STREAM_INTERNAL_H
#define MEDIA_ADX_STREAM_INTERNAL_H

#include <kosdec/adx_sequence.h>

#define MEDIA_ADX_READ_CHUNK 512u

enum kosdec_adx_status media_adx_stream_step_file(struct kosdec_adx_stream *);
struct kosdec_adx_stream_result media_adx_stream_read(
    struct kosdec_adx_stream *, int16_t *, size_t, bool separate_transport);
enum kosdec_adx_status media_adx_stream_seek_step(struct kosdec_adx_stream *,
                                                bool separate_transport);
enum kosdec_adx_status media_adx_sequence_step_file(struct kosdec_adx_sequence *);
struct kosdec_adx_sequence_result media_adx_sequence_read(
    struct kosdec_adx_sequence *, int16_t *, size_t, bool separate_transport);

#endif
