/* Copyright (C) 2026 Kyle Mayer */
#include <kosdec/ahx_stream.h>

#include <stdalign.h>

enum { INPUT_CAPACITY = 1024 };

struct kosdec_ahx_stream {
    alignas(max_align_t) uint8_t input[INPUT_CAPACITY];
    struct kosdec_ahx_decoder *decoder;
    kosdec_ahx_read_at read_at;
    void *context;
    uint64_t size, read_offset;
    size_t offset, available;
    bool primed, failed;
};

size_t kosdec_ahx_stream_workspace_size(void) {
    return sizeof(struct kosdec_ahx_stream) + kosdec_ahx_decoder_workspace_size();
}

size_t kosdec_ahx_stream_workspace_alignment(void) {
    return alignof(struct kosdec_ahx_stream);
}

struct kosdec_ahx_stream *kosdec_ahx_stream_init(void *workspace, size_t bytes) {
    if(!workspace || bytes < kosdec_ahx_stream_workspace_size() ||
       (uintptr_t)workspace % alignof(struct kosdec_ahx_stream))
        return NULL;
    struct kosdec_ahx_stream *s = workspace;
    *s = (struct kosdec_ahx_stream){0};
    return s;
}

bool kosdec_ahx_stream_start(struct kosdec_ahx_stream *s,
    kosdec_ahx_read_at read_at, void *context, uint64_t size) {
    if(!s || !read_at || !size)
        return false;
    struct kosdec_ahx_decoder *d = kosdec_ahx_decoder_init(s + 1,
        kosdec_ahx_decoder_workspace_size());
    if(!d)
        return false;
    s->decoder = d;
    s->read_at = read_at;
    s->context = context;
    s->size = size;
    s->read_offset = 0;
    s->offset = 0;
    s->available = 0;
    s->primed = false;
    s->failed = false;
    return true;
}

void kosdec_ahx_stream_stop(struct kosdec_ahx_stream *s) {
    if(s) {
        s->read_at = NULL;
        s->context = NULL;
        s->primed = false;
    }
}

bool kosdec_ahx_stream_info(const struct kosdec_ahx_stream *s, struct kosdec_ahx_info *info) {
    return s && s->primed && kosdec_ahx_decoder_info(s->decoder, info);
}

static struct kosdec_ahx_decode_result service_buffer(struct kosdec_ahx_stream *s,
    int16_t *pcm, size_t capacity) {
    struct kosdec_ahx_decode_result result = kosdec_ahx_decode(s->decoder,
        s->input + s->offset, s->available - s->offset, s->read_offset == s->size,
        pcm, capacity);
    s->offset += result.consumed;
    if(result.status == KOSDEC_AHX_NEED_OUTPUT || result.status == KOSDEC_AHX_MORE)
        s->primed = true;
    return result;
}

struct kosdec_ahx_decode_result kosdec_ahx_stream_service(struct kosdec_ahx_stream *s,
    int16_t *pcm, size_t capacity) {
    if(!s || !s->read_at || (!pcm && capacity))
        return (struct kosdec_ahx_decode_result){ .status = KOSDEC_AHX_BAD_ARGUMENT };
    if(s->failed)
        return (struct kosdec_ahx_decode_result){ .status = KOSDEC_AHX_IO_ERROR };
    struct kosdec_ahx_decode_result first = service_buffer(s, pcm, capacity);
    if(first.status != KOSDEC_AHX_NEED_INPUT)
        return first;

    uint64_t remaining = s->size - s->read_offset;
    size_t limit = remaining < INPUT_CAPACITY ? (size_t)remaining : INPUT_CAPACITY;
    size_t bytes = 0;
    enum kosdec_ahx_read_status status = s->read_at(s->context,
        s->read_offset, s->input, limit, &bytes);
    if(status == KOSDEC_AHX_READ_AGAIN && !bytes)
        return first;
    if(status != KOSDEC_AHX_READ_BYTES || !bytes || bytes > limit) {
        s->failed = true;
        first.status = KOSDEC_AHX_IO_ERROR;
        return first;
    }
    s->offset = 0;
    s->available = bytes;
    s->read_offset += bytes;
    struct kosdec_ahx_decode_result result = service_buffer(s, pcm, capacity);
    result.consumed += first.consumed;
    return result;
}
