#include <kosdec/ahx_stream.h>
#include "silent.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct source {
    size_t reads, offset;
    bool again, error;
};

static enum kosdec_ahx_read_status read_at(void *context, uint64_t offset,
    void *dst, size_t capacity, size_t *bytes_read) {
    struct source *s = context;
    assert(capacity > 0 && capacity <= sizeof(silent) - offset && offset == s->offset);
    ++s->reads;
    if(s->error)
        return KOSDEC_AHX_READ_ERROR;
    if(s->again) {
        s->again = false;
        return KOSDEC_AHX_READ_AGAIN;
    }
    size_t n = capacity < 3 ? capacity : 3;
    memcpy(dst, silent + offset, n);
    *bytes_read = n;
    s->offset += n;
    return KOSDEC_AHX_READ_BYTES;
}

int main(void) {
    size_t bytes = kosdec_ahx_stream_workspace_size();
    void *workspace = malloc(bytes);
    assert(workspace && !kosdec_ahx_stream_init(workspace, bytes - 1));
    struct kosdec_ahx_stream *s = kosdec_ahx_stream_init(workspace, bytes);
    struct source first = { .again = true }, second = { .error = true };
    struct kosdec_ahx_info info;
    assert(s && !kosdec_ahx_stream_info(s, &info));
    assert(kosdec_ahx_stream_service(s, NULL, 0).status == KOSDEC_AHX_BAD_ARGUMENT);
    assert(kosdec_ahx_stream_start(s, read_at, &first, sizeof(silent)));
    assert(kosdec_ahx_stream_service(s, NULL, 0).status == KOSDEC_AHX_NEED_INPUT);
    assert(first.reads == 1 && !first.offset);
    assert(!kosdec_ahx_stream_start(s, NULL, &second, sizeof(silent)));

    struct kosdec_ahx_decode_result result;
    size_t used = 0;
    do {
        size_t previous = first.reads;
        result = kosdec_ahx_stream_service(s, NULL, 0);
        assert(first.reads <= previous + 1);
        used += result.consumed;
        assert(!result.samples);
        if(result.status == KOSDEC_AHX_NEED_INPUT)
            assert(!kosdec_ahx_stream_info(s, &info));
    } while(result.status == KOSDEC_AHX_NEED_INPUT && first.offset < sizeof(silent));
    assert(result.status == KOSDEC_AHX_NEED_OUTPUT && used == 26 + 14);
    assert(kosdec_ahx_stream_info(s, &info) && info.rate == 22050 && info.declared_samples == 1);
    assert(kosdec_ahx_stream_service(s, NULL, 0).status == KOSDEC_AHX_NEED_OUTPUT);
    int16_t pcm[KOSDEC_AHX_GRANULE_SAMPLES];
    size_t samples = 0, valid = 0;
    do {
        result = kosdec_ahx_stream_service(s, pcm, KOSDEC_AHX_GRANULE_SAMPLES);
        assert(result.status == KOSDEC_AHX_MORE || result.status == KOSDEC_AHX_NEED_INPUT ||
               result.status == KOSDEC_AHX_DONE);
        for(size_t i = 0; i < result.samples; ++i)
            assert(pcm[i] == 0);
        samples += result.samples;
        valid += result.valid_samples;
        used += result.consumed;
    } while(result.status != KOSDEC_AHX_DONE);
    assert(samples == KOSDEC_AHX_FRAME_SAMPLES && valid == 1 && used == sizeof(silent));

    size_t retired_reads = first.reads;
    assert(kosdec_ahx_stream_start(s, read_at, &second, sizeof(silent)));
    assert(!kosdec_ahx_stream_info(s, &info));
    assert(kosdec_ahx_stream_service(s, pcm, 1).status == KOSDEC_AHX_IO_ERROR);
    assert(kosdec_ahx_stream_service(s, pcm, 1).status == KOSDEC_AHX_IO_ERROR);
    assert(first.reads == retired_reads && second.reads == 1);
    kosdec_ahx_stream_stop(s);
    assert(kosdec_ahx_stream_service(s, pcm, 1).status == KOSDEC_AHX_BAD_ARGUMENT);
    assert(second.reads == 1);
    second = (struct source){0};
    assert(kosdec_ahx_stream_start(s, read_at, &second, 20));
    do {
        result = kosdec_ahx_stream_service(s, pcm, 1);
    } while(result.status == KOSDEC_AHX_NEED_INPUT);
    assert(result.status == KOSDEC_AHX_TRUNCATED && second.offset == 20);
    kosdec_ahx_stream_stop(s);
    free(workspace);
    puts("AHX STREAM PASS");
    return 0;
}
