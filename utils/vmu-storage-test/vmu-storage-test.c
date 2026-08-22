/* KallistiOS ##version##

   vmu-storage-test.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dc/vmu_pkg.h>
#include <dc/vmufs_meta.h>

#include "vmufs_internal.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "vmu-storage-test currently requires a little-endian host"
#endif

#define TEST_FAT_ENTRIES (VMUFS_BLOCK_SIZE / sizeof(uint16_t))
#define TEST_DIR_ENTRIES \
    (VMUFS_STANDARD_DIR_BLOCKS * VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t))

uint16_t net_crc16ccitt(const uint8_t *data, int size, uint16_t start) {
    uint16_t value = start;

    while(size--) {
        uint16_t temporary = (value >> 8) ^ *data++;

        temporary ^= temporary >> 4;
        value = (value << 8) ^ (temporary << 12) ^
                (temporary << 5) ^ temporary;
    }

    return value;
}

static void make_fat(uint16_t *fat) {
    for(size_t i = 0; i < TEST_FAT_ENTRIES; ++i)
        fat[i] = VMUFS_FAT_FREE;
}

static void make_card(vmu_root_t *root, uint16_t *fat, vmu_dir_t *dir) {
    memset(root, 0, sizeof(*root));
    memset(root->magic, 0x55, sizeof(root->magic));
    root->fat_loc = VMUFS_STANDARD_FAT_BLOCK;
    root->fat_size = 1;
    root->dir_loc = VMUFS_STANDARD_DIR_BLOCK;
    root->dir_size = VMUFS_STANDARD_DIR_BLOCKS;
    root->blk_cnt = VMUFS_STANDARD_USER_BLOCKS;
    make_fat(fat);
    memset(dir, 0, TEST_DIR_ENTRIES * sizeof(*dir));
}

static void make_entry(vmu_dir_t *entry, const char *name,
                       uint16_t first_block, uint16_t blocks) {
    size_t length = strlen(name);

    memset(entry, 0, sizeof(*entry));
    entry->filetype = VMUFS_FILETYPE_DATA;
    entry->firstblk = first_block;
    entry->filesize = blocks;
    if(length > sizeof(entry->filename))
        length = sizeof(entry->filename);
    memcpy(entry->filename, name, length);
}

static int expect_chain_error(const vmu_root_t *root, const uint16_t *fat,
                              const vmu_dir_t *entry, int expected_errno) {
    uint16_t blocks[TEST_FAT_ENTRIES];

    errno = 0;
    if(vmufs_chain_collect(root, fat, TEST_FAT_ENTRIES, entry, blocks,
                           TEST_FAT_ENTRIES) == 0 || errno != expected_errno)
        return -1;

    return 0;
}

static int test_chains(void) {
    uint16_t fat[TEST_FAT_ENTRIES];
    uint16_t saved_fat[TEST_FAT_ENTRIES];
    uint16_t blocks[TEST_FAT_ENTRIES];
    vmu_root_t root = { .blk_cnt = VMUFS_STANDARD_USER_BLOCKS };
    vmu_dir_t entry = { .firstblk = 199, .filesize = 2 };

    make_fat(fat);
    fat[199] = 198;
    fat[198] = VMUFS_FAT_EOF;
    if(vmufs_chain_collect(&root, fat, TEST_FAT_ENTRIES, &entry, blocks,
                           TEST_FAT_ENTRIES) < 0 ||
       blocks[0] != 199 || blocks[1] != 198)
        return -1;

    entry.firstblk = VMUFS_STANDARD_USER_BLOCKS;
    if(expect_chain_error(&root, fat, &entry, EILSEQ) < 0)
        return -1;

    make_fat(fat);
    entry.firstblk = 10;
    entry.filesize = 2;
    fat[10] = VMUFS_FAT_EOF;
    if(expect_chain_error(&root, fat, &entry, EILSEQ) < 0)
        return -1;

    make_fat(fat);
    entry.firstblk = 10;
    entry.filesize = 1;
    fat[10] = 11;
    if(expect_chain_error(&root, fat, &entry, EILSEQ) < 0)
        return -1;

    make_fat(fat);
    entry.firstblk = 10;
    entry.filesize = 3;
    fat[10] = 11;
    fat[11] = 10;
    if(expect_chain_error(&root, fat, &entry, EILSEQ) < 0)
        return -1;

    entry.filesize = 0;
    if(expect_chain_error(&root, fat, &entry, EILSEQ) < 0)
        return -1;

    entry.filesize = 2;
    errno = 0;
    if(vmufs_chain_collect(&root, fat, TEST_FAT_ENTRIES, &entry,
                           blocks, 1) == 0 || errno != EINVAL)
        return -1;

    make_fat(fat);
    fat[0] = VMUFS_FAT_EOF;
    memcpy(saved_fat, fat, sizeof(fat));
    errno = 0;
    if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_GAME, 2, blocks,
                            TEST_FAT_ENTRIES) == 0 || errno != ENOSPC ||
       memcmp(fat, saved_fat, sizeof(fat)) != 0)
        return -1;

    make_fat(fat);
    if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_GAME, 2, blocks,
                            TEST_FAT_ENTRIES) < 0 ||
       blocks[0] != 0 || blocks[1] != 1 || fat[0] != 1 ||
       fat[1] != VMUFS_FAT_EOF)
        return -1;
    vmufs_chain_release(fat, blocks, 2);
    if(fat[0] != VMUFS_FAT_FREE || fat[1] != VMUFS_FAT_FREE)
        return -1;

    return 0;
}

static int validate_orphans_only(const vmu_root_t *root,
                                 const uint16_t *fat,
                                 const vmu_dir_t *dir) {
    vmufs_validation_t validation;

    return vmufs_validate(root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                          TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                          &validation) < 0 && errno == EILSEQ &&
           vmufs_validation_allows_mutation(&validation) ? 0 : -1;
}

static int test_commit_prefixes(void) {
    uint16_t fat[TEST_FAT_ENTRIES];
    uint16_t old_blocks[2], new_blocks[2];
    uint16_t source_blocks[2], target_blocks[2];
    uint16_t multi_blocks[3];
    vmu_dir_t dir[TEST_DIR_ENTRIES];
    vmufs_validation_t validation;
    vmu_root_t root;

    /* New save: data writes do not alter metadata; the FAT-only prefix is an
       orphan, and installing the directory makes the filesystem complete. */
    make_card(&root, fat, dir);
    if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_DATA, 2, new_blocks, 2) < 0 ||
       validate_orphans_only(&root, fat, dir) < 0)
        return -1;
    make_entry(&dir[0], "NEW", new_blocks[0], 2);
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                      &validation) < 0)
        return -1;

    /* Replacement: the old file remains valid while the new chain is staged;
       after the directory switches, only the old chain is orphaned. */
    make_card(&root, fat, dir);
    if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_DATA, 2, old_blocks, 2) < 0)
        return -1;
    make_entry(&dir[0], "SAVE", old_blocks[0], 2);
    if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_DATA, 2, new_blocks, 2) < 0 ||
       validate_orphans_only(&root, fat, dir) < 0)
        return -1;
    make_entry(&dir[0], "SAVE", new_blocks[0], 2);
    if(validate_orphans_only(&root, fat, dir) < 0)
        return -1;
    vmufs_chain_release(fat, old_blocks, 2);
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                      &validation) < 0)
        return -1;

    /* Delete: removing the directory first leaves an orphan-only prefix;
       releasing the former chain completes a valid empty filesystem. */
    make_card(&root, fat, dir);
    if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_DATA, 2, old_blocks, 2) < 0)
        return -1;
    make_entry(&dir[0], "DELETE", old_blocks[0], 2);
    memset(&dir[0], 0, sizeof(dir[0]));
    if(validate_orphans_only(&root, fat, dir) < 0)
        return -1;
    vmufs_chain_release(fat, old_blocks, 2);
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                      &validation) < 0)
        return -1;

    /* Replacement rename across directory blocks: removing the destination
       first preserves the source and only orphans the destination chain.
       Publishing the new name preserves that property until FAT cleanup. */
    make_card(&root, fat, dir);
    if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_DATA, 2, source_blocks, 2) < 0 ||
       vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_DATA, 2, target_blocks, 2) < 0)
        return -1;
    make_entry(&dir[0], "SOURCE", source_blocks[0], 2);
    make_entry(&dir[16], "TARGET", target_blocks[0], 2);
    memset(&dir[16], 0, sizeof(dir[16]));
    if(validate_orphans_only(&root, fat, dir) < 0)
        return -1;
    memset(dir[0].filename, 0, sizeof(dir[0].filename));
    memcpy(dir[0].filename, "TARGET", 6);
    if(validate_orphans_only(&root, fat, dir) < 0)
        return -1;
    vmufs_chain_release(fat, target_blocks, 2);
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                      &validation) < 0)
        return -1;

    /* Entries in one directory block can be replaced by a single block
       commit, after which only the replaced chain needs reclamation. */
    make_card(&root, fat, dir);
    if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_DATA, 2, source_blocks, 2) < 0 ||
       vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                            VMUFS_FILETYPE_DATA, 2, target_blocks, 2) < 0)
        return -1;
    make_entry(&dir[0], "SOURCE", source_blocks[0], 2);
    make_entry(&dir[1], "TARGET", target_blocks[0], 2);
    memset(&dir[1], 0, sizeof(dir[1]));
    memset(dir[0].filename, 0, sizeof(dir[0].filename));
    memcpy(dir[0].filename, "TARGET", 6);
    if(validate_orphans_only(&root, fat, dir) < 0)
        return -1;
    vmufs_chain_release(fat, target_blocks, 2);
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                      &validation) < 0)
        return -1;

    /* Multi-delete commits whole directory blocks before releasing the
       chains confirmed by those blocks. Every acknowledged prefix is either
       orphan-only before cleanup or fully consistent after cleanup. */
    for(size_t prefix = 0; prefix <= 3; ++prefix) {
        static const size_t entries[] = {0, 16, 32};

        make_card(&root, fat, dir);
        for(size_t i = 0; i < 3; ++i) {
            uint16_t block;

            if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                                    VMUFS_FILETYPE_DATA, 1, &block, 1) < 0)
                return -1;
            multi_blocks[i] = block;
            make_entry(&dir[entries[i]], "MULTI", block, 1);
            dir[entries[i]].filename[5] = (char)('0' + i);
        }
        for(size_t i = 0; i < prefix; ++i)
            memset(&dir[entries[i]], 0, sizeof(dir[entries[i]]));

        if(prefix == 0) {
            if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                              TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                              &validation) < 0)
                return -1;
        }
        else if(validate_orphans_only(&root, fat, dir) < 0) {
            return -1;
        }

        for(size_t i = 0; i < prefix; ++i)
            vmufs_chain_release(fat, &multi_blocks[i], 1);
        if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                          TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                          &validation) < 0)
            return -1;
    }

    /* An unacknowledged directory write has two possible on-card outcomes.
       If it did not land, its file remains live; if it did, its chain remains
       allocated as an orphan. Neither outcome permits that chain to be reused. */
    make_card(&root, fat, dir);
    for(size_t i = 0; i < 2; ++i) {
        if(vmufs_chain_allocate(&root, fat, TEST_FAT_ENTRIES,
                                VMUFS_FILETYPE_DATA, 1,
                                &multi_blocks[i], 1) < 0)
            return -1;
        make_entry(&dir[i * 16], i ? "MAYBE" : "CONFIRMED",
                   multi_blocks[i], 1);
    }
    memset(&dir[0], 0, sizeof(dir[0]));
    vmufs_chain_release(fat, &multi_blocks[0], 1);
    if(vmufs_validate(&root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      TEST_FAT_ENTRIES, dir, TEST_DIR_ENTRIES,
                      &validation) < 0)
        return -1;
    memset(&dir[16], 0, sizeof(dir[16]));
    if(validate_orphans_only(&root, fat, dir) < 0)
        return -1;

    return 0;
}

static int test_package_round_trip(void) {
    uint8_t icon[512];
    static const uint8_t payload[] = {1, 3, 5, 7, 9};
    vmu_pkg_t source = {0}, parsed;
    uint8_t *encoded = NULL, *copy = NULL, *unaligned = NULL;
    uint16_t stored_crc;
    int encoded_size = 0;
    int result = -1;

    memset(source.desc_short, 'S', sizeof(source.desc_short));
    memset(source.desc_long, 'L', sizeof(source.desc_long));
    memset(source.app_id, 'A', sizeof(source.app_id));
    for(size_t i = 0; i < sizeof(icon); ++i)
        icon[i] = (uint8_t)i;
    source.icon_cnt = 1;
    source.icon_anim_speed = 6;
    source.eyecatch_type = VMUPKG_EC_NONE;
    source.data_len = sizeof(payload);
    source.icon_data = icon;
    source.data = payload;

    if(vmu_pkg_build(&source, &encoded, &encoded_size) < 0 ||
       encoded_size != (int)(sizeof(vmu_hdr_t) + sizeof(icon) +
                             sizeof(payload)))
        goto out;

    memcpy(&stored_crc, encoded + offsetof(vmu_hdr_t, crc),
           sizeof(stored_crc));
    if(stored_crc != 0x740e)
        goto out;

    copy = malloc((size_t)encoded_size);
    unaligned = malloc((size_t)encoded_size + 1u);
    if(!copy || !unaligned)
        goto out;
    memcpy(copy, encoded, (size_t)encoded_size);
    memcpy(unaligned + 1, encoded, (size_t)encoded_size);

    if(vmu_pkg_parse(unaligned + 1, (size_t)encoded_size, &parsed) < 0 ||
       parsed.icon_cnt != 1 || parsed.icon_anim_speed != 6 ||
       parsed.data_len != (int)sizeof(payload) ||
       memcmp(parsed.data, payload, sizeof(payload)) != 0 ||
       parsed.desc_short[16] != '\0' || parsed.desc_long[32] != '\0' ||
       parsed.app_id[16] != '\0' ||
       memcmp(unaligned + 1, copy, (size_t)encoded_size) != 0)
        goto out;

    errno = 0;
    memset(&parsed, 0xa5, sizeof(parsed));
    unaligned[1 + encoded_size - 1] ^= 0x80;
    if(vmu_pkg_parse(unaligned + 1, (size_t)encoded_size, &parsed) == 0 ||
       errno != EILSEQ || parsed.data != NULL || parsed.icon_cnt != 0)
        goto out;
    unaligned[1 + encoded_size - 1] ^= 0x80;

    {
        vmu_hdr_t header;

        memcpy(&header, encoded, sizeof(header));
        header.icon_cnt = 4;
        memcpy(unaligned + 1, &header, sizeof(header));
        errno = 0;
        if(vmu_pkg_parse(unaligned + 1, (size_t)encoded_size,
                         &parsed) == 0 || errno != EILSEQ)
            goto out;

        memcpy(unaligned + 1, encoded, (size_t)encoded_size);
        header.eyecatch_type = VMUPKG_EC_16COL + 1;
        memcpy(unaligned + 1, &header, sizeof(header));
        errno = 0;
        if(vmu_pkg_parse(unaligned + 1, (size_t)encoded_size,
                         &parsed) == 0 || errno != EILSEQ)
            goto out;
        memcpy(unaligned + 1, encoded, (size_t)encoded_size);
    }

    errno = 0;
    if(vmu_pkg_parse(unaligned + 1, (size_t)encoded_size - 1u,
                     &parsed) == 0 || errno != EILSEQ)
        goto out;

    result = 0;

out:
    free(unaligned);
    free(copy);
    free(encoded);
    return result;
}

static int test_package_arguments(void) {
    vmu_pkg_t source = {0};
    uint8_t *encoded = (uint8_t *)(uintptr_t)1;
    int encoded_size = 123;

    source.icon_cnt = 4;
    errno = 0;
    if(vmu_pkg_build(&source, &encoded, &encoded_size) == 0 ||
       errno != EINVAL || encoded != NULL || encoded_size != 0)
        return -1;

    source.icon_cnt = 1;
    errno = 0;
    if(vmu_pkg_build(&source, &encoded, &encoded_size) == 0 ||
       errno != EINVAL || encoded != NULL || encoded_size != 0)
        return -1;

    return 0;
}

int main(void) {
    if(test_chains() < 0 || test_commit_prefixes() < 0 ||
       test_package_round_trip() < 0 ||
       test_package_arguments() < 0) {
        fprintf(stderr, "vmu-storage-test: FAIL errno=%d\n", errno);
        return 1;
    }

    puts("vmu-storage-test: PASS");
    return 0;
}
