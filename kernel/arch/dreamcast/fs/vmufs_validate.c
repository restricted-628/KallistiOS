/* KallistiOS ##version##

   vmufs_validate.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vmufs_internal.h"

#define VMUFS_NO_OWNER SIZE_MAX
#define VMUFS_NO_ENTRY SIZE_MAX

_Static_assert(sizeof(vmu_root_t) == VMUFS_BLOCK_SIZE,
               "vmu_root_t must describe one VMU block");
_Static_assert(sizeof(vmu_dir_t) == 32,
               "vmu_dir_t must describe one directory entry");

static void record_error(vmufs_validation_t *result,
                         vmufs_validation_error_t error,
                         size_t dir_index, uint16_t block) {
    if(result->first_error == VMUFS_VALIDATION_OK) {
        result->first_error = error;
        result->first_dir_index = dir_index;
        result->first_block = block;
    }

    result->error_count++;
}

int vmufs_root_validate_at(const vmu_root_t *root, size_t card_blocks,
                           size_t root_block) {
    size_t dir_first;

    if(!root || card_blocks == 0 || card_blocks > UINT16_MAX + 1u ||
       root_block >= card_blocks) {
        errno = EINVAL;
        return -1;
    }

    for(size_t i = 0; i < sizeof(root->magic); ++i) {
        if(root->magic[i] != 0x55) {
            errno = EILSEQ;
            return -1;
        }
    }

    if(root->use_custom > 1 || root->blk_cnt == 0 ||
       root->blk_cnt > card_blocks || root->fat_size == 0 ||
       root->dir_size == 0 || root->fat_loc >= card_blocks ||
       root->dir_loc >= card_blocks || root->dir_size > root->dir_loc + 1u) {
        errno = EILSEQ;
        return -1;
    }

    /* KOS currently transfers one complete FAT block per operation. Refuse a
       larger geometry instead of allocating it and silently using only block 0. */
    if(root->fat_size != 1 || root->blk_cnt > VMUFS_BLOCK_SIZE / sizeof(uint16_t)) {
        errno = ENOTSUP;
        return -1;
    }

    dir_first = root->dir_loc + 1u - root->dir_size;

    /* User data must end before metadata, and the FAT must not overlap the
       backwards-growing directory extent. The physical root block is supplied
       separately because corrupt on-card geometry cannot be trusted to reserve
       the block from which that geometry was read. */
    if(root->fat_loc < root->blk_cnt || dir_first < root->blk_cnt ||
       (root->fat_loc >= dir_first && root->fat_loc <= root->dir_loc) ||
       root_block < root->blk_cnt || root->fat_loc == root_block ||
       (root_block >= dir_first && root_block <= root->dir_loc)) {
        errno = EILSEQ;
        return -1;
    }

    return 0;
}

int vmufs_root_validate(const vmu_root_t *root, size_t card_blocks) {
    return vmufs_root_validate_at(root, card_blocks,
                                  VMUFS_STANDARD_ROOT_BLOCK);
}

size_t vmufs_fat_free_executable(const vmu_root_t *root,
                                 const uint16_t *fat,
                                 size_t fat_entries) {
    size_t count = 0;

    if(!root || !fat || fat_entries < root->blk_cnt)
        return 0;

    while(count < root->blk_cnt && fat[count] == VMUFS_FAT_FREE)
        ++count;

    return count;
}

int vmufs_validate_at(const vmu_root_t *root, size_t card_blocks,
                      size_t root_block, const uint16_t *fat,
                      size_t fat_entries, const vmu_dir_t *dir,
                      size_t dir_entries, vmufs_validation_t *result) {
    size_t required_dir_entries;
    size_t *owner = NULL;
    uint32_t *seen = NULL;
    uint32_t epoch = 0;

    if(result) {
        memset(result, 0, sizeof(*result));
        result->first_dir_index = VMUFS_NO_ENTRY;
        result->first_block = UINT16_MAX;
    }

    if(!root || !fat || !dir || !result || card_blocks == 0 ||
       card_blocks > UINT16_MAX + 1u || root_block >= card_blocks) {
        if(result) {
            record_error(result, VMUFS_VALIDATION_INVALID_ARGUMENT,
                         VMUFS_NO_ENTRY, UINT16_MAX);
        }
        errno = EINVAL;
        return -1;
    }

    if(vmufs_root_validate_at(root, card_blocks, root_block) < 0) {
        vmufs_validation_error_t error = errno == ENOTSUP ?
            VMUFS_VALIDATION_UNSUPPORTED_FAT :
            VMUFS_VALIDATION_BAD_ROOT_GEOMETRY;

        if(errno == EILSEQ) {
            bool bad_magic = false;

            for(size_t i = 0; i < sizeof(root->magic); ++i)
                bad_magic |= root->magic[i] != 0x55;

            if(bad_magic)
                error = VMUFS_VALIDATION_BAD_ROOT_MAGIC;
        }

        record_error(result, error, VMUFS_NO_ENTRY, UINT16_MAX);
        return -1;
    }

    required_dir_entries = root->dir_size * VMUFS_BLOCK_SIZE /
                           sizeof(vmu_dir_t);

    if(fat_entries < root->blk_cnt || dir_entries < required_dir_entries) {
        record_error(result, VMUFS_VALIDATION_INVALID_ARGUMENT,
                     VMUFS_NO_ENTRY, UINT16_MAX);
        errno = EINVAL;
        return -1;
    }

    owner = malloc(root->blk_cnt * sizeof(*owner));
    seen = calloc(root->blk_cnt, sizeof(*seen));
    if(!owner || !seen) {
        free(owner);
        free(seen);
        errno = ENOMEM;
        return -1;
    }

    for(size_t i = 0; i < root->blk_cnt; ++i)
        owner[i] = VMUFS_NO_OWNER;

    for(size_t i = 0; i < required_dir_entries; ++i) {
        uint16_t block;
        bool chain_ok = true;

        if(dir[i].filetype == 0) {
            result->free_dir_entries++;
            continue;
        }

        result->file_count++;

        if(dir[i].filetype != VMUFS_FILETYPE_DATA &&
           dir[i].filetype != VMUFS_FILETYPE_GAME) {
            record_error(result, VMUFS_VALIDATION_BAD_FILE_TYPE,
                         i, dir[i].firstblk);
        }

        for(size_t j = 0; j < i; ++j) {
            if(dir[j].filetype &&
               memcmp(dir[i].filename, dir[j].filename,
                      sizeof(dir[i].filename)) == 0) {
                record_error(result, VMUFS_VALIDATION_DUPLICATE_NAME,
                             i, dir[i].firstblk);
                break;
            }
        }

        if(dir[i].filesize == 0) {
            record_error(result, VMUFS_VALIDATION_EMPTY_FILE,
                         i, dir[i].firstblk);
            continue;
        }

        if(dir[i].filesize > root->blk_cnt) {
            record_error(result, VMUFS_VALIDATION_CHAIN_TOO_LONG,
                         i, dir[i].firstblk);
            continue;
        }

        ++epoch;
        block = dir[i].firstblk;

        for(size_t step = 0; step < dir[i].filesize; ++step) {
            uint16_t next;

            if(block >= root->blk_cnt) {
                vmufs_validation_error_t error =
                    block == VMUFS_FAT_FREE || block == VMUFS_FAT_EOF ?
                    VMUFS_VALIDATION_CHAIN_EARLY_END :
                    VMUFS_VALIDATION_CHAIN_OUT_OF_RANGE;
                record_error(result, error, i, block);
                chain_ok = false;
                break;
            }

            if(seen[block] == epoch) {
                record_error(result, VMUFS_VALIDATION_CHAIN_CYCLE, i, block);
                chain_ok = false;
                break;
            }

            if(owner[block] != VMUFS_NO_OWNER) {
                record_error(result, VMUFS_VALIDATION_CHAIN_CROSSLINK,
                             i, block);
                chain_ok = false;
                break;
            }

            seen[block] = epoch;
            owner[block] = i;
            result->reachable_blocks++;
            next = fat[block];

            if(step + 1u == dir[i].filesize) {
                if(next != VMUFS_FAT_EOF) {
                    record_error(result, VMUFS_VALIDATION_CHAIN_TOO_LONG,
                                 i, block);
                    chain_ok = false;
                }
            }
            else if(next == VMUFS_FAT_FREE || next == VMUFS_FAT_EOF) {
                record_error(result, VMUFS_VALIDATION_CHAIN_EARLY_END,
                             i, block);
                chain_ok = false;
            }

            if(!chain_ok)
                break;

            block = next;
        }
    }

    for(size_t block = 0; block < root->blk_cnt; ++block) {
        if(fat[block] == VMUFS_FAT_FREE) {
            result->free_blocks++;
        }
        else {
            result->used_blocks++;

            if(owner[block] == VMUFS_NO_OWNER) {
                result->orphan_blocks++;
                record_error(result, VMUFS_VALIDATION_ORPHAN_BLOCK,
                             VMUFS_NO_ENTRY, (uint16_t)block);
            }
        }
    }

    result->executable_free_blocks =
        vmufs_fat_free_executable(root, fat, fat_entries);

    free(owner);
    free(seen);

    if(result->error_count) {
        errno = EILSEQ;
        return -1;
    }

    return 0;
}

int vmufs_validate(const vmu_root_t *root, size_t card_blocks,
                   const uint16_t *fat, size_t fat_entries,
                   const vmu_dir_t *dir, size_t dir_entries,
                   vmufs_validation_t *result) {
    return vmufs_validate_at(root, card_blocks, VMUFS_STANDARD_ROOT_BLOCK,
                             fat, fat_entries, dir, dir_entries, result);
}

bool vmufs_validation_allows_mutation(
    const vmufs_validation_t *result) {
    if(!result)
        return false;

    if(result->error_count == 0)
        return true;

    /* Orphans consume capacity but are not referenced by a live directory
       entry. They can safely remain allocated until an explicit repair pass;
       every other inconsistency can make releasing or reusing blocks unsafe. */
    return result->first_error == VMUFS_VALIDATION_ORPHAN_BLOCK &&
           result->orphan_blocks != 0 &&
           result->error_count == result->orphan_blocks;
}
