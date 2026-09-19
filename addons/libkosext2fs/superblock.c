/* KallistiOS ##version##
 
   superblock.c
   Copyright (C) 2012 Lawrence Sebald
*/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "superblock.h"
#include "utils.h"
#include "ext2internal.h"

int ext2_read_superblock(ext2_superblock_t *sb, kos_blockdev_t *bd) {
    if(bd->l_block_size > 10) {
        uint8_t *buf;

        if(!(buf = (uint8_t *)malloc(1 << bd->l_block_size)))
            return -ENOMEM;

        if(bd->read_blocks(bd, 0, 1, buf))
            return -EIO;

        memcpy(sb, buf + 1024, 1024);
        free(buf);
        return 0;
    }
    else if(bd->l_block_size == 10) {
        return bd->read_blocks(bd, 1, 1, sb);
    }
    else {
        return bd->read_blocks(bd, 1024 >> bd->l_block_size,
                               1024 >> bd->l_block_size, sb);
    }
}

int ext2_write_superblock(struct ext2fs_struct *fs, uint32_t bg) {
    uint8_t *buf;
    ext2_superblock_t *sb;
    kos_blockdev_t *bd = fs->dev;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->flags & EXT2FS_MNT_FLAG_RW))
        return 0;

    /* Handle the first block group specially (i.e, the main superblock)... */
    if(!bg) {
        if(bd->l_block_size > 10) {
            /* Allocate space to read the block in, and do so... */
            if(!(buf = (uint8_t *)malloc(1 << bd->l_block_size)))
                return -ENOMEM;

            if(bd->read_blocks(bd, 0, 1, buf))
                return -EIO;

            /* Copy the superblock in and write it out. */
            memcpy(buf + 1024, &fs->sb, 1024);
            if(bd->write_blocks(bd, 0, 1, buf))
                return -EIO;

            free(buf);
            return 0;
        }
        else if(bd->l_block_size == 10) {
            return bd->write_blocks(bd, 1, 1, &fs->sb);
        }
        else {
            return bd->write_blocks(bd, 1024 >> bd->l_block_size,
                                    1024 >> bd->l_block_size, &fs->sb);
        }
    }
    else {
        /* We need to make space for a whole block here, since we have to do
           writes one block at a time. */
        if(!(buf = (uint8_t *)malloc(fs->block_size)))
            return -ENOMEM;

        memcpy(buf, &fs->sb, 1024);

        if(fs->block_size > 1024)
            memset(buf + 1024, 0, fs->block_size - 1024);

        /* If we're not a revision 0 fs, write the block group number in the
           copy we're writing. */
        if(fs->sb.s_rev_level >= EXT2_DYNAMIC_REV) {
            sb = (ext2_superblock_t *)buf;
            sb->s_block_group_nr = (uint16_t)bg;
        }

        /* Write the block out to the fs. */
        if(ext2_block_write_nc(fs, bg * fs->sb.s_blocks_per_group +
                               fs->sb.s_first_data_block, buf)) {
            free(buf);
            return -EIO;
        }

        free(buf);
        return 0;
    }
}

#ifdef EXT2FS_DEBUG
void ext2_print_superblock(const ext2_superblock_t *sb) {
    dbglog(DBG_KDEBUG, "ext2fs Superblock:\n");
    dbglog(DBG_KDEBUG, "Inode Count: %" PRIu32 "\n", sb->s_inodes_count);
    dbglog(DBG_KDEBUG, "Block Count: %" PRIu32 "\n", sb->s_blocks_count);
    dbglog(DBG_KDEBUG, "Reserved Blocks: %" PRIu32 "\n", sb->s_r_blocks_count);
    dbglog(DBG_KDEBUG, "Free Blocks: %" PRIu32 "\n", sb->s_free_blocks_count);
    dbglog(DBG_KDEBUG, "Free Inodes: %" PRIu32 "\n", sb->s_free_inodes_count);
    dbglog(DBG_KDEBUG, "First Data Block: %" PRIu32 "\n",
           sb->s_first_data_block);
    dbglog(DBG_KDEBUG, "Log Block Size: %" PRIu32 "\n", sb->s_log_block_size);
    dbglog(DBG_KDEBUG, "Log Fragment Size: %" PRIu32 "\n", sb->s_log_frag_size);
    dbglog(DBG_KDEBUG, "Blocks Per Group: %" PRIu32 "\n",
           sb->s_blocks_per_group);
    dbglog(DBG_KDEBUG, "Fragments Per Group: %" PRIu32 "\n",
           sb->s_frags_per_group);
    dbglog(DBG_KDEBUG, "Inodes per Group: %" PRIu32 "\n",
           sb->s_inodes_per_group);
    dbglog(DBG_KDEBUG, "Last Mount Time: %" PRIu32 "\n", sb->s_mtime);
    dbglog(DBG_KDEBUG, "Last Write Time: %" PRIu32 "\n", sb->s_wtime);
    dbglog(DBG_KDEBUG, "Mount counter: %" PRIu16 "\n", sb->s_mnt_count);
    dbglog(DBG_KDEBUG, "Max Mount count: %" PRIu16 "\n", sb->s_max_mnt_count);
    dbglog(DBG_KDEBUG, "Magic value: %04" PRIx16 "\n", sb->s_magic);
    dbglog(DBG_KDEBUG, "State: %04" PRIx16 "\n", sb->s_state);
    dbglog(DBG_KDEBUG, "Error handling: %" PRIu16 "\n", sb->s_errors);
    dbglog(DBG_KDEBUG, "Minor revision: %" PRIu16 "\n", sb->s_minor_rev_level);
    dbglog(DBG_KDEBUG, "Last check: %" PRIu32 "\n", sb->s_lastcheck);
    dbglog(DBG_KDEBUG, "Check Interval: %" PRIu32 "\n", sb->s_checkinterval);
    dbglog(DBG_KDEBUG, "Creator OS: %" PRIu32 "\n", sb->s_creator_os);
    dbglog(DBG_KDEBUG, "Revision Level: %" PRIu32 "\n", sb->s_rev_level);
    dbglog(DBG_KDEBUG, "Default reserved UID: %" PRIu16 "\n", sb->s_def_resuid);
    dbglog(DBG_KDEBUG, "Default reserved GID: %" PRIu16 "\n", sb->s_def_resgid);

    if(sb->s_rev_level >= EXT2_DYNAMIC_REV) {
        dbglog(DBG_KDEBUG, "First Inode: %" PRIu32 "\n", sb->s_first_ino);
        dbglog(DBG_KDEBUG, "Inode Size: %" PRIu16 "\n", sb->s_inode_size);
        dbglog(DBG_KDEBUG, "Block Group #: %" PRIu16 "\n",
               sb->s_block_group_nr);
        dbglog(DBG_KDEBUG, "Compat Features: %08" PRIx32 "\n",
               sb->s_feature_compat);
        dbglog(DBG_KDEBUG, "Incompat Features: %08" PRIx32 "\n",
               sb->s_feature_incompat);
        dbglog(DBG_KDEBUG, "RO Compat Features: %08" PRIx32 "\n",
               sb->s_feature_ro_compat);
        dbglog(DBG_KDEBUG, "UUID: %02x %02x %02x %02x %02x %02x %02x %02x\n"
               "      %02x %02x %02x %02x %02x %02x %02x %02x\n", sb->s_uuid[0],
               sb->s_uuid[1], sb->s_uuid[2], sb->s_uuid[3], sb->s_uuid[4],
               sb->s_uuid[5], sb->s_uuid[6], sb->s_uuid[7], sb->s_uuid[8],
               sb->s_uuid[9], sb->s_uuid[10], sb->s_uuid[11], sb->s_uuid[12],
               sb->s_uuid[13], sb->s_uuid[14], sb->s_uuid[15]);
        dbglog(DBG_KDEBUG, "Volume name: %s\n", sb->s_volume_name);
        dbglog(DBG_KDEBUG, "Last mount dir: %s\n", sb->s_last_mounted);

        if(sb->s_feature_incompat & EXT2_FEATURE_INCOMPAT_COMPRESSION) {
            dbglog(DBG_KDEBUG, "Algorithm bitmap: %08" PRIx32 "\n",
                   sb->s_algo_bitmap);
        }

        dbglog(DBG_KDEBUG, "Preallocate blocks: %" PRIu8 "\n",
               sb->s_prealloc_blocks);

        if(sb->s_feature_compat & EXT2_FEATURE_COMPAT_DIR_PREALLOC) {
            dbglog(DBG_KDEBUG, "Preallocate directory blocks: %" PRIu8 "\n",
                   sb->s_prealloc_dir_blocks);
        }

        if(sb->s_feature_compat & EXT2_FEATURE_COMPAT_HAS_JOURNAL) {
            dbglog(DBG_KDEBUG,
                   "Journal UUID: %02x %02x %02x %02x %02x %02x %02x %02x\n"
                   "              %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   sb->s_journal_uuid[0], sb->s_journal_uuid[1],
                   sb->s_journal_uuid[2], sb->s_journal_uuid[3],
                   sb->s_journal_uuid[4], sb->s_journal_uuid[5],
                   sb->s_journal_uuid[6], sb->s_journal_uuid[7],
                   sb->s_journal_uuid[8], sb->s_journal_uuid[9],
                   sb->s_journal_uuid[10], sb->s_journal_uuid[11],
                   sb->s_journal_uuid[12], sb->s_journal_uuid[13],
                   sb->s_journal_uuid[14], sb->s_journal_uuid[15]);
            dbglog(DBG_KDEBUG, "Journal Inode Number: %" PRIu32 "\n",
                   sb->s_journal_inum);
            dbglog(DBG_KDEBUG, "Journal Device: %" PRIu32 "\n",
                   sb->s_journal_dev);
            dbglog(DBG_KDEBUG, "Last orphan: %" PRIu32 "\n", sb->s_last_orphan);
        }

        if(sb->s_feature_compat & EXT2_FEATURE_COMPAT_DIR_INDEX) {
            dbglog(DBG_KDEBUG, "Hash seed: %08" PRIx32 " %08" PRIx32 
                   " %08" PRIx32 " %08" PRIx32 "\n", sb->s_hash_seed[0],
                   sb->s_hash_seed[1], sb->s_hash_seed[2], sb->s_hash_seed[3]);
            dbglog(DBG_KDEBUG, "Default hash ver: %" PRIu8 "\n",
                   sb->s_def_hash_version);
        }

        dbglog(DBG_KDEBUG, "Default mount options: %08" PRIx32 "\n",
               sb->s_default_mount_options);
        dbglog(DBG_KDEBUG, "First meta block group: %" PRIu32 "\n",
               sb->s_first_meta_bg);
    }
}
#endif /* EXT2FS_DEBUG */
