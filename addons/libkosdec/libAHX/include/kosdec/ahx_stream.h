#ifndef KOSDEC_AHX_STREAM_H
#define KOSDEC_AHX_STREAM_H

#include <kosdec/ahx.h>

#ifdef __cplusplus
extern "C" {
#endif

enum kosdec_ahx_read_status {
    KOSDEC_AHX_READ_BYTES,
    KOSDEC_AHX_READ_AGAIN,
    KOSDEC_AHX_READ_ERROR
};

/* The provider owns a finite source. A BYTES result writes 1..capacity
   bytes and sets bytes_read. AGAIN and ERROR write no bytes. The caller
   retains the provider and its context until stop or successful rebind.
   A read is synchronous and must not retain dst or reenter the stream. */
typedef enum kosdec_ahx_read_status (*kosdec_ahx_read_at)(
    void *context, uint64_t offset, void *dst, size_t capacity, size_t *bytes_read);

struct kosdec_ahx_stream;
size_t kosdec_ahx_stream_workspace_size(void);
size_t kosdec_ahx_stream_workspace_alignment(void);
struct kosdec_ahx_stream *kosdec_ahx_stream_init(void *workspace, size_t bytes);

/* Bind or rebind a finite source, resetting the parser and synthesis
   history only on success. Calls for an instance are serialized. */
bool kosdec_ahx_stream_start(struct kosdec_ahx_stream *stream,
    kosdec_ahx_read_at read_at, void *context, uint64_t size);

/* Synchronous unbind. On return no future service can use the old context. */
void kosdec_ahx_stream_stop(struct kosdec_ahx_stream *stream);

/* Format info becomes visible after the first complete coded frame arrives.
   Every admitted title produces one channel of signed PCM16 in granules of
   KOSDEC_AHX_GRANULE_SAMPLES. */
bool kosdec_ahx_stream_info(const struct kosdec_ahx_stream *stream,
    struct kosdec_ahx_info *info);

/* At most one provider read and one decoder granule per service, with the
   same output and status contract as decode. A zero capacity can prime
   metadata without emitting/advancing synthesis. consumed counts coded
   bytes accepted by the decoder, excluding buffered unread source bytes. */
struct kosdec_ahx_decode_result kosdec_ahx_stream_service(struct kosdec_ahx_stream *stream,
    int16_t *pcm, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
