/* KallistiOS ##version##

   sip_stream_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __DC_MAPLE_SIP_STREAM_INTERNAL_H
#define __DC_MAPLE_SIP_STREAM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

uint64_t _sip_ring_oldest(uint64_t write_position, size_t capacity);
size_t _sip_ring_available(uint64_t write_position, size_t capacity,
                           uint64_t *read_position, uint64_t *lost_bytes);
void _sip_ring_write(uint8_t *ring, size_t capacity,
                     uint64_t write_position, const uint8_t *source,
                     size_t bytes);
void _sip_ring_copy(const uint8_t *ring, size_t capacity,
                    uint64_t read_position, uint8_t *destination,
                    size_t bytes);

#endif /* __DC_MAPLE_SIP_STREAM_INTERNAL_H */
