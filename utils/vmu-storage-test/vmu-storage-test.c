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
    if(test_chains() < 0 || test_package_round_trip() < 0 ||
       test_package_arguments() < 0) {
        fprintf(stderr, "vmu-storage-test: FAIL errno=%d\n", errno);
        return 1;
    }

    puts("vmu-storage-test: PASS");
    return 0;
}
