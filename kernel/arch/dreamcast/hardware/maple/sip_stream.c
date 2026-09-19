/* KallistiOS ##version##

   sip_stream.c
   Copyright (C) 2026 Joseph Black
*/

#include <string.h>

#include "sip_stream_internal.h"

uint64_t _sip_ring_oldest(uint64_t write_position, size_t capacity) {
    if(write_position > capacity)
        return write_position - capacity;

    return 0;
}

size_t _sip_ring_available(uint64_t write_position, size_t capacity,
                           uint64_t *read_position, uint64_t *lost_bytes) {
    uint64_t oldest = _sip_ring_oldest(write_position, capacity);
    uint64_t available;

    if(*read_position < oldest) {
        if(lost_bytes)
            *lost_bytes += oldest - *read_position;

        *read_position = oldest;
    }
    else if(*read_position > write_position) {
        *read_position = write_position;
    }

    available = write_position - *read_position;
    return available > capacity ? capacity : (size_t)available;
}

void _sip_ring_write(uint8_t *ring, size_t capacity,
                     uint64_t write_position, const uint8_t *source,
                     size_t bytes) {
    size_t chunk;
    size_t offset;

    /* Only the newest capacity bytes can remain observable. Skipping older
       input also keeps each destination index tied to its absolute position. */
    if(bytes > capacity) {
        size_t skip = bytes - capacity;

        source += skip;
        write_position += skip;
        bytes = capacity;
    }

    while(bytes) {
        offset = (size_t)(write_position % capacity);
        chunk = capacity - offset;

        if(chunk > bytes)
            chunk = bytes;

        memcpy(ring + offset, source, chunk);
        source += chunk;
        write_position += chunk;
        bytes -= chunk;
    }
}

void _sip_ring_copy(const uint8_t *ring, size_t capacity,
                    uint64_t read_position, uint8_t *destination,
                    size_t bytes) {
    size_t chunk;
    size_t offset;

    while(bytes) {
        offset = (size_t)(read_position % capacity);
        chunk = capacity - offset;

        if(chunk > bytes)
            chunk = bytes;

        memcpy(destination, ring + offset, chunk);
        destination += chunk;
        read_position += chunk;
        bytes -= chunk;
    }
}
