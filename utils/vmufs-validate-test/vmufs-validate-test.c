/* KallistiOS ##version##

   vmufs-validate-test.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <dc/vmufs_meta.h>

#include "vmufs_internal.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "vmufs-validate-test currently requires a little-endian host"
#endif

#define TEST_FAT_ENTRIES (VMUFS_BLOCK_SIZE / sizeof(uint16_t))
#define TEST_DIR_ENTRIES \
    (VMUFS_STANDARD_DIR_BLOCKS * VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t))

static void make_root(vmu_root_t *root) {
    memset(root, 0, sizeof(*root));
    memset(root->magic, 0x55, sizeof(root->magic));
    root->fat_loc = VMUFS_STANDARD_FAT_BLOCK;
    root->fat_size = 1;
    root->dir_loc = VMUFS_STANDARD_DIR_BLOCK;
    root->dir_size = VMUFS_STANDARD_DIR_BLOCKS;
    root->blk_cnt = VMUFS_STANDARD_USER_BLOCKS;
}

static void make_empty(vmu_root_t *root, uint16_t *fat, vmu_dir_t *dir) {
    make_root(root);

    for(size_t i = 0; i < TEST_FAT_ENTRIES; ++i)
        fat[i] = VMUFS_FAT_FREE;

    memset(dir, 0, TEST_DIR_ENTRIES * sizeof(*dir));
}

static void make_file(vmu_dir_t *entry, const char *name,
                      uint16_t first_block, uint16_t block_count) {
    size_t name_length = strlen(name);

    memset(entry, 0, sizeof(*entry));
    entry->filetype = VMUFS_FILETYPE_DATA;
    entry->firstblk = first_block;
    entry->filesize = block_count;
    if(name_length > sizeof(entry->filename))
        name_length = sizeof(entry->filename);
    memcpy(entry->filename, name, name_length);
}

static int expect_root_error(vmu_root_t *root, int expected_errno) {
    errno = 0;
    if(vmufs_root_validate(root, VMUFS_STANDARD_CARD_BLOCKS) == 0 ||
       errno != expected_errno)
        return -1;

    return 0;
}

static int expect_validation_error(const vmu_root_t *root,
                                   const uint16_t *fat,
                                   size_t fat_entries,
                                   const vmu_dir_t *dir,
                                   vmufs_validation_error_t expected) {
    vmufs_validation_t result;

    errno = 0;
    if(vmufs_validate(root, VMUFS_STANDARD_CARD_BLOCKS, fat, fat_entries,
                      dir, TEST_DIR_ENTRIES, &result) == 0 ||
       result.first_error != expected || errno == 0)
        return -1;

    return 0;
}

static int test_root_geometry(void) {
    vmu_root_t root;

    make_root(&root);
    if(vmufs_root_validate(&root, VMUFS_STANDARD_CARD_BLOCKS) < 0)
        return -1;

    root.magic[7] = 0;
    if(expect_root_error(&root, EILSEQ) < 0)
        return -1;

    make_root(&root);
    root.fat_loc = VMUFS_STANDARD_ROOT_BLOCK;
    if(expect_root_error(&root, EILSEQ) < 0)
        return -1;

    make_root(&root);
    root.fat_loc = 240;
    root.dir_loc = VMUFS_STANDARD_ROOT_BLOCK;
    if(expect_root_error(&root, EILSEQ) < 0)
        return -1;

    make_root(&root);
    root.fat_loc = 250;
    if(expect_root_error(&root, EILSEQ) < 0)
        return -1;

    make_root(&root);
    root.fat_size = 2;
    if(expect_root_error(&root, ENOTSUP) < 0)
        return -1;

    make_root(&root);
    errno = 0;
    if(vmufs_root_validate(&root, 0) == 0 || errno != EINVAL)
        return -1;

    make_root(&root);
    if(vmufs_root_validate_at(&root, VMUFS_STANDARD_CARD_BLOCKS, 240) < 0)
        return -1;

    errno = 0;
    if(vmufs_root_validate_at(&root, VMUFS_STANDARD_CARD_BLOCKS, 242) == 0 ||
       errno != EILSEQ)
        return -1;

    return 0;
}

static int test_valid_metadata(void) {
    uint16_t fat[TEST_FAT_ENTRIES];
    vmu_dir_t dir[TEST_DIR_ENTRIES];
    vmufs_validation_t result;
    vmu_root_t root;

    make_empty(&root, fat, dir);
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES, &result) < 0 ||
       result.error_count != 0 || result.file_count != 0 ||
       result.free_dir_entries != TEST_DIR_ENTRIES ||
       result.free_blocks != VMUFS_STANDARD_USER_BLOCKS ||
       result.executable_free_blocks != VMUFS_STANDARD_USER_BLOCKS)
        return -1;

    make_file(&dir[0], "VALID", 5, 2);
    fat[5] = 6;
    fat[6] = VMUFS_FAT_EOF;
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES, &result) < 0 ||
       result.file_count != 1 || result.reachable_blocks != 2 ||
       result.used_blocks != 2 ||
       result.free_blocks != VMUFS_STANDARD_USER_BLOCKS - 2 ||
       result.executable_free_blocks != 5)
        return -1;

    return 0;
}

static int test_chain_errors(void) {
    uint16_t fat[TEST_FAT_ENTRIES];
    vmu_dir_t dir[TEST_DIR_ENTRIES];
    vmu_root_t root;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "TYPE", 0, 1);
    dir[0].filetype = 0x44;
    fat[0] = VMUFS_FAT_EOF;
    if(expect_validation_error(&root, fat, TEST_FAT_ENTRIES, dir,
                               VMUFS_VALIDATION_BAD_FILE_TYPE) < 0)
        return -1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "EMPTY", 0, 0);
    if(expect_validation_error(&root, fat, TEST_FAT_ENTRIES, dir,
                               VMUFS_VALIDATION_EMPTY_FILE) < 0)
        return -1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "RANGE", VMUFS_STANDARD_USER_BLOCKS, 1);
    if(expect_validation_error(&root, fat, TEST_FAT_ENTRIES, dir,
                               VMUFS_VALIDATION_CHAIN_OUT_OF_RANGE) < 0)
        return -1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "EARLY", 0, 2);
    fat[0] = VMUFS_FAT_EOF;
    if(expect_validation_error(&root, fat, TEST_FAT_ENTRIES, dir,
                               VMUFS_VALIDATION_CHAIN_EARLY_END) < 0)
        return -1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "LONG", 0, 1);
    fat[0] = 1;
    fat[1] = VMUFS_FAT_EOF;
    if(expect_validation_error(&root, fat, TEST_FAT_ENTRIES, dir,
                               VMUFS_VALIDATION_CHAIN_TOO_LONG) < 0)
        return -1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "CYCLE", 0, 3);
    fat[0] = 1;
    fat[1] = 0;
    if(expect_validation_error(&root, fat, TEST_FAT_ENTRIES, dir,
                               VMUFS_VALIDATION_CHAIN_CYCLE) < 0)
        return -1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "FIRST", 0, 1);
    make_file(&dir[1], "SECOND", 0, 1);
    fat[0] = VMUFS_FAT_EOF;
    if(expect_validation_error(&root, fat, TEST_FAT_ENTRIES, dir,
                               VMUFS_VALIDATION_CHAIN_CROSSLINK) < 0)
        return -1;

    make_empty(&root, fat, dir);
    make_file(&dir[0], "DUPLICATE", 0, 1);
    make_file(&dir[1], "DUPLICATE", 1, 1);
    fat[0] = VMUFS_FAT_EOF;
    fat[1] = VMUFS_FAT_EOF;
    if(expect_validation_error(&root, fat, TEST_FAT_ENTRIES, dir,
                               VMUFS_VALIDATION_DUPLICATE_NAME) < 0)
        return -1;

    return 0;
}

static int test_orphans_and_arguments(void) {
    uint16_t fat[TEST_FAT_ENTRIES];
    vmu_dir_t dir[TEST_DIR_ENTRIES];
    vmufs_validation_t result;
    vmu_root_t root;

    make_empty(&root, fat, dir);
    fat[7] = VMUFS_FAT_EOF;
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES, &result) == 0 ||
       result.first_error != VMUFS_VALIDATION_ORPHAN_BLOCK ||
       result.orphan_blocks != 1 ||
       !vmufs_validation_allows_mutation(&result))
        return -1;

    make_file(&dir[0], "BADTYPE", 0, 1);
    dir[0].filetype = 0x44;
    fat[0] = VMUFS_FAT_EOF;
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES, &result) == 0 ||
       vmufs_validation_allows_mutation(&result))
        return -1;

    make_empty(&root, fat, dir);
    if(expect_validation_error(&root, fat,
                               VMUFS_STANDARD_USER_BLOCKS - 1,
                               dir, VMUFS_VALIDATION_INVALID_ARGUMENT) < 0)
        return -1;

    errno = 0;
    if(vmufs_validate(&root, 0, fat, TEST_FAT_ENTRIES, dir,
                      TEST_DIR_ENTRIES, &result) == 0 || errno != EINVAL ||
       result.first_error != VMUFS_VALIDATION_INVALID_ARGUMENT)
        return -1;

    return 0;
}

static int test_executable_prefix(void) {
    uint16_t fat[TEST_FAT_ENTRIES];
    vmu_dir_t dir[TEST_DIR_ENTRIES];
    vmu_root_t root;

    make_empty(&root, fat, dir);
    if(vmufs_fat_free_executable(&root, fat, TEST_FAT_ENTRIES) !=
       VMUFS_STANDARD_USER_BLOCKS)
        return -1;

    fat[10] = VMUFS_FAT_EOF;
    if(vmufs_fat_free_executable(&root, fat, TEST_FAT_ENTRIES) != 10)
        return -1;

    fat[0] = VMUFS_FAT_EOF;
    if(vmufs_fat_free_executable(&root, fat, TEST_FAT_ENTRIES) != 0)
        return -1;

    return 0;
}

int main(void) {
    if(test_root_geometry() < 0 || test_valid_metadata() < 0 ||
       test_chain_errors() < 0 || test_orphans_and_arguments() < 0 ||
       test_executable_prefix() < 0) {
        fprintf(stderr, "vmufs-validate-test: FAIL errno=%d\n", errno);
        return 1;
    }

    puts("vmufs-validate-test: PASS");
    return 0;
}
