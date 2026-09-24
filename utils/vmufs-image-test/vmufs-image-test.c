/* KallistiOS ##version##

   vmufs-image-test.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dc/vmufs_meta.h>

#include "vmufs_internal.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "vmufs-image-test currently requires a little-endian host"
#endif

#define STANDARD_ROOT_BLOCK 255u
#define TEST_CARD_BLOCKS    256u
#define TEST_USER_BLOCKS    200u
#define TEST_DIR_BLOCKS     13u
#define TEST_DIR_ENTRIES    (TEST_DIR_BLOCKS * VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t))

static const char *validation_error_name(vmufs_validation_error_t error) {
    static const char *const names[] = {
        "ok",
        "invalid argument",
        "bad root magic",
        "bad root geometry",
        "unsupported FAT geometry",
        "bad file type",
        "empty file",
        "chain out of range",
        "chain ends early",
        "chain longer than directory size",
        "chain cycle",
        "cross-linked chain",
        "duplicate filename",
        "orphan block"
    };

    if((size_t)error >= sizeof(names) / sizeof(names[0]))
        return "unknown error";

    return names[error];
}

static void make_root(vmu_root_t *root) {
    memset(root, 0, sizeof(*root));
    memset(root->magic, 0x55, sizeof(root->magic));
    root->fat_loc = 254;
    root->fat_size = 1;
    root->dir_loc = 253;
    root->dir_size = TEST_DIR_BLOCKS;
    root->blk_cnt = TEST_USER_BLOCKS;
}

static void make_empty(vmu_root_t *root, uint16_t *fat, vmu_dir_t *dir) {
    make_root(root);

    for(size_t i = 0; i < VMUFS_BLOCK_SIZE / sizeof(*fat); ++i)
        fat[i] = VMUFS_FAT_FREE;

    memset(dir, 0, TEST_DIR_ENTRIES * sizeof(*dir));
}

static void make_file(vmu_dir_t *entry, const char *name,
                      uint16_t first_block, uint16_t blocks) {
    memset(entry, 0, sizeof(*entry));
    entry->filetype = VMUFS_FILETYPE_DATA;
    entry->firstblk = first_block;
    entry->filesize = blocks;
    memcpy(entry->filename, name,
           strlen(name) < sizeof(entry->filename) ?
           strlen(name) : sizeof(entry->filename));
}

static int expect_error(const vmu_root_t *root, const uint16_t *fat,
                        const vmu_dir_t *dir,
                        vmufs_validation_error_t expected) {
    vmufs_validation_t result;

    if(vmufs_validate(root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) == 0 ||
       result.first_error != expected) {
        fprintf(stderr, "expected '%s', got '%s'\n",
                validation_error_name(expected),
                validation_error_name(result.first_error));
        return -1;
    }

    return 0;
}

static uint32_t random_state = 0x4b4f5356u;

static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static int test_format_model(void) {
    vmufs_format_options_t options = {0};
    vmu_root_t root;
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];

    options.icon_shape = 123;
    options.timestamp.cent = 0x20;
    options.timestamp.year = 0x26;
    if(vmufs_format_build(&options, &root, fat,
                          sizeof(fat) / sizeof(fat[0])) < 0 ||
       vmufs_root_validate(&root, TEST_CARD_BLOCKS) < 0 ||
       root.fat_loc != VMUFS_STANDARD_FAT_BLOCK ||
       root.dir_loc != VMUFS_STANDARD_DIR_BLOCK ||
       root.dir_size != VMUFS_STANDARD_DIR_BLOCKS ||
       root.blk_cnt != VMUFS_STANDARD_USER_BLOCKS ||
       root.icon_shape != 123 || fat[241] != VMUFS_FAT_EOF ||
       fat[242] != 241 || fat[253] != 252 ||
       fat[254] != VMUFS_FAT_EOF || fat[255] != VMUFS_FAT_EOF)
        return -1;

    options.use_custom_color = 1;
    options.custom_color[3] = 127;
    if(vmufs_format_build(&options, &root, fat,
                          sizeof(fat) / sizeof(fat[0])) == 0)
        return -1;

    options.use_custom_color = 0;
    options.custom_color[0] = 1;
    options.custom_color[3] = 0;
    if(vmufs_format_build(&options, &root, fat,
                          sizeof(fat) / sizeof(fat[0])) == 0)
        return -1;

    return 0;
}

static int test_root_geometry_guards(void) {
    vmu_root_t root;

    make_root(&root);
    root.fat_loc = STANDARD_ROOT_BLOCK;
    if(vmufs_root_validate(&root, TEST_CARD_BLOCKS) == 0 || errno != EILSEQ)
        return -1;

    make_root(&root);
    root.fat_loc = 241;
    root.dir_loc = STANDARD_ROOT_BLOCK;
    if(vmufs_root_validate(&root, TEST_CARD_BLOCKS) == 0 || errno != EILSEQ)
        return -1;

    make_root(&root);
    if(vmufs_root_validate_at(&root, TEST_CARD_BLOCKS, 240) < 0)
        return -1;

    make_root(&root);
    root.blk_cnt = 256;
    root.fat_loc = 510;
    root.dir_loc = 509;
    if(vmufs_root_validate(&root, 512) == 0 || errno != EILSEQ)
        return -1;

    return 0;
}

static int test_random_defrag_models(void) {
    for(size_t iteration = 0; iteration < 128; ++iteration) {
        vmu_root_t root;
        uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
        uint16_t shuffled[TEST_USER_BLOCKS];
        uint32_t payload[TEST_USER_BLOCKS] = {0};
        uint32_t relocated[TEST_USER_BLOCKS] = {0};
        uint32_t expected[12][9] = {{0}};
        vmu_dir_t dir[TEST_DIR_ENTRIES];
        vmufs_defrag_plan_t plan;
        vmufs_defrag_plan_t second_plan;
        vmufs_validation_t validation;
        size_t file_count = 1u + next_random() % 10u;
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
            size_t blocks = 1u + next_random() % 8u;
            char name[13];

            if(live_blocks + blocks > 80)
                blocks = 1;
            snprintf(name, sizeof(name), "F%03zu%03zu", iteration, file);
            make_file(&dir[file], name, shuffled[next_source],
                      (uint16_t)blocks);
            if(file == executable_entry)
                dir[file].filetype = VMUFS_FILETYPE_GAME;

            for(size_t step = 0; step < blocks; ++step) {
                uint16_t source = shuffled[next_source + step];
                uint32_t token = (uint32_t)((file + 1u) << 16) |
                                 (uint32_t)(step + 1u);

                expected[file][step] = token;
                payload[source] = token;
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

        for(size_t i = 0; i < plan.live_blocks; ++i)
            relocated[plan.target[i]] = payload[plan.source[i]];

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

            for(size_t step = 0; step < dir[file].filesize; ++step) {
                if(block >= TEST_USER_BLOCKS ||
                   relocated[block] != expected[file][step])
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

static int run_self_tests(void) {
    vmu_root_t root;
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t saved_fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t blocks[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    vmu_dir_t dir[TEST_DIR_ENTRIES];
    vmu_dir_t saved_dir[TEST_DIR_ENTRIES];
    vmufs_validation_t result;

    if(test_format_model() < 0 || test_root_geometry_guards() < 0 ||
       test_random_defrag_models() < 0)
        return 1;

    make_empty(&root, fat, dir);
    if(vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) < 0 ||
       result.free_blocks != TEST_USER_BLOCKS ||
       result.executable_free_blocks != TEST_USER_BLOCKS ||
       !vmufs_validation_allows_mutation(&result))
        return 1;

    make_file(&dir[0], "VALID", 199, 2);
    fat[199] = 198;
    fat[198] = VMUFS_FAT_EOF;
    if(vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) < 0 ||
       result.file_count != 1 || result.reachable_blocks != 2 ||
       result.free_blocks != 198 || result.executable_free_blocks != 198)
        return 1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "RANGE", TEST_USER_BLOCKS, 1);
    if(expect_error(&root, fat, dir,
                    VMUFS_VALIDATION_CHAIN_OUT_OF_RANGE) < 0)
        return 1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "EARLY", 10, 2);
    fat[10] = VMUFS_FAT_EOF;
    if(expect_error(&root, fat, dir,
                    VMUFS_VALIDATION_CHAIN_EARLY_END) < 0)
        return 1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "CYCLE", 10, 3);
    fat[10] = 11;
    fat[11] = 10;
    if(expect_error(&root, fat, dir,
                    VMUFS_VALIDATION_CHAIN_CYCLE) < 0)
        return 1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "FIRST", 10, 2);
    make_file(&dir[1], "SECOND", 11, 1);
    fat[10] = 11;
    fat[11] = VMUFS_FAT_EOF;
    if(expect_error(&root, fat, dir,
                    VMUFS_VALIDATION_CHAIN_CROSSLINK) < 0)
        return 1;
    if(vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) == 0 ||
       vmufs_validation_allows_mutation(&result))
        return 1;

    make_empty(&root, fat, dir);
    fat[50] = VMUFS_FAT_EOF;
    if(expect_error(&root, fat, dir,
                    VMUFS_VALIDATION_ORPHAN_BLOCK) < 0)
        return 1;
    if(vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) == 0 ||
       !vmufs_validation_allows_mutation(&result))
        return 1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "SAME", 10, 1);
    make_file(&dir[1], "SAME", 11, 1);
    fat[10] = VMUFS_FAT_EOF;
    fat[11] = VMUFS_FAT_EOF;
    if(expect_error(&root, fat, dir,
                    VMUFS_VALIDATION_DUPLICATE_NAME) < 0)
        return 1;

    make_empty(&root, fat, dir);
    fat[0] = VMUFS_FAT_EOF;
    if(vmufs_fat_free_executable(&root, fat,
                                 VMUFS_BLOCK_SIZE / sizeof(*fat)) != 0)
        return 1;

    make_empty(&root, fat, dir);
    root.magic[0] = 0;
    if(expect_error(&root, fat, dir,
                    VMUFS_VALIDATION_BAD_ROOT_MAGIC) < 0)
        return 1;

    make_empty(&root, fat, dir);
    if(vmufs_chain_allocate(&root, fat,
                            VMUFS_BLOCK_SIZE / sizeof(*fat),
                            VMUFS_FILETYPE_DATA, 3, blocks,
                            sizeof(blocks) / sizeof(blocks[0])) < 0 ||
       blocks[0] != 199 || blocks[1] != 198 || blocks[2] != 197 ||
       fat[199] != 198 || fat[198] != 197 ||
       fat[197] != VMUFS_FAT_EOF)
        return 1;

    vmufs_chain_release(fat, blocks, 3);
    if(fat[199] != VMUFS_FAT_FREE || fat[198] != VMUFS_FAT_FREE ||
       fat[197] != VMUFS_FAT_FREE)
        return 1;

    if(vmufs_chain_allocate(&root, fat,
                            VMUFS_BLOCK_SIZE / sizeof(*fat),
                            VMUFS_FILETYPE_GAME, 3, blocks,
                            sizeof(blocks) / sizeof(blocks[0])) < 0 ||
       blocks[0] != 0 || blocks[1] != 1 || blocks[2] != 2)
        return 1;

    make_empty(&root, fat, dir);
    fat[1] = VMUFS_FAT_EOF;
    memcpy(saved_fat, fat, sizeof(fat));
    if(vmufs_chain_allocate(&root, fat,
                            VMUFS_BLOCK_SIZE / sizeof(*fat),
                            VMUFS_FILETYPE_GAME, 2, blocks,
                            sizeof(blocks) / sizeof(blocks[0])) == 0 ||
       memcmp(fat, saved_fat, sizeof(fat)) != 0)
        return 1;

    /* Model every metadata commit prefix of a copy-on-write replacement.
       The staging states contain an orphan by design, but the directory's
       selected old/new chain must remain exact and readable throughout. */
    make_empty(&root, fat, dir);
    make_file(&dir[0], "REPLACE", 199, 2);
    fat[199] = 198;
    fat[198] = VMUFS_FAT_EOF;
    memcpy(saved_fat, fat, sizeof(fat));
    memcpy(saved_dir, dir, sizeof(dir));

    if(vmufs_chain_allocate(&root, fat,
                            VMUFS_BLOCK_SIZE / sizeof(*fat),
                            VMUFS_FILETYPE_DATA, 2, blocks,
                            sizeof(blocks) / sizeof(blocks[0])) < 0 ||
       vmufs_chain_collect(&root, fat,
                           VMUFS_BLOCK_SIZE / sizeof(*fat),
                           &dir[0], blocks + 2,
                           sizeof(blocks) / sizeof(blocks[0]) - 2) < 0)
        return 1;

    if(vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) == 0 ||
       result.first_error != VMUFS_VALIDATION_ORPHAN_BLOCK)
        return 1;

    make_file(&dir[0], "REPLACE", blocks[0], 2);
    if(vmufs_chain_collect(&root, fat,
                           VMUFS_BLOCK_SIZE / sizeof(*fat),
                           &dir[0], blocks + 4,
                           sizeof(blocks) / sizeof(blocks[0]) - 4) < 0 ||
       vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) == 0 ||
       result.first_error != VMUFS_VALIDATION_ORPHAN_BLOCK)
        return 1;

    fat[199] = VMUFS_FAT_FREE;
    fat[198] = VMUFS_FAT_FREE;
    if(vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) < 0)
        return 1;

    /* Deletion commits the empty directory entry before releasing its chain.
       The intermediate state can leak blocks but cannot cross-link a live file. */
    memcpy(fat, saved_fat, sizeof(fat));
    memcpy(dir, saved_dir, sizeof(dir));
    memset(&dir[0], 0, sizeof(dir[0]));
    if(vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) == 0 ||
       result.first_error != VMUFS_VALIDATION_ORPHAN_BLOCK)
        return 1;

    fat[199] = VMUFS_FAT_FREE;
    fat[198] = VMUFS_FAT_FREE;
    if(vmufs_validate(&root, TEST_CARD_BLOCKS, fat,
                      VMUFS_BLOCK_SIZE / sizeof(*fat),
                      dir, TEST_DIR_ENTRIES, &result) < 0)
        return 1;

    puts("vmufs-image-test: all tests passed");
    return 0;
}

static int load_image(const char *path, uint8_t **data_out, size_t *size_out) {
    FILE *fp = fopen(path, "rb");
    uint8_t *data;
    long length;

    if(!fp)
        return -1;

    if(fseek(fp, 0, SEEK_END) < 0 || (length = ftell(fp)) < 0 ||
       fseek(fp, 0, SEEK_SET) < 0) {
        fclose(fp);
        return -1;
    }

    data = malloc((size_t)length);
    if(!data) {
        fclose(fp);
        errno = ENOMEM;
        return -1;
    }

    if(fread(data, 1, (size_t)length, fp) != (size_t)length) {
        free(data);
        fclose(fp);
        errno = EIO;
        return -1;
    }

    fclose(fp);
    *data_out = data;
    *size_out = (size_t)length;
    return 0;
}

static int inspect_image(const char *path, size_t root_block) {
    uint8_t *image = NULL;
    vmu_dir_t *dir = NULL;
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    vmu_root_t root;
    vmufs_validation_t result;
    size_t image_size, card_blocks, dir_entries, dir_first;
    int status = 1;

    if(load_image(path, &image, &image_size) < 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return 1;
    }

    if(image_size == 0 || image_size % VMUFS_BLOCK_SIZE != 0) {
        fprintf(stderr, "%s: size is not a whole number of VMU blocks\n", path);
        goto out;
    }

    card_blocks = image_size / VMUFS_BLOCK_SIZE;
    if(root_block >= card_blocks) {
        fprintf(stderr, "%s: root block %zu is outside the image\n",
                path, root_block);
        goto out;
    }

    memcpy(&root, image + root_block * VMUFS_BLOCK_SIZE, sizeof(root));
    if(vmufs_root_validate_at(&root, card_blocks, root_block) < 0) {
        fprintf(stderr, "%s: invalid or unsupported root geometry: %s\n",
                path, strerror(errno));
        goto out;
    }

    memcpy(fat, image + (size_t)root.fat_loc * VMUFS_BLOCK_SIZE,
           sizeof(fat));
    dir_entries = root.dir_size * VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t);
    dir = malloc(dir_entries * sizeof(*dir));
    if(!dir) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        goto out;
    }

    dir_first = root.dir_loc + 1u - root.dir_size;
    for(size_t i = 0; i < root.dir_size; ++i) {
        size_t image_block = root.dir_loc - i;
        memcpy((uint8_t *)dir + i * VMUFS_BLOCK_SIZE,
               image + image_block * VMUFS_BLOCK_SIZE, VMUFS_BLOCK_SIZE);
    }

    (void)dir_first;
    status = vmufs_validate_at(&root, card_blocks, root_block, fat,
                               VMUFS_BLOCK_SIZE / sizeof(*fat),
                               dir, dir_entries, &result) < 0;

    printf("%s: files=%zu free-dir=%zu free=%zu used=%zu reachable=%zu "
           "orphans=%zu executable-free=%zu errors=%zu\n",
           path, result.file_count, result.free_dir_entries,
           result.free_blocks, result.used_blocks, result.reachable_blocks,
           result.orphan_blocks, result.executable_free_blocks,
           result.error_count);

    if(status) {
        fprintf(stderr, "first error: %s",
                validation_error_name(result.first_error));
        if(result.first_dir_index != SIZE_MAX)
            fprintf(stderr, ", directory entry %zu", result.first_dir_index);
        if(result.first_block != UINT16_MAX)
            fprintf(stderr, ", block %" PRIu16, result.first_block);
        fputc('\n', stderr);
    }

out:
    free(dir);
    free(image);
    return status;
}

int main(int argc, char **argv) {
    size_t root_block = STANDARD_ROOT_BLOCK;
    char *end;

    if(argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return run_self_tests();

    if(argc == 4 && strcmp(argv[1], "--root-block") == 0) {
        unsigned long value = strtoul(argv[2], &end, 0);

        if(*argv[2] == '\0' || *end != '\0' || value > SIZE_MAX) {
            fprintf(stderr, "invalid root block: %s\n", argv[2]);
            return 2;
        }

        root_block = (size_t)value;
        return inspect_image(argv[3], root_block);
    }

    if(argc == 2)
        return inspect_image(argv[1], root_block);

    fprintf(stderr, "usage: %s [--root-block block] image\n"
                    "       %s --self-test\n", argv[0], argv[0]);
    return 2;
}
