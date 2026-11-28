/* Copyright (C) 2026 Kyle Mayer */
#include <kosdec/ahx.h>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t header[] = {
    0x80, 0, 0, 22, 0x10, 0, 0, 1, 0, 0, 0x56, 0x22,
    0, 0, 0, 1, 0, 0, 6, 0, '(', 'c', ')', 'C', 'R', 'I'
};
static const uint8_t empty_frame[] = {
    0xff, 0xf5, 0xe1, 0xc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t endcode[] = {
    0, 0x80, 1, 0, 12, 'A', 'H', 'X', 'E', '(', 'c', ')', 'C', 'R', 'I', 0, 0
};
enum { STREAM_BYTES = sizeof(header) + sizeof(empty_frame) + sizeof(endcode) };

static void make_stream(uint8_t *out) {
    memcpy(out, header, sizeof(header));
    memcpy(out + sizeof(header), empty_frame, sizeof(empty_frame));
    memcpy(out + sizeof(header) + sizeof(empty_frame), endcode, sizeof(endcode));
}

static enum kosdec_ahx_result feed_until_end(void *workspace, const uint8_t *data,
                                             size_t bytes, size_t window,
                                             size_t *frame_count, bool trace) {
    struct kosdec_ahx_parser *p = kosdec_ahx_parser_init(workspace, kosdec_ahx_parser_workspace_size());
    struct kosdec_ahx_info info;
    const struct kosdec_ahx_frame sentinel = { .index = UINT32_MAX };
    assert(p && !kosdec_ahx_parser_info(p, &info) && window);
    for(size_t cursor = 0;;) {
        struct kosdec_ahx_frame frame = sentinel;
        size_t left = bytes - cursor;
        size_t offer = left < window ? left : window;
        size_t used = 0;
        enum kosdec_ahx_result result = kosdec_ahx_parser_feed(p, data + cursor, offer,
                                                               offer == left, &used, &frame);
        assert(used <= offer);
        cursor += used;
        if(result == KOSDEC_AHX_FRAME) {
            assert(kosdec_ahx_parser_info(p, &info));
            assert(frame.bytes <= cursor && frame.index == *frame_count);
            assert(!memcmp(frame.data, data + cursor - frame.bytes, frame.bytes));
            uint32_t remaining = info.declared_samples - frame.index * KOSDEC_AHX_FRAME_SAMPLES;
            assert(frame.valid_samples == (remaining < KOSDEC_AHX_FRAME_SAMPLES ?
                   remaining : KOSDEC_AHX_FRAME_SAMPLES));
            ++*frame_count;
            if(trace)
                printf("frame %" PRIu32 " %zu %zu %u\n",
                       frame.index, cursor - frame.bytes, cursor, (unsigned)frame.valid_samples);
            continue;
        }
        assert(!frame.data && frame.index == sentinel.index);
        if(result == KOSDEC_AHX_NEED_INPUT) {
            assert(used == offer && cursor < bytes);
            continue;
        }
        assert(kosdec_ahx_parser_feed(p, NULL, 0, true, &used, &frame) == result && !used);
        return result;
    }
}

static void sync_markers(void) {
    const uint8_t data[] = {0xff, 0xf0, 0x2f, 0xff, 0x18, 0x00, 0x10, 0x00, 0xca};
    size_t cursor = 0;
    assert(kosdec_ahx_scan_sync(data, sizeof(data), &cursor) == KOSDEC_AHX_SYNC_FRAME && cursor == 12);
    assert(kosdec_ahx_scan_sync(data, sizeof(data), &cursor) == KOSDEC_AHX_SYNC_FRAME && cursor == 32);
    assert(kosdec_ahx_scan_sync(data, sizeof(data), &cursor) == KOSDEC_AHX_SYNC_END && cursor == 68);
    assert(kosdec_ahx_scan_sync(data, sizeof(data), &cursor) == KOSDEC_AHX_SYNC_EXHAUSTED && cursor == 72);
    cursor = 8;
    assert(kosdec_ahx_scan_sync(NULL, 0, &cursor) == KOSDEC_AHX_SYNC_BAD_ARGUMENT && cursor == 8);
}

static void synthetic(void *workspace) {
    sync_markers();
    /* All 30 bands have their largest allocation, with SCFSI 0. */
    enum { MAX_CODED_FRAME = 846 };
    uint8_t dense[sizeof(header) + MAX_CODED_FRAME + sizeof(endcode)] = {0};
    memcpy(dense, header, sizeof(header));
    dense[12] = 0;
    dense[13] = 0;
    dense[14] = 4;
    dense[15] = 128;
    memcpy(dense + sizeof(header), empty_frame, 4);
    memset(dense + sizeof(header) + 4, 0xff, 9);
    dense[sizeof(header) + 13] = 0xe0;
    memcpy(dense + sizeof(header) + MAX_CODED_FRAME, endcode, sizeof(endcode));
    size_t dense_count = 0;
    assert(feed_until_end(workspace, dense, sizeof(dense), 1, &dense_count, false)
           == KOSDEC_AHX_DONE && dense_count == 1);

    uint8_t stream[STREAM_BYTES + 1];
    for(size_t window = 1; window <= STREAM_BYTES; ++window) {
        make_stream(stream);
        size_t count = 0;
        assert(feed_until_end(workspace, stream, STREAM_BYTES, window, &count, false) ==
               KOSDEC_AHX_DONE && count == 1);
    }
    for(size_t size = 0; size < STREAM_BYTES; ++size) {
        size_t count = 0;
        assert(feed_until_end(workspace, stream, size, 1, &count, false) == KOSDEC_AHX_TRUNCATED);
    }
    stream[STREAM_BYTES] = 0xff;
    size_t count = 0;
    assert(feed_until_end(workspace, stream, sizeof(stream), STREAM_BYTES, &count, false) ==
           KOSDEC_AHX_INVALID && count == 1);

    make_stream(stream);
    memmove(stream + sizeof(header) + sizeof(empty_frame),
            stream + sizeof(header) + sizeof(empty_frame) + 1, sizeof(endcode) - 1);
    count = 0;
    assert(feed_until_end(workspace, stream, STREAM_BYTES - 1, 1, &count, false) == KOSDEC_AHX_DONE);

    make_stream(stream);
    stream[4] = 0x11;
    count = 0;
    assert(feed_until_end(workspace, stream, STREAM_BYTES, 7, &count, false) == KOSDEC_AHX_UNSUPPORTED);
    make_stream(stream);
    stream[19] = 8;
    assert(feed_until_end(workspace, stream, STREAM_BYTES, 7, &count, false) == KOSDEC_AHX_UNSUPPORTED);
    make_stream(stream);
    stream[2] = stream[3] = 0xff;
    assert(feed_until_end(workspace, stream, STREAM_BYTES, 9, &count, false) == KOSDEC_AHX_UNSUPPORTED);
    /* A common extension with one opaque 20-byte record, followed by
       the six-byte tag at the end of the declared header. */
    enum { EXTENSION_BYTES = 24 };
    uint8_t extended[STREAM_BYTES + EXTENSION_BYTES];
    memcpy(extended, header, 20);
    extended[3] += EXTENSION_BYTES;
    memset(extended + 20, 0xa5, EXTENSION_BYTES);
    extended[22] = 0;
    extended[23] = 1;
    make_stream(stream);
    memcpy(extended + 20 + EXTENSION_BYTES, stream + 20, STREAM_BYTES - 20);
    struct kosdec_ahx_parser *p = kosdec_ahx_parser_init(workspace, kosdec_ahx_parser_workspace_size());
    struct kosdec_ahx_info info;
    struct kosdec_ahx_frame frame;
    size_t used;
    assert(kosdec_ahx_parser_feed(p, extended, sizeof(extended), true, &used, &frame)
           == KOSDEC_AHX_FRAME);
    assert(kosdec_ahx_parser_info(p, &info) && info.extension_bytes == EXTENSION_BYTES &&
           info.extension_records == 1 && !memcmp(info.extension, extended + 20, EXTENSION_BYTES));
    size_t tail = used;
    assert(kosdec_ahx_parser_feed(p, extended + tail, sizeof(extended) - tail, true, &used, &frame)
           == KOSDEC_AHX_DONE);
    assert(!memcmp(info.extension, extended + 20, EXTENSION_BYTES));

    extended[23] = 2;
    count = 0;
    assert(feed_until_end(workspace, extended, sizeof(extended), 3, &count, false) ==
           KOSDEC_AHX_INVALID && !count);

    make_stream(stream);
    stream[sizeof(header)] = 0;
    assert(feed_until_end(workspace, stream, STREAM_BYTES, 9, &count, false) == KOSDEC_AHX_INVALID);
    make_stream(stream);
    stream[STREAM_BYTES - 2] = 1;
    assert(feed_until_end(workspace, stream, STREAM_BYTES, 9, &count, false) == KOSDEC_AHX_INVALID);
}

static void fixture(void *workspace, const char *path) {
    FILE *in = fopen(path, "rb");
    assert(in && !fseek(in, 0, SEEK_END));
    long length = ftell(in);
    assert(length > 0 && length <= 2000000 && !fseek(in, 0, SEEK_SET));
    uint8_t *data = malloc((size_t)length);
    assert(data && fread(data, 1, (size_t)length, in) == (size_t)length && !fclose(in));
    printf("file %s\n", path);
    for(size_t window = 1; window <= 1024; window *= 32) {
        size_t count = 0;
        assert(feed_until_end(workspace, data, (size_t)length, window, &count, window == 1) ==
               KOSDEC_AHX_DONE && count);
    }
    free(data);
}

int main(int argc, char **argv) {
    size_t bytes = kosdec_ahx_parser_workspace_size();
    void *workspace = malloc(bytes);
    assert(workspace && !kosdec_ahx_parser_init(workspace, bytes - 1));
    synthetic(workspace);
    for(int i = 1; i < argc; ++i)
        fixture(workspace, argv[i]);
    free(workspace);
    puts("AHX PARSER PASS");
    return 0;
}
