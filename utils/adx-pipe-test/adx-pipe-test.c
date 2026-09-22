/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Synthetic, asset-free SPSC and backpressure tests.
*/
#include <dc/sound/adx_pipe.h>
#include <dc/sound/adx_input.h>
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES 65539u
static uint8_t encoded[36 + ((FRAMES + 31) / 32) * 36];
static int16_t expected[FRAMES * 2], actual[FRAMES * 2];

static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (24 - 8 * i));
}

static size_t fixture(unsigned channels, unsigned frames) {
    memset(encoded, 0, sizeof(encoded));
    encoded[0] = 0x80; encoded[3] = 32;
    encoded[4] = 3; encoded[5] = 18; encoded[6] = 4;
    encoded[7] = (uint8_t)channels;
    put32(encoded + 8, 44100); put32(encoded + 12, frames);
    encoded[16] = 1; encoded[17] = 244; encoded[18] = 5;
    memcpy(encoded + 30, "(c)CRI", 6);
    size_t bytes = 36 + ((frames + 31) / 32) * 18 * channels;
    for(size_t b = 36; b < bytes; b += 18) {
        encoded[b + 1] = (uint8_t)(b % 251);
        for(size_t i = 2; i < 18; ++i)
            encoded[b + i] = (uint8_t)(b * 7 + i * 29);
    }
    return bytes;
}

static void reference(size_t bytes, unsigned channels) {
    snd_adx_decoder_t d;
    snd_adx_init(&d);
    size_t in = 0, out = 0;
    for(;;) {
        snd_adx_result_t r = snd_adx_decode(&d, encoded + in, bytes - in,
                                          expected + out * channels, 32, true);
        in += r.consumed; out += r.frames;
        if(r.status == SND_ADX_DONE)
            break;
        assert(r.status == SND_ADX_MORE);
    }
}

static void controls(void) {
    snd_adx_pipe_t p;
    int16_t storage[128], scratch[64];
    assert(snd_adx_pipe_init(NULL, storage, 128, 64) < 0);
    assert(snd_adx_pipe_init(&p, NULL, 128, 64) < 0);
    assert(snd_adx_pipe_init(&p, storage, 127, 64) < 0);
    assert(snd_adx_pipe_init(&p, storage, 128, 63) < 0);
    assert(snd_adx_pipe_init(&p, storage, 128, 16) < 0);
    assert(snd_adx_pipe_init(&p, storage, 128, 64) == 0);
    snd_adx_info_t info = {0};
    assert(!snd_adx_pipe_get_info(&p, &info));
    snd_adx_pipe_result_t read = snd_adx_pipe_read(&p, scratch, 32);
    assert(read.starved && !read.drained && !read.frames);

    size_t bytes = fixture(2, 97);
    snd_adx_result_t r = snd_adx_pipe_decode(&p, encoded, bytes, true);
    assert(r.consumed == 36 && !r.frames);
    assert(snd_adx_pipe_get_info(&p, &info) && info.channels == 2);
    r = snd_adx_pipe_decode(&p, encoded + 36, bytes - 36, true);
    assert(r.frames == 32);
    r = snd_adx_pipe_decode(&p, encoded + 72, bytes - 72, true);
    assert(r.frames == 32);
    uint8_t snapshot[sizeof(p)];
    memcpy(snapshot, &p, sizeof(p));
    r = snd_adx_pipe_decode(&p, encoded + 108, bytes - 108, true);
    assert(r.status == SND_ADX_NEED_OUTPUT && !r.consumed);
    assert(!memcmp(snapshot, &p, sizeof(p)));
    read = snd_adx_pipe_read(&p, scratch, 1);
    assert(read.frames == 1 && read.queued_frames == 63);
    r = snd_adx_pipe_decode(&p, encoded + 108, bytes - 108, true);
    assert(r.status == SND_ADX_NEED_OUTPUT);
    read = snd_adx_pipe_read(&p, scratch, 31);
    assert(read.frames == 31);
    r = snd_adx_pipe_decode(&p, encoded + 108, bytes - 108, true);
    assert(r.frames == 32);
    snd_adx_pipe_request_cancel(&p);
    read = snd_adx_pipe_read(&p, NULL, 0);
    assert(read.producer == SND_ADX_MORE && !read.drained);
    r = snd_adx_pipe_decode(&p, NULL, 0, false);
    assert(r.status == SND_ADX_CANCELLED);
    read = snd_adx_pipe_read(&p, scratch, 32);
    assert(read.producer == SND_ADX_CANCELLED && !read.drained && !read.starved);
    read = snd_adx_pipe_read(&p, scratch, 32);
    assert(read.drained && read.frames == 32);
    assert(snd_adx_pipe_read(&p, NULL, 0).drained);
    assert(snd_adx_pipe_read(&p, NULL, 1).producer == SND_ADX_BAD_ARGUMENT);

    /* Header errors and truncated blocks publish closure, not fake PCM. */
    snd_adx_pipe_init(&p, storage, 128, 64);
    r = snd_adx_pipe_decode(&p, encoded, 12, true);
    assert(r.status == SND_ADX_TRUNCATED);
    assert(snd_adx_pipe_read(&p, scratch, 32).drained);
    snd_adx_pipe_init(&p, storage, 128, 64);
    snd_adx_pipe_decode(&p, encoded, 36, false);
    r = snd_adx_pipe_decode(&p, encoded + 36, 10, true);
    assert(r.status == SND_ADX_TRUNCATED);
    read = snd_adx_pipe_read(&p, scratch, 32);
    assert(read.drained && !read.frames);
}

typedef struct context {
    snd_adx_pipe_t pipe;
    int16_t ring[256];
    size_t bytes;
    unsigned channels;
    bool expect_cancel;
} context_t;

static void *produce(void *opaque) {
    context_t *c = opaque;
    size_t pos = 0;
    for(;;) {
        size_t take = c->bytes - pos;
        if(take > 23)
            take = 23;
        snd_adx_result_t r = snd_adx_pipe_decode(&c->pipe, encoded + pos,
                                                take, pos + take == c->bytes);
        pos += r.consumed;
        if(r.status >= SND_ADX_DONE) {
            assert(r.status == (c->expect_cancel ? SND_ADX_CANCELLED : SND_ADX_DONE));
            if(!c->expect_cancel)
                assert(pos == c->bytes);
            return NULL;
        }
        assert(r.status <= SND_ADX_NEED_OUTPUT);
        sched_yield();
    }
}

static void concurrent(unsigned channels, unsigned capacity, bool wrap) {
    context_t c;
    c.bytes = fixture(channels, FRAMES);
    c.channels = channels;
    c.expect_cancel = false;
    reference(c.bytes, channels);
    assert(!snd_adx_pipe_init(&c.pipe, c.ring, 256, capacity));
    if(wrap) {
        /* White-box setup before sharing: exercise uint32 cursor rollover. */
        c.pipe.write_cursor = UINT32_MAX - 31;
        c.pipe.read_cursor = UINT32_MAX - 31;
    }
    pthread_t producer;
    assert(!pthread_create(&producer, NULL, produce, &c));
    size_t done = 0;
    for(;;) {
        int16_t scratch[34];
        snd_adx_pipe_result_t r = snd_adx_pipe_read(&c.pipe, scratch, 17);
        assert(done + r.frames <= FRAMES);
        memcpy(actual + done * channels, scratch, r.frames * channels * 2);
        done += r.frames;
        if(r.drained) {
            assert(r.producer == SND_ADX_DONE);
            break;
        }
        sched_yield();
    }
    assert(!pthread_join(producer, NULL));
    assert(done == FRAMES && !memcmp(actual, expected, FRAMES * channels * 2));
    snd_adx_pipe_request_cancel(&c.pipe);
    assert(snd_adx_pipe_decode(&c.pipe, NULL, 0, true).status == SND_ADX_DONE);
}

static void cancel_concurrent(void) {
    context_t c;
    c.bytes = fixture(2, FRAMES);
    c.channels = 2;
    c.expect_cancel = true;
    assert(!snd_adx_pipe_init(&c.pipe, c.ring, 256, 32));
    pthread_t producer;
    assert(!pthread_create(&producer, NULL, produce, &c));
    /* No consumption: the producer must stall at one full group. */
    while(snd_adx_pipe_read(&c.pipe, NULL, 0).queued_frames != 32)
        sched_yield();
    snd_adx_pipe_request_cancel(&c.pipe);
    assert(!pthread_join(producer, NULL));
    snd_adx_pipe_result_t r = snd_adx_pipe_read(&c.pipe, NULL, 0);
    assert(r.producer == SND_ADX_CANCELLED && !r.drained);
    int16_t pcm[64];
    r = snd_adx_pipe_read(&c.pipe, pcm, 32);
    assert(r.producer == SND_ADX_CANCELLED && r.frames == 32 && r.drained);
}

static void input_edges(void) {
    uint8_t storage[128];
    int16_t pcm[64], out[64];
    snd_adx_input_t q;
    snd_adx_pipe_t p;
    assert(snd_adx_input_init(NULL, storage, 128) < 0);
    assert(snd_adx_input_init(&q, NULL, 128) < 0);
    assert(snd_adx_input_init(&q, storage, 127) < 0);
    assert(snd_adx_input_init(&q, storage, 16) < 0);
    size_t bytes = fixture(2, 33);
    for(size_t length = 0; length <= bytes; ++length)
    for(size_t fragment = 1; fragment <= 37; fragment += 6) {
        snd_adx_input_init(&q, storage, 32);
        snd_adx_pipe_init(&p, pcm, 64, 32);
        q.write_cursor = q.read_cursor = UINT32_MAX - 6;
        size_t loaded = 0, produced = 0;
        for(size_t steps = 0; steps < 1000; ++steps) {
            size_t take = length - loaded;
            if(take > fragment)
                take = fragment;
            loaded += snd_adx_input_write(&q, encoded + loaded, take);
            if(loaded == length)
                assert(!snd_adx_input_close(&q, SND_ADX_INPUT_EOF));
            snd_adx_input_result_t r = snd_adx_input_step(&q, &p);
            snd_adx_pipe_result_t read = snd_adx_pipe_read(&p, out, 32);
            produced += read.frames;
            if(r.codec.status >= SND_ADX_DONE) {
                assert(r.codec.status == (length == bytes ? SND_ADX_DONE : SND_ADX_TRUNCATED));
                assert(produced == (length == bytes ? 33u : length >= 72 ? 32u : 0u));
                break;
            }
            assert(steps < 999);
        }
    }
    /* Closed input with two already-buffered physical spans must not park. */
    bytes = fixture(1, 1);
    snd_adx_input_init(&q, storage, 128);
    snd_adx_pipe_init(&p, pcm, 64, 32);
    q.write_cursor = q.read_cursor = 123;
    assert(snd_adx_input_write(&q, encoded, bytes) == bytes);
    assert(!snd_adx_input_close(&q, SND_ADX_INPUT_EOF));
    assert(snd_adx_input_close(&q, SND_ADX_INPUT_FAILED) < 0);
    assert(snd_adx_input_write(&q, encoded, 1) == 0);
    snd_adx_input_result_t r = snd_adx_input_step(&q, &p);
    assert(r.codec.status == SND_ADX_MORE && r.codec.consumed == 5);
    r = snd_adx_input_step(&q, &p);
    assert(r.codec.status == SND_ADX_MORE && p.decoder.header_ready);
    r = snd_adx_input_step(&q, &p);
    assert(r.codec.status == SND_ADX_DONE && r.codec.frames == 1);

    /* An I/O error is observable separately, not a successful/truncated EOF. */
    snd_adx_input_init(&q, storage, 128);
    snd_adx_pipe_init(&p, pcm, 64, 32);
    assert(snd_adx_input_close(&q, SND_ADX_INPUT_OPEN) < 0);
    assert(!snd_adx_input_close(&q, SND_ADX_INPUT_FAILED));
    r = snd_adx_input_step(&q, &p);
    assert(r.input == SND_ADX_INPUT_FAILED && r.codec.status == SND_ADX_CANCELLED);
    assert(snd_adx_input_step(NULL, &p).codec.status == SND_ADX_BAD_ARGUMENT);

    /* Source failure while PCM is full must acknowledge without losing PCM. */
    fixture(2, 33);
    snd_adx_input_init(&q, storage, 128);
    snd_adx_pipe_init(&p, pcm, 64, 32);
    assert(snd_adx_input_write(&q, encoded, 72) == 72);
    assert(snd_adx_input_step(&q, &p).codec.status == SND_ADX_MORE);
    assert(snd_adx_input_step(&q, &p).codec.frames == 32);
    assert(!snd_adx_input_close(&q, SND_ADX_INPUT_FAILED));
    r = snd_adx_input_step(&q, &p);
    assert(r.input == SND_ADX_INPUT_FAILED && r.codec.status == SND_ADX_CANCELLED);
    snd_adx_pipe_result_t read = snd_adx_pipe_read(&p, out, 32);
    assert(read.frames == 32 && read.drained && read.producer == SND_ADX_CANCELLED);

    /* A late loader error remains visible but cannot rewrite codec DONE. */
    bytes = fixture(1, 1);
    snd_adx_input_init(&q, storage, 128);
    snd_adx_pipe_init(&p, pcm, 64, 32);
    assert(snd_adx_input_write(&q, encoded, bytes) == bytes);
    assert(snd_adx_input_step(&q, &p).codec.status == SND_ADX_MORE);
    assert(snd_adx_input_step(&q, &p).codec.status == SND_ADX_DONE);
    assert(!snd_adx_input_close(&q, SND_ADX_INPUT_FAILED));
    r = snd_adx_input_step(&q, &p);
    assert(r.input == SND_ADX_INPUT_FAILED && r.codec.status == SND_ADX_DONE);
}

typedef struct input_context {
    context_t output;
    snd_adx_input_t input;
    uint8_t storage[128];
    size_t fragment;
} input_context_t;

static void *load_bytes(void *opaque) {
    input_context_t *c = opaque;
    size_t pos = 0;
    while(pos != c->output.bytes) {
        size_t take = c->output.bytes - pos;
        if(take > c->fragment)
            take = c->fragment;
        pos += snd_adx_input_write(&c->input, encoded + pos, take);
        sched_yield();
    }
    assert(!snd_adx_input_close(&c->input, SND_ADX_INPUT_EOF));
    return NULL;
}

static void *decode_bytes(void *opaque) {
    input_context_t *c = opaque;
    for(;;) {
        snd_adx_input_result_t r = snd_adx_input_step(&c->input, &c->output.pipe);
        if(r.codec.status >= SND_ADX_DONE) {
            assert(r.codec.status == SND_ADX_DONE);
            return NULL;
        }
        sched_yield();
    }
}

static void input_concurrent(unsigned channels, size_t fragment) {
    input_context_t c;
    c.output.bytes = fixture(channels, FRAMES);
    c.fragment = fragment;
    reference(c.output.bytes, channels);
    snd_adx_input_init(&c.input, c.storage, 128);
    snd_adx_pipe_init(&c.output.pipe, c.output.ring, 256, 64);
    c.input.write_cursor = c.input.read_cursor = UINT32_MAX - 21;
    pthread_t loader, decoder;
    assert(!pthread_create(&loader, NULL, load_bytes, &c));
    assert(!pthread_create(&decoder, NULL, decode_bytes, &c));
    size_t frames = 0;
    for(;;) {
        int16_t out[34];
        snd_adx_pipe_result_t r = snd_adx_pipe_read(&c.output.pipe, out, 17);
        assert(frames + r.frames <= FRAMES);
        memcpy(actual + frames * channels, out, r.frames * channels * 2);
        frames += r.frames;
        if(r.drained) {
            assert(r.producer == SND_ADX_DONE);
            break;
        }
        sched_yield();
    }
    assert(!pthread_join(loader, NULL));
    assert(!pthread_join(decoder, NULL));
    assert(frames == FRAMES && !memcmp(actual, expected, FRAMES * channels * 2));
}

int main(void) {
    controls();
    for(unsigned channels = 1; channels <= 2; ++channels)
    for(unsigned capacity = 32; capacity <= 128; capacity *= 2) {
        concurrent(channels, capacity, false);
        concurrent(channels, capacity, true);
    }
    cancel_concurrent();
    input_edges();
    for(unsigned channels = 1; channels <= 2; ++channels) {
        input_concurrent(channels, 3);
        input_concurrent(channels, 137);
    }
    puts("ADX pipeline: input/PCM backpressure, EOF, cancellation, three-thread wrap PASS");
    return 0;
}
