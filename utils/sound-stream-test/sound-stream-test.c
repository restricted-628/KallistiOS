/* KallistiOS ##version##

   Host-side exact stream-tail tests.
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "snd_stream_split.h"

static void test_pcm16_tail(void) {
    const uint8_t source[] = {
        0x01, 0x02, 0x11, 0x12,
        0x03, 0x04, 0x13, 0x14,
        0x05, 0x06, 0x15, 0x16
    };
    uint8_t left[8] = {0};
    uint8_t right[8] = {0};
    const uint8_t expected_left[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t expected_right[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16};

    snd_stream_split_exact(16, source, left, right, sizeof(source));
    assert(!memcmp(left, expected_left, sizeof(expected_left)));
    assert(!memcmp(right, expected_right, sizeof(expected_right)));
    assert(left[6] == 0 && right[6] == 0);
}

static void test_pcm8_tail(void) {
    const uint8_t source[] = {1, 11, 2, 12, 3, 13};
    uint8_t left[5] = {0};
    uint8_t right[5] = {0};
    const uint8_t expected_left[] = {1, 2, 3};
    const uint8_t expected_right[] = {11, 12, 13};

    snd_stream_split_exact(8, source, left, right, sizeof(source));
    assert(!memcmp(left, expected_left, sizeof(expected_left)));
    assert(!memcmp(right, expected_right, sizeof(expected_right)));
    assert(left[3] == 0 && right[3] == 0);
}

static void test_adpcm_odd_tail(void) {
    const uint8_t source[] = {0xa1, 0xb2, 0xc3};
    uint8_t left[4] = {0};
    uint8_t right[4] = {0};

    snd_stream_split_exact(4, source, left, right, sizeof(source));
    assert(left[0] == 0xba);
    assert(right[0] == 0x21);
    assert(left[1] == 0x0c);
    assert(right[1] == 0x03);
    assert(left[2] == 0 && right[2] == 0);
}

int main(void) {
    test_pcm16_tail();
    test_pcm8_tail();
    test_adpcm_odd_tail();
    puts("Sound stream exact-tail tests passed");
    return 0;
}
