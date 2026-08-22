/* KallistiOS ##version##

   vmufs_chain.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vmufs_internal.h"

#define VMUFS_FAT_ENTRY_COUNT (VMUFS_BLOCK_SIZE / sizeof(uint16_t))

int vmufs_chain_collect(const vmu_root_t *root, const uint16_t *fat,
                        size_t fat_entries, const vmu_dir_t *entry,
                        uint16_t *blocks, size_t block_capacity) {
    bool seen[VMUFS_FAT_ENTRY_COUNT] = {false};
    uint16_t block;

    if(!root || !fat || !entry || !blocks || root->blk_cnt == 0 ||
       root->blk_cnt > VMUFS_FAT_ENTRY_COUNT ||
       fat_entries < root->blk_cnt || block_capacity < entry->filesize) {
        errno = EINVAL;
        return -1;
    }

    if(entry->filesize == 0 || entry->filesize > root->blk_cnt) {
        errno = EILSEQ;
        return -1;
    }

    block = entry->firstblk;
    for(size_t i = 0; i < entry->filesize; ++i) {
        uint16_t next;

        if(block >= root->blk_cnt || seen[block]) {
            errno = EILSEQ;
            return -1;
        }

        seen[block] = true;
        blocks[i] = block;
        next = fat[block];

        if((i + 1u == entry->filesize && next != VMUFS_FAT_EOF) ||
           (i + 1u < entry->filesize && next >= root->blk_cnt)) {
            errno = EILSEQ;
            return -1;
        }

        block = next;
    }

    return 0;
}
