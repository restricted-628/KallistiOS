/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Shared by the host asset generator and the target readback check.
*/
#ifndef TEXTURE_PATTERN_H
#define TEXTURE_PATTERN_H
#include <stdint.h>

#define TEXTURE_WIDTH 128u
#define TEXTURE_HEIGHT 128u
#define TEXTURE_BYTES (TEXTURE_WIDTH * TEXTURE_HEIGHT * 2u)

static inline uint16_t texture_pixel(unsigned int x, unsigned int y) {
    /* White border/diagonals, then red, green, blue, yellow quadrants. */
    static const uint16_t colors[4] = { 0xf800, 0x07e0, 0x001f, 0xffe0 };
    if(x == 0 || y == 0 || x == TEXTURE_WIDTH - 1
            || y == TEXTURE_HEIGHT - 1 || x == y
            || x + y == TEXTURE_WIDTH - 1)
        return 0xffff;
    return colors[(y >= TEXTURE_HEIGHT / 2) * 2 + (x >= TEXTURE_WIDTH / 2)];
}
#endif
