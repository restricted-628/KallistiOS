/* KallistiOS ##version##

   snd_stream_split.h
   Copyright (C) 2026 Joseph Black

   Exact tail-safe stereo separation shared with host validation.
*/

#ifndef __SND_STREAM_SPLIT_H
#define __SND_STREAM_SPLIT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void snd_stream_split_exact(int sample_bits, const void *data,
                                          void *left_buffer,
                                          void *right_buffer, size_t bytes) {
    const uint8_t *source = data;
    uint8_t *left = left_buffer;
    uint8_t *right = right_buffer;
    size_t frame;

    if(sample_bits == 16) {
        for(frame = 0; frame < bytes / 4; ++frame) {
            memcpy(left + frame * 2, source + frame * 4, 2);
            memcpy(right + frame * 2, source + frame * 4 + 2, 2);
        }
    }
    else if(sample_bits == 8) {
        for(frame = 0; frame < bytes / 2; ++frame) {
            left[frame] = source[frame * 2];
            right[frame] = source[frame * 2 + 1];
        }
    }
    else {
        /* Each source byte contains one left/right ADPCM frame. Pack two
           successive frames into one byte per channel. A final odd source
           byte leaves the destination high nibbles at their caller-zeroed
           values, producing silence without a source over-read. */
        for(frame = 0; frame < bytes; ++frame) {
            unsigned int shift = (frame & 1) ? 4 : 0;

            left[frame >> 1] |= ((source[frame] >> 4) & 0x0f) << shift;
            right[frame >> 1] |= (source[frame] & 0x0f) << shift;
        }
    }
}

#endif /* __SND_STREAM_SPLIT_H */
