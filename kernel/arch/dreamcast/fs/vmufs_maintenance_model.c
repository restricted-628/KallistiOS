/* KallistiOS ##version##

   vmufs_maintenance_model.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vmufs_internal.h"

static const uint8_t standard_root_control_1[6] = {
    0xff, 0x00, 0x00, 0x00, 0xff, 0x00
};

static const uint8_t standard_root_control_2[6] = {
    0x1f, 0x00, 0x00, 0x00, 0x80, 0x00
};

int vmufs_format_build(const vmufs_format_options_t *options,
                       vmu_root_t *root, uint16_t *fat,
                       size_t fat_entries) {
    const size_t standard_fat_entries =
        VMUFS_BLOCK_SIZE / sizeof(*fat);

    if(!options || !root || !fat || fat_entries < standard_fat_entries ||
       options->use_custom_color > 1 || options->icon_shape > 123 ||
       (options->use_custom_color && options->custom_color[3] < 128)) {
        errno = EINVAL;
        return -1;
    }

    if(!options->use_custom_color) {
        for(size_t i = 0; i < sizeof(options->custom_color); ++i) {
            if(options->custom_color[i] != 0) {
                errno = EINVAL;
                return -1;
            }
        }
    }

    memset(root, 0, sizeof(*root));
    memset(root->magic, 0x55, sizeof(root->magic));
    root->use_custom = options->use_custom_color;
    if(options->use_custom_color) {
        memcpy(root->custom_color, options->custom_color,
               sizeof(root->custom_color));
    }
    root->timestamp = options->timestamp;

    /* These bytes are part of the canonical 128 KiB format image. Keep the
       values explicit without assigning semantics to reserved fields. */
    memcpy(root->unk1, standard_root_control_1, sizeof(root->unk1));
    root->fat_loc = VMUFS_STANDARD_FAT_BLOCK;
    root->fat_size = 1;
    root->dir_loc = VMUFS_STANDARD_DIR_BLOCK;
    root->dir_size = VMUFS_STANDARD_DIR_BLOCKS;
    root->icon_shape = options->icon_shape;
    root->blk_cnt = VMUFS_STANDARD_USER_BLOCKS;
    memcpy(root->unk2, standard_root_control_2,
           sizeof(standard_root_control_2));

    for(size_t i = 0; i < standard_fat_entries; ++i)
        fat[i] = VMUFS_FAT_FREE;

    /* Directory blocks form a backwards chain; the FAT and root each end
       their own one-block metadata chains. */
    fat[241] = VMUFS_FAT_EOF;
    for(size_t block = 242; block <= VMUFS_STANDARD_DIR_BLOCK; ++block)
        fat[block] = (uint16_t)(block - 1u);
    fat[VMUFS_STANDARD_FAT_BLOCK] = VMUFS_FAT_EOF;
    fat[VMUFS_STANDARD_ROOT_BLOCK] = VMUFS_FAT_EOF;

    return 0;
}

static int append_entry(const vmu_root_t *root, const uint16_t *old_fat,
                        size_t fat_entries, vmu_dir_t *entry,
                        vmufs_defrag_plan_t *plan, uint16_t *next_target,
                        bool descending) {
    uint16_t blocks[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    size_t first = plan->live_blocks;

    if(vmufs_chain_collect(root, old_fat, fat_entries, entry, blocks,
                           sizeof(blocks) / sizeof(blocks[0])) < 0)
        return -1;

    for(size_t i = 0; i < entry->filesize; ++i) {
        uint16_t target = *next_target;

        if(target >= root->blk_cnt || first + i >= root->blk_cnt) {
            errno = EILSEQ;
            return -1;
        }

        plan->source[first + i] = blocks[i];
        plan->target[first + i] = target;
        if(blocks[i] != target)
            ++plan->moved_blocks;

        if(i + 1u < entry->filesize) {
            if(descending)
                --*next_target;
            else
                ++*next_target;
        }
    }

    if(entry->firstblk != plan->target[first]) {
        entry->firstblk = plan->target[first];
        entry->dirty = 1;
    }

    for(size_t i = 0; i < entry->filesize; ++i) {
        uint16_t target = plan->target[first + i];

        plan->fat[target] = i + 1u < entry->filesize ?
            plan->target[first + i + 1u] : VMUFS_FAT_EOF;
    }

    plan->live_blocks += entry->filesize;
    if(descending)
        *next_target = plan->live_blocks < root->blk_cnt ?
            (uint16_t)(plan->target[plan->live_blocks - 1u] - 1u) : 0;
    else
        *next_target = (uint16_t)plan->live_blocks;

    return 0;
}

int vmufs_defrag_plan_build(const vmu_root_t *root, const uint16_t *fat,
                            size_t fat_entries, vmu_dir_t *directory,
                            size_t directory_entries,
                            vmufs_defrag_plan_t *plan) {
    vmufs_validation_t validation;
    size_t required_entries;
    size_t executable_count = 0;
    uint16_t low_target = 0;
    uint16_t high_target;

    if(!root || !fat || !directory || !plan) {
        errno = EINVAL;
        return -1;
    }

    required_entries = root->dir_size * VMUFS_BLOCK_SIZE /
                       sizeof(*directory);
    if(directory_entries < required_entries ||
       vmufs_validate(root, VMUFS_STANDARD_CARD_BLOCKS, fat, fat_entries,
                      directory, directory_entries, &validation) < 0)
        return -1;

    for(size_t i = 0; i < required_entries; ++i) {
        if(directory[i].filetype == VMUFS_FILETYPE_GAME)
            ++executable_count;
    }

    if(executable_count > 1) {
        errno = EILSEQ;
        return -1;
    }

    memset(plan, 0, sizeof(*plan));
    memcpy(plan->fat, fat, VMUFS_BLOCK_SIZE);
    for(size_t i = 0; i < root->blk_cnt; ++i)
        plan->fat[i] = VMUFS_FAT_FREE;

    /* Validation and the executable-count check occur before the directory is
       touched, so every expected failure leaves caller metadata unchanged. */
    for(size_t i = 0; i < required_entries; ++i) {
        if(directory[i].filetype == VMUFS_FILETYPE_GAME &&
           append_entry(root, fat, fat_entries, &directory[i], plan,
                        &low_target, false) < 0)
            return -1;
    }

    high_target = (uint16_t)(root->blk_cnt - 1u);
    for(size_t i = 0; i < required_entries; ++i) {
        if(directory[i].filetype == VMUFS_FILETYPE_DATA &&
           append_entry(root, fat, fat_entries, &directory[i], plan,
                        &high_target, true) < 0)
            return -1;
    }

    for(size_t block = 0; block < root->dir_size; ++block) {
        vmu_dir_t *entries = directory +
            block * VMUFS_BLOCK_SIZE / sizeof(*directory);

        for(size_t i = 0; i < VMUFS_BLOCK_SIZE / sizeof(*directory); ++i) {
            if(entries[i].dirty) {
                ++plan->dirty_dir_blocks;
                break;
            }
        }
    }

    return 0;
}
