/* Copyright (C) 2026 Kyle Mayer */
#include <kosdec/ahx.h>
#include "silent.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(__sh__)
#include <fenv.h>
#endif

static uint32_t fp_environment(void) {
#if defined(__sh__)
    uint32_t fpscr;
    __asm__ volatile("sts fpscr,%0" : "=r"(fpscr));
    return fpscr;
#else
    return (uint32_t)fegetround();
#endif
}

static void synthetic(void *workspace, size_t bytes) {
    assert(!kosdec_ahx_decoder_init(workspace, bytes - 1));
    assert(!kosdec_ahx_decoder_init((uint8_t *)workspace + 1, bytes));
    struct kosdec_ahx_decoder *d = kosdec_ahx_decoder_init(workspace, bytes);
    struct kosdec_ahx_info info;
    assert(d && !kosdec_ahx_decoder_info(d, &info));
    assert(kosdec_ahx_decode(d, NULL, 1, false, NULL, 0).status == KOSDEC_AHX_BAD_ARGUMENT);
    struct kosdec_ahx_decode_result r = kosdec_ahx_decode(d, silent, sizeof(silent), true, NULL, 0);
    assert(r.status == KOSDEC_AHX_NEED_OUTPUT && r.consumed == 26 + 14 && !r.samples);
    assert(kosdec_ahx_decoder_info(d, &info));
    assert(info.rate == 22050 && info.declared_samples == 1 && info.header_bytes == 26);
    size_t used = r.consumed;
    for(size_t i = 0; i < KOSDEC_AHX_FRAME_SAMPLES; ++i) {
        int16_t sample = 1;
        uint32_t before = fp_environment();
        r = kosdec_ahx_decode(d, silent + used, sizeof(silent) - used, true, &sample, 1);
        assert(fp_environment() == before);
        assert(r.status == KOSDEC_AHX_MORE && !r.consumed && r.samples == 1 && sample == 0);
        assert(r.valid_samples == (i == 0));
    }
    r = kosdec_ahx_decode(d, silent + used, sizeof(silent) - used, true, NULL, 0);
    assert(r.status == KOSDEC_AHX_DONE && r.consumed == sizeof(silent) - used);
    assert(kosdec_ahx_decode(d, NULL, 0, true, NULL, 0).status == KOSDEC_AHX_DONE);
    d = kosdec_ahx_decoder_init(workspace, bytes);
    assert(d && !kosdec_ahx_decoder_info(d, &info));
    assert(kosdec_ahx_decode(d, silent, 22, true, NULL, 0).status == KOSDEC_AHX_TRUNCATED);

    enum { HEADER_BYTES = 26, BAD_GROUP_FRAME_BYTES = 22 };
    uint8_t bad[HEADER_BYTES + BAD_GROUP_FRAME_BYTES] = {0};
    for(size_t i = 0; i < HEADER_BYTES + 4; ++i)
        bad[i] = silent[i];
    bad[HEADER_BYTES + 4] = 0x10;  /* Band zero uses grouped base 3. */
    bad[HEADER_BYTES + 13] = 0x10; /* SCFSI 2. */
    bad[HEADER_BYTES + 14] = 0x1f; /* First grouped code exceeds 3^3 - 1. */
    d = kosdec_ahx_decoder_init(workspace, bytes);
    int16_t sentinel = 1234;
    r = kosdec_ahx_decode(d, bad, sizeof(bad), false, &sentinel, 1);
    assert(r.status == KOSDEC_AHX_INVALID && r.consumed == sizeof(bad) &&
           !r.samples && sentinel == 1234);
    assert(kosdec_ahx_decode(d, NULL, 0, true, &sentinel, 1).status == KOSDEC_AHX_INVALID);
}

static void compare(void *workspace, size_t bytes, const uint8_t *input, size_t input_bytes,
    const uint8_t *oracle, size_t oracle_bytes) {
    struct kosdec_ahx_decoder *d = kosdec_ahx_decoder_init(workspace, bytes);
    assert(d);
    size_t offset = 0, sample = 0, valid = 0;
    int16_t pcm[13];
    for(;;) {
        size_t remaining = input_bytes - offset;
        size_t offer = remaining < 37 ? remaining : 37;
        struct kosdec_ahx_decode_result r = kosdec_ahx_decode(d, input + offset, offer,
            offer == remaining, pcm, sizeof(pcm) / sizeof(*pcm));
        assert(r.consumed <= offer);
        offset += r.consumed;
        if(r.status == KOSDEC_AHX_DONE)
            break;
        if(r.status != KOSDEC_AHX_MORE && r.status != KOSDEC_AHX_NEED_INPUT) {
            fprintf(stderr, "decode status %d after %zu samples\n", r.status, sample);
            abort();
        }
        for(size_t i = 0; i < r.samples; ++i, ++sample) {
            assert(sample < oracle_bytes / 2);
            unsigned expected = oracle[2 * sample] | (unsigned)oracle[2 * sample + 1] << 8;
            if(expected != (uint16_t)pcm[i]) {
                fprintf(stderr, "sample %zu expected %04x got %04x\n",
                    sample, expected, (unsigned)(uint16_t)pcm[i]);
                abort();
            }
        }
        valid += r.valid_samples;
    }
    struct kosdec_ahx_info info;
    assert(kosdec_ahx_decoder_info(d, &info));
    assert(offset == input_bytes && info.declared_samples == valid && 2 * sample == oracle_bytes);
    printf("AHX PCM MATCH %zu samples (%zu valid) %" PRIu32 " Hz\n", sample, valid, info.rate);
}

#if defined(KOSDEC_AHX_EMBED_FIXTURES)
#include "ahx_fixture_data.h"
#else
static uint8_t *read_file(const char *name, size_t *size) {
    FILE *file = fopen(name, "rb");
    if(!file) {
        perror(name);
        abort();
    }
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0 && !fseek(file, 0, SEEK_SET));
    *size = (size_t)length;
    uint8_t *data = malloc(*size);
    assert(data && fread(data, 1, *size, file) == *size && !fclose(file));
    return data;
}

static void fixture(void *workspace, size_t bytes, const char *source, const char *expected) {
    size_t input_bytes, oracle_bytes;
    uint8_t *input = read_file(source, &input_bytes);
    uint8_t *oracle = read_file(expected, &oracle_bytes);
    printf("AHX FIXTURE %s\n", source);
    compare(workspace, bytes, input, input_bytes, oracle, oracle_bytes);
    free(input);
    free(oracle);
}
#endif

int main(int argc, char **argv) {
    size_t bytes = kosdec_ahx_decoder_workspace_size();
    void *workspace = malloc(bytes);
    assert(workspace);
    synthetic(workspace, bytes);
#if defined(KOSDEC_AHX_EMBED_FIXTURES)
    (void)argc;
    (void)argv;
    compare(workspace, bytes, speech_ahx, sizeof(speech_ahx), speech_pcm, sizeof(speech_pcm));
    compare(workspace, bytes, tail_ahx, sizeof(tail_ahx), tail_pcm, sizeof(tail_pcm));
#elif defined(__DREAMCAST__)
    (void)argc;
    (void)argv;
    fixture(workspace, bytes, "/cd/speech.ahx", "/cd/speech.pcm");
    fixture(workspace, bytes, "/cd/tail.ahx", "/cd/tail.pcm");
#else
    assert(argc % 2 == 1);
    for(int i = 1; i < argc; i += 2)
        fixture(workspace, bytes, argv[i], argv[i + 1]);
#endif
    free(workspace);
    puts("AHX DECODER PASS");
    return 0;
}
