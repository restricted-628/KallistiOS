/* KallistiOS ##version##

   utils.h
   Copyright (C) 2012 Lawrence Sebald
*/

#ifndef __EXT2_UTILS_H
#define __EXT2_UTILS_H

#include <sys/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>

uint32_t ext2_bit_find_nonzero(const uint32_t *btbl, uint32_t start,
                               uint32_t end);
uint32_t ext2_bit_find_zero(const uint32_t *btbl, uint32_t start, uint32_t end);

/* This was a macro, originally. However, that relies on a GCC extension, so I
   made it an inline function instead. */
static inline int ext2_bit_is_set(const uint32_t *btbl, uint32_t bit_num) {
    int byte = (bit_num >> 5);
    int bit = (bit_num & 0x1F);
    return btbl[byte] & (1 << bit);
}

static inline void ext2_bit_set(uint32_t *btbl, uint32_t bit_num) {
    int byte = (bit_num >> 5);
    int bit = (bit_num & 0x1F);
    btbl[byte] |= (1 << bit);
}

static inline void ext2_bit_clear(uint32_t *btbl, uint32_t bit_num) {
    int byte = (bit_num >> 5);
    int bit = (bit_num & 0x1F);
    btbl[byte] &= ~(1 << bit);
}

__END_DECLS

#endif /* !__EXT2_UTILS_H */
