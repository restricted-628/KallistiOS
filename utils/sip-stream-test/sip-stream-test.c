/* KallistiOS ##version##

   Host tests for the microphone capture ring.
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sip_stream_internal.h"

static void assert_bytes(const uint8_t *actual, const uint8_t *expected,
                         size_t bytes) {
    assert(memcmp(actual, expected, bytes) == 0);
}

int main(void) {
    uint8_t ring[8] = { 0 };
    uint8_t output[8] = { 0 };
    const uint8_t first[] = { 0, 1, 2, 3, 4, 5 };
    const uint8_t second[] = { 6, 7, 8, 9, 10, 11 };
    const uint8_t newest[] = {
        20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    };
    const uint8_t expected_wrapped[] = { 4, 5, 6, 7, 8, 9, 10, 11 };
    const uint8_t expected_newest[] = { 24, 25, 26, 27, 28, 29, 30, 31 };
    uint64_t read_position = 0;
    uint64_t lost = 0;
    size_t available;

    assert(_sip_ring_oldest(0, sizeof(ring)) == 0);
    assert(_sip_ring_oldest(8, sizeof(ring)) == 0);
    assert(_sip_ring_oldest(12, sizeof(ring)) == 4);

    _sip_ring_write(ring, sizeof(ring), 0, first, sizeof(first));
    _sip_ring_copy(ring, sizeof(ring), 0, output, sizeof(first));
    assert_bytes(output, first, sizeof(first));

    _sip_ring_write(ring, sizeof(ring), 6, second, sizeof(second));
    available = _sip_ring_available(12, sizeof(ring), &read_position, &lost);
    assert(available == sizeof(ring));
    assert(read_position == 4);
    assert(lost == 4);
    _sip_ring_copy(ring, sizeof(ring), read_position, output, available);
    assert_bytes(output, expected_wrapped, sizeof(expected_wrapped));

    _sip_ring_write(ring, sizeof(ring), 12, newest, sizeof(newest));
    read_position = 0;
    lost = 0;
    available = _sip_ring_available(24, sizeof(ring), &read_position, &lost);
    assert(available == sizeof(ring));
    assert(read_position == 16);
    assert(lost == 16);
    _sip_ring_copy(ring, sizeof(ring), read_position, output, available);
    assert_bytes(output, expected_newest, sizeof(expected_newest));

    read_position = 30;
    available = _sip_ring_available(24, sizeof(ring), &read_position, NULL);
    assert(available == 0);
    assert(read_position == 24);

    puts("SIP stream ring tests passed");
    return 0;
}
