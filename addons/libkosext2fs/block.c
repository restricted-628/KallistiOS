/* KallistiOS ##version##

   block.c
   Copyright (C) 2012 Lawrence Sebald
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "ext2fs.h"
#include "block.h"
#include "ext2internal.h"

int ext2_read_blockgroups(ext2_fs_t *fs, uint32_t start_block) {
    uint8_t *buf;
    ext2_bg_desc_t *ptr = fs->bg;
    uint32_t bg_per_block;
    int block_size = 1024 << fs->sb.s_log_block_size;
    uint32_t count = fs->bg_count;

    if(!(buf = (uint8_t *)malloc(block_size)))
        return -ENOMEM;

    bg_per_block = block_size / sizeof(ext2_bg_desc_t);

    while(count) {
        if(ext2_block_read_nc(fs, start_block++, buf)) {
            free(buf);
            return -EIO;
        }

        if(count < bg_per_block) {
            memcpy(ptr, buf, count * sizeof(ext2_bg_desc_t));
            count = 0;
        }
        else {
            memcpy(ptr, buf, bg_per_block * sizeof(ext2_bg_desc_t));
            ptr += bg_per_block;
            count -= bg_per_block;
        }
    }

    free(buf);
    return 0;
}

int ext2_write_blockgroups(ext2_fs_t *fs, uint32_t bg) {
    uint8_t *buf;
    ext2_bg_desc_t *ptr = fs->bg;
    uint32_t bg_per_block;
    uint32_t count = fs->bg_count;
    uint32_t start_block = bg * fs->sb.s_blocks_per_group +
        fs->sb.s_first_data_block + 1;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->mnt_flags & EXT2FS_MNT_FLAG_RW))
        return 0;

    if(!(buf = (uint8_t *)malloc(fs->block_size)))
        return -ENOMEM;

    bg_per_block = fs->block_size / sizeof(ext2_bg_desc_t);

    while(count) {
        if(count < bg_per_block) {
            memcpy(buf, ptr, count * sizeof(ext2_bg_desc_t));
            memset(buf + count * sizeof(ext2_bg_desc_t), 0,
                   (bg_per_block - count) * sizeof(ext2_bg_desc_t));
            count = 0;
        }
        else {
            memcpy(buf, ptr, count * sizeof(ext2_bg_desc_t));
            count -= bg_per_block;
            ptr += bg_per_block;
        }

        if(ext2_block_write_nc(fs, start_block++, buf)) {
            free(buf);
            return -EIO;
        }
    }
    
    free(buf);
    return 0;
}
