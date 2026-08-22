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

int vmufs_chain_allocate(const vmu_root_t *root, uint16_t *fat,
                         size_t fat_entries, uint8_t filetype,
                         size_t block_count, uint16_t *blocks,
                         size_t block_capacity) {
    size_t found = 0;

    if(!root || !fat || !blocks || block_count == 0 ||
       root->blk_cnt == 0 || root->blk_cnt > VMUFS_FAT_ENTRY_COUNT ||
       fat_entries < root->blk_cnt || block_count > root->blk_cnt ||
       block_capacity < block_count ||
       (filetype != VMUFS_FILETYPE_DATA &&
        filetype != VMUFS_FILETYPE_GAME)) {
        errno = EINVAL;
        return -1;
    }

    if(filetype == VMUFS_FILETYPE_GAME) {
        /* Executable images must occupy one contiguous prefix beginning at
           block zero. Total free space elsewhere cannot satisfy that layout. */
        for(size_t i = 0; i < block_count; ++i) {
            if(fat[i] != VMUFS_FAT_FREE) {
                errno = ENOSPC;
                return -1;
            }

            blocks[found++] = (uint16_t)i;
        }
    }
    else {
        /* Ordinary saves grow down from the high end, preserving as much of
           the executable-eligible block-zero prefix as possible. */
        for(size_t i = root->blk_cnt; i > 0 && found < block_count; --i) {
            size_t block = i - 1u;

            if(fat[block] == VMUFS_FAT_FREE)
                blocks[found++] = (uint16_t)block;
        }

        if(found != block_count) {
            errno = ENOSPC;
            return -1;
        }
    }

    /* Select the whole chain before changing the FAT. Failed allocation is
       therefore all-or-nothing in the caller's metadata snapshot. */
    for(size_t i = 0; i < block_count; ++i) {
        fat[blocks[i]] = i + 1u < block_count ?
            blocks[i + 1u] : VMUFS_FAT_EOF;
    }

    return 0;
}

void vmufs_chain_release(uint16_t *fat, const uint16_t *blocks,
                         size_t block_count) {
    if(!fat || !blocks)
        return;

    for(size_t i = 0; i < block_count; ++i)
        fat[blocks[i]] = VMUFS_FAT_FREE;
}

int vmufs_fat_reclaim_orphans(const vmu_root_t *root, uint16_t *fat,
                              size_t fat_entries, const vmu_dir_t *directory,
                              size_t directory_entries, size_t *reclaimed) {
    bool reachable[VMUFS_FAT_ENTRY_COUNT] = {false};
    uint16_t blocks[VMUFS_FAT_ENTRY_COUNT];
    size_t count = 0;

    if(reclaimed)
        *reclaimed = 0;
    if(!root || !fat || !directory || !reclaimed || root->blk_cnt == 0 ||
       root->blk_cnt > VMUFS_FAT_ENTRY_COUNT ||
       fat_entries < root->blk_cnt) {
        errno = EINVAL;
        return -1;
    }

    for(size_t i = 0; i < directory_entries; ++i) {
        const vmu_dir_t *entry = &directory[i];

        if(entry->filetype == 0)
            continue;
        if(vmufs_chain_collect(root, fat, fat_entries, entry, blocks,
                               VMUFS_FAT_ENTRY_COUNT) < 0)
            return -1;
        for(size_t j = 0; j < entry->filesize; ++j) {
            if(reachable[blocks[j]]) {
                errno = EILSEQ;
                return -1;
            }
            reachable[blocks[j]] = true;
        }
    }

    for(size_t i = 0; i < root->blk_cnt; ++i) {
        if(fat[i] != VMUFS_FAT_FREE && !reachable[i]) {
            fat[i] = VMUFS_FAT_FREE;
            ++count;
        }
    }
    *reclaimed = count;
    return 0;
}
