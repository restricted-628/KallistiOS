#ifndef MEDIA_AHX_FRAME_H
#define MEDIA_AHX_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum {
    AHX_BANDS = 30, AHX_SUBBANDS = 32, AHX_GRANULES = 12,
    AHX_ALLOCATION_END = 32 + 4 * 4 + 7 * 3 + 19 * 2
};

/* MPEG-2 Layer II low-rate mono allocation, ISO/IEC 13818-3 Annex B. */
static inline unsigned ahx_allocation_width(unsigned band) {
    return band < 4 ? 4 : band < 11 ? 3 : 2;
}

static inline unsigned ahx_quantizer(unsigned band, unsigned allocation) {
    return allocation + (band >= 4 && allocation >= 3);
}

static inline bool ahx_grouped(unsigned q) {
    return q == 1 || q == 2 || q == 4;
}

static inline unsigned ahx_code_bits(unsigned q) {
    if(q < 3)
        return q == 2 ? 7 : q * 5;
    if(q == 3)
        return 3;
    return q == 4 ? 10 : q - 1;
}

/* Call only after admitting the byte containing the last requested bit. */
static inline unsigned ahx_read_bits(const uint8_t *buffer, unsigned *cursor, unsigned width) {
    size_t offset = *cursor / 8;
    unsigned available = 8 - *cursor % 8;
    *cursor += width;
    unsigned value = buffer[offset];
    while(available < width) {
        value = value << 8 | buffer[++offset];
        available += 8;
    }
    return (value >> (available - width)) & ((1u << width) - 1);
}
#endif
