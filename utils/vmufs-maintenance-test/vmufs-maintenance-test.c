/* KallistiOS ##version##

   vmufs-maintenance-test.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vmufs_internal.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "vmufs-maintenance-test currently requires a little-endian host"
#endif

#define TEST_CARD_BLOCKS      VMUFS_STANDARD_CARD_BLOCKS
#define TEST_USER_BLOCKS      VMUFS_STANDARD_USER_BLOCKS
#define TEST_DIR_ENTRIES      (VMUFS_STANDARD_DIR_BLOCKS * \
                               VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t))
#define TEST_ITERATIONS       512u
#define TEST_MAX_FILES        12u
#define TEST_MAX_FILE_BLOCKS  8u

typedef uint8_t block_image_t[VMUFS_BLOCK_SIZE];

static uint32_t random_state = 0x4b4f5356u;

static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void make_empty(vmu_root_t *root, uint16_t *fat, vmu_dir_t *dir) {
    memset(root, 0, sizeof(*root));
    memset(root->magic, 0x55, sizeof(root->magic));
    root->fat_loc = VMUFS_STANDARD_FAT_BLOCK;
    root->fat_size = 1;
    root->dir_loc = VMUFS_STANDARD_DIR_BLOCK;
    root->dir_size = VMUFS_STANDARD_DIR_BLOCKS;
    root->blk_cnt = VMUFS_STANDARD_USER_BLOCKS;

    for(size_t i = 0; i < VMUFS_BLOCK_SIZE / sizeof(*fat); ++i)
        fat[i] = VMUFS_FAT_FREE;
    memset(dir, 0, TEST_DIR_ENTRIES * sizeof(*dir));
}

static void make_file(vmu_dir_t *entry, size_t index, uint8_t filetype,
                      uint16_t first_block, uint16_t block_count) {
    char name[sizeof(entry->filename) + 1];

    memset(entry, 0, sizeof(*entry));
    entry->filetype = filetype;
    entry->firstblk = first_block;
    entry->filesize = block_count;
    (void)snprintf(name, sizeof(name), "FILE%08zu", index);
    memcpy(entry->filename, name, sizeof(entry->filename));
}

static int test_format_model(void) {
    static const uint8_t control_1[6] = {
        0xff, 0x00, 0x00, 0x00, 0xff, 0x00
    };
    static const uint8_t control_2[6] = {
        0x1f, 0x00, 0x00, 0x00, 0x80, 0x00
    };
    vmufs_format_options_t options = {0};
    vmu_root_t root;
    vmu_root_t saved_root;
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t saved_fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];

    memset(&root, 0xa5, sizeof(root));
    memset(fat, 0xa5, sizeof(fat));
    options.icon_shape = 123;
    options.timestamp.cent = 0x20;
    options.timestamp.year = 0x26;

    if(vmufs_format_build(&options, &root, fat,
                          sizeof(fat) / sizeof(fat[0])) < 0 ||
       vmufs_root_validate(&root, TEST_CARD_BLOCKS) < 0 ||
       root.fat_loc != VMUFS_STANDARD_FAT_BLOCK ||
       root.fat_size != 1 || root.dir_loc != VMUFS_STANDARD_DIR_BLOCK ||
       root.dir_size != VMUFS_STANDARD_DIR_BLOCKS ||
       root.blk_cnt != VMUFS_STANDARD_USER_BLOCKS ||
       root.icon_shape != 123 || root.timestamp.cent != 0x20 ||
       root.timestamp.year != 0x26 ||
       memcmp(root.unk1, control_1, sizeof(control_1)) != 0 ||
       memcmp(root.unk2, control_2, sizeof(control_2)) != 0)
        return -1;

    for(size_t i = sizeof(control_2); i < sizeof(root.unk2); ++i) {
        if(root.unk2[i] != 0)
            return -1;
    }

    for(size_t i = 0; i < VMUFS_STANDARD_USER_BLOCKS; ++i) {
        if(fat[i] != VMUFS_FAT_FREE)
            return -1;
    }
    if(fat[241] != VMUFS_FAT_EOF || fat[242] != 241 ||
       fat[253] != 252 || fat[254] != VMUFS_FAT_EOF ||
       fat[255] != VMUFS_FAT_EOF)
        return -1;

    memcpy(&saved_root, &root, sizeof(root));
    memcpy(saved_fat, fat, sizeof(fat));
    options.use_custom_color = 1;
    options.custom_color[3] = 127;
    if(vmufs_format_build(&options, &root, fat,
                          sizeof(fat) / sizeof(fat[0])) == 0 ||
       errno != EINVAL || memcmp(&root, &saved_root, sizeof(root)) != 0 ||
       memcmp(fat, saved_fat, sizeof(fat)) != 0)
        return -1;

    options.use_custom_color = 0;
    options.custom_color[0] = 1;
    options.custom_color[3] = 0;
    if(vmufs_format_build(&options, &root, fat,
                          sizeof(fat) / sizeof(fat[0])) == 0 ||
       errno != EINVAL || memcmp(&root, &saved_root, sizeof(root)) != 0 ||
       memcmp(fat, saved_fat, sizeof(fat)) != 0)
        return -1;

    options.use_custom_color = 1;
    options.custom_color[0] = 0x11;
    options.custom_color[1] = 0x22;
    options.custom_color[2] = 0x33;
    options.custom_color[3] = 0x80;
    if(vmufs_format_build(&options, &root, fat,
                          sizeof(fat) / sizeof(fat[0])) < 0 ||
       root.use_custom != 1 ||
       memcmp(root.custom_color, options.custom_color,
              sizeof(root.custom_color)) != 0)
        return -1;

    return 0;
}

static void fill_block(block_image_t block, size_t iteration,
                       size_t file, size_t logical_block) {
    for(size_t i = 0; i < VMUFS_BLOCK_SIZE; ++i) {
        uint32_t value = (uint32_t)(iteration * 131u + file * 29u +
                                    logical_block * 17u + i);
        block[i] = (uint8_t)(value ^ (value >> 8) ^ (value >> 16));
    }
}

static int test_random_defrag_models(void) {
    for(size_t iteration = 0; iteration < TEST_ITERATIONS; ++iteration) {
        vmu_root_t root;
        uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
        uint16_t shuffled[TEST_USER_BLOCKS];
        vmu_dir_t dir[TEST_DIR_ENTRIES];
        block_image_t source_image[TEST_USER_BLOCKS] = {{0}};
        block_image_t final_image[TEST_USER_BLOCKS] = {{0}};
        block_image_t expected[TEST_MAX_FILES][TEST_MAX_FILE_BLOCKS] = {{{0}}};
        vmufs_defrag_plan_t plan;
        vmufs_defrag_plan_t second_plan;
        vmufs_validation_t validation;
        size_t file_count = 1u + next_random() % TEST_MAX_FILES;
        size_t executable_entry = (next_random() & 1u) ?
            next_random() % file_count : SIZE_MAX;
        size_t next_source = 0;
        size_t live_blocks = 0;

        make_empty(&root, fat, dir);
        for(size_t i = 0; i < TEST_USER_BLOCKS; ++i)
            shuffled[i] = (uint16_t)i;
        for(size_t i = TEST_USER_BLOCKS; i > 1; --i) {
            size_t other = next_random() % i;
            uint16_t temporary = shuffled[i - 1u];

            shuffled[i - 1u] = shuffled[other];
            shuffled[other] = temporary;
        }

        for(size_t file = 0; file < file_count; ++file) {
            size_t blocks = 1u + next_random() % TEST_MAX_FILE_BLOCKS;
            uint8_t filetype = file == executable_entry ?
                VMUFS_FILETYPE_GAME : VMUFS_FILETYPE_DATA;

            make_file(&dir[file], iteration * TEST_MAX_FILES + file,
                      filetype, shuffled[next_source], (uint16_t)blocks);
            for(size_t step = 0; step < blocks; ++step) {
                uint16_t source = shuffled[next_source + step];

                fill_block(expected[file][step], iteration, file, step);
                memcpy(source_image[source], expected[file][step],
                       VMUFS_BLOCK_SIZE);
                fat[source] = step + 1u < blocks ?
                    shuffled[next_source + step + 1u] : VMUFS_FAT_EOF;
            }

            next_source += blocks;
            live_blocks += blocks;
        }

        if(vmufs_defrag_plan_build(&root, fat,
                                   sizeof(fat) / sizeof(fat[0]),
                                   dir, TEST_DIR_ENTRIES, &plan) < 0 ||
           plan.live_blocks != live_blocks)
            return -1;

        for(size_t i = 0; i < plan.live_blocks; ++i) {
            memcpy(final_image[plan.target[i]], source_image[plan.source[i]],
                   VMUFS_BLOCK_SIZE);
        }

        if(vmufs_validate(&root, TEST_CARD_BLOCKS, plan.fat,
                          sizeof(plan.fat) / sizeof(plan.fat[0]),
                          dir, TEST_DIR_ENTRIES, &validation) < 0 ||
           validation.free_blocks != TEST_USER_BLOCKS - live_blocks ||
           validation.executable_free_blocks !=
               (executable_entry == SIZE_MAX ?
                TEST_USER_BLOCKS - live_blocks : 0))
            return -1;

        for(size_t file = 0; file < file_count; ++file) {
            uint16_t block = dir[file].firstblk;

            if(file == executable_entry && block != 0)
                return -1;
            for(size_t step = 0; step < dir[file].filesize; ++step) {
                if(block >= TEST_USER_BLOCKS ||
                   memcmp(final_image[block], expected[file][step],
                          VMUFS_BLOCK_SIZE) != 0)
                    return -1;
                block = plan.fat[block];
            }
        }

        for(size_t i = 0; i < TEST_DIR_ENTRIES; ++i)
            dir[i].dirty = 0;
        if(vmufs_defrag_plan_build(&root, plan.fat,
                                   sizeof(plan.fat) / sizeof(plan.fat[0]),
                                   dir, TEST_DIR_ENTRIES,
                                   &second_plan) < 0 ||
           second_plan.moved_blocks != 0 ||
           second_plan.dirty_dir_blocks != 0 ||
           memcmp(plan.fat, second_plan.fat, sizeof(plan.fat)) != 0)
            return -1;
    }

    return 0;
}

static int test_full_card_boundary(void) {
    vmu_root_t root;
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    vmu_dir_t dir[TEST_DIR_ENTRIES];
    vmufs_defrag_plan_t plan;
    vmufs_defrag_plan_t second_plan;
    vmufs_validation_t validation;

    make_empty(&root, fat, dir);
    make_file(&dir[0], 0, VMUFS_FILETYPE_GAME, 190, 10);
    for(size_t block = 190; block < 200; ++block) {
        fat[block] = block + 1u < 200 ?
            (uint16_t)(block + 1u) : VMUFS_FAT_EOF;
    }

    make_file(&dir[1], 1, VMUFS_FILETYPE_DATA, 189, 190);
    for(size_t block = 190; block > 0; --block) {
        fat[block - 1u] = block > 1u ?
            (uint16_t)(block - 2u) : VMUFS_FAT_EOF;
    }

    if(vmufs_defrag_plan_build(&root, fat,
                               sizeof(fat) / sizeof(fat[0]),
                               dir, TEST_DIR_ENTRIES, &plan) < 0 ||
       plan.live_blocks != TEST_USER_BLOCKS || dir[0].firstblk != 0 ||
       dir[1].firstblk != TEST_USER_BLOCKS - 1u ||
       vmufs_validate(&root, TEST_CARD_BLOCKS, plan.fat,
                      sizeof(plan.fat) / sizeof(plan.fat[0]),
                      dir, TEST_DIR_ENTRIES, &validation) < 0 ||
       validation.free_blocks != 0 ||
       validation.executable_free_blocks != 0)
        return -1;

    for(size_t i = 0; i < TEST_DIR_ENTRIES; ++i)
        dir[i].dirty = 0;
    if(vmufs_defrag_plan_build(&root, plan.fat,
                               sizeof(plan.fat) / sizeof(plan.fat[0]),
                               dir, TEST_DIR_ENTRIES, &second_plan) < 0 ||
       second_plan.moved_blocks != 0 ||
       second_plan.dirty_dir_blocks != 0)
        return -1;

    return 0;
}

static int test_rejection_is_nonmutating(void) {
    vmu_root_t root;
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    vmu_dir_t dir[TEST_DIR_ENTRIES];
    vmu_dir_t saved_dir[TEST_DIR_ENTRIES];
    vmufs_defrag_plan_t plan;
    vmufs_defrag_plan_t saved_plan;

    make_empty(&root, fat, dir);
    make_file(&dir[0], 0, VMUFS_FILETYPE_GAME, 0, 1);
    make_file(&dir[1], 1, VMUFS_FILETYPE_GAME, 1, 1);
    fat[0] = VMUFS_FAT_EOF;
    fat[1] = VMUFS_FAT_EOF;
    memset(&plan, 0xa5, sizeof(plan));
    memcpy(saved_dir, dir, sizeof(dir));
    memcpy(&saved_plan, &plan, sizeof(plan));

    if(vmufs_defrag_plan_build(&root, fat,
                               sizeof(fat) / sizeof(fat[0]),
                               dir, TEST_DIR_ENTRIES, &plan) == 0 ||
       errno != EILSEQ || memcmp(dir, saved_dir, sizeof(dir)) != 0 ||
       memcmp(&plan, &saved_plan, sizeof(plan)) != 0)
        return -1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], 0, VMUFS_FILETYPE_DATA, 10, 2);
    make_file(&dir[1], 1, VMUFS_FILETYPE_DATA, 11, 1);
    fat[10] = 11;
    fat[11] = VMUFS_FAT_EOF;
    memset(&plan, 0x5a, sizeof(plan));
    memcpy(saved_dir, dir, sizeof(dir));
    memcpy(&saved_plan, &plan, sizeof(plan));

    if(vmufs_defrag_plan_build(&root, fat,
                               sizeof(fat) / sizeof(fat[0]),
                               dir, TEST_DIR_ENTRIES, &plan) == 0 ||
       errno != EILSEQ || memcmp(dir, saved_dir, sizeof(dir)) != 0 ||
       memcmp(&plan, &saved_plan, sizeof(plan)) != 0)
        return -1;

    return 0;
}

int main(void) {
    if(test_format_model() < 0) {
        fprintf(stderr, "format model test failed\n");
        return 1;
    }
    if(test_random_defrag_models() < 0) {
        fprintf(stderr, "random defragmentation test failed\n");
        return 1;
    }
    if(test_full_card_boundary() < 0) {
        fprintf(stderr, "full-card boundary test failed\n");
        return 1;
    }
    if(test_rejection_is_nonmutating() < 0) {
        fprintf(stderr, "nonmutating rejection test failed\n");
        return 1;
    }

    puts("vmufs-maintenance-test: all tests passed");
    return 0;
}
