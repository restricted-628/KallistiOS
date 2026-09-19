/* KallistiOS ##version##

   ext2fs.c
   Copyright (C) 2012, 2013 Lawrence Sebald
*/

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "superblock.h"
#include "inode.h"
#include "utils.h"
#include "block.h"
#include "ext2fs.h"
#include "directory.h"
#include "ext2internal.h"

static int initted = 0;

/* This is basically the same as bgrad_cache from fs_iso9660 */
static void make_mru(ext2_fs_t *fs, ext2_cache_t **cache, int block) {
    int i;
    ext2_cache_t *tmp;

    /* Don't try it with the end block */
    if(block < 0 || block >= fs->cache_size - 1)
        return;

    /* Make a copy and scoot everything down */
    tmp = cache[block];

    for(i = block; i < fs->cache_size - 1; ++i) {
        cache[i] = cache[i + 1];
    }

    cache[fs->cache_size - 1] = tmp;
}

/* XXXX: This needs locking! */
uint8_t *ext2_block_read(ext2_fs_t *fs, uint32_t bl, int *err) {
    int i;
    uint8_t *rv;
    ext2_cache_t **cache = fs->bcache;

    /* Look through the cache from the most recently used to the least recently
       used entry. */
    for(i = fs->cache_size - 1; i >= 0; --i) {
        if(cache[i]->block == bl && cache[i]->flags) {
            rv = cache[i]->data;
            make_mru(fs, cache, i);
            goto out;
        }
    }

    /* If we didn't get anything, did we end up with an invalid entry or do we
       need to boot someone out? */
    if(i < 0) {
        i = 0;

        /* Make sure that if the block is dirty, we write it back out. */
        if(cache[0]->flags & EXT2_CACHE_FLAG_DIRTY) {
            if(ext2_block_write_nc(fs, cache[0]->block, cache[0]->data)) {
                /* XXXX: Uh oh... */
                *err = EIO;
                return NULL;
            }
        }
    }

    /* Try to read the block in question. */
    if(ext2_block_read_nc(fs, bl, cache[i]->data)) {
        *err = EIO;
        cache[i]->flags = 0;            /* Mark it as invalid... */
        return NULL;
    }

    cache[i]->block = bl;
    cache[i]->flags = EXT2_CACHE_FLAG_VALID;
    rv = cache[i]->data;
    make_mru(fs, cache, i);

out:
    return rv;
}

int ext2_block_read_nc(ext2_fs_t *fs, uint32_t block_num, uint8_t *rv) {
    int fs_per_block = fs->sb.s_log_block_size - fs->dev->l_block_size + 10;

    if(fs_per_block < 0)
        /* This should never happen, as the ext2 block size must be at least
           as large as the sector size of the block device itself. */
        return -EINVAL;

    if(fs->sb.s_blocks_count <= block_num)
        return -EINVAL;

    if(fs->dev->read_blocks(fs->dev, block_num << fs_per_block,
                            1 << fs_per_block, rv))
        return -EIO;

    return 0;
}

int ext2_block_write_nc(ext2_fs_t *fs, uint32_t block_num, const uint8_t *blk) {
    int fs_per_block = fs->sb.s_log_block_size - fs->dev->l_block_size + 10;

    if(fs_per_block < 0)
        /* This should never happen, as the ext2 block size must be at least
           as large as the sector size of the block device itself. */
        return -EINVAL;

    if(fs->sb.s_blocks_count <= block_num)
        return -EINVAL;

    if(fs->dev->write_blocks(fs->dev, block_num << fs_per_block,
                             1 << fs_per_block, blk))
        return -EIO;

    return 0;
}

int ext2_block_mark_dirty(ext2_fs_t *fs, uint32_t block_num) {
    int i;
    ext2_cache_t **cache = fs->bcache;

    /* Look through the cache from the most recently used to the least recently
       used entry. */
    for(i = fs->cache_size - 1; i >= 0; --i) {
        if(cache[i]->block == block_num && cache[i]->flags) {
            cache[i]->flags |= EXT2_CACHE_FLAG_DIRTY;
            make_mru(fs, cache, i);
            return 0;
        }
    }

    return -EINVAL;
}

int ext2_block_cache_wb(ext2_fs_t *fs) {
    int i, err;
    ext2_cache_t **cache = fs->bcache;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->mnt_flags & EXT2FS_MNT_FLAG_RW))
        return 0;

    for(i = fs->cache_size - 1; i >= 0; --i) {
        if(cache[i]->flags & EXT2_CACHE_FLAG_DIRTY) {
            if((err = ext2_block_write_nc(fs, cache[i]->block, cache[i]->data)))
                return err;

            cache[i]->flags &= ~EXT2_CACHE_FLAG_DIRTY;
        }
    }
    
    return 0;
}

uint8_t *ext2_block_alloc(ext2_fs_t *fs, uint32_t bg, uint32_t *bn, int *err) {
    uint8_t *buf, *blk;
    uint32_t index;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->mnt_flags & EXT2FS_MNT_FLAG_RW)) {
        *err = EROFS;
        return NULL;
    }

    /* See if we have any free blocks at all... */
    if(!fs->sb.s_free_blocks_count) {
        *err = ENOSPC;
        return NULL;
    }

    /* See if we have any free blocks in the block group requested. */
    if(fs->bg[bg].bg_free_blocks_count) {
        if(!(buf = ext2_block_read(fs, fs->bg[bg].bg_block_bitmap, err)))
            return NULL;

        index = ext2_bit_find_zero((uint32_t *)buf, 0,
                                   fs->sb.s_blocks_per_group - 1);
        if(index < fs->sb.s_blocks_per_group) {
            *bn = index + bg * fs->sb.s_blocks_per_group +
                fs->sb.s_first_data_block;

            if(!(blk = ext2_block_read(fs, *bn, err)))
                return NULL;

            ext2_bit_set((uint32_t *)buf, index);
            ext2_block_mark_dirty(fs, fs->bg[bg].bg_block_bitmap);
            --fs->bg[bg].bg_free_blocks_count;
            --fs->sb.s_free_blocks_count;
            fs->flags |= EXT2_FS_FLAG_SB_DIRTY;

            memset(blk, 0, fs->block_size);
            ext2_block_mark_dirty(fs, *bn);
            return blk;
        }

        /* We shouldn't get here... But, just in case, fall through. We should
           probably log an error and tell the user to fsck though. */
        dbglog(DBG_WARNING, "ext2_block_alloc: Block group %" PRIu32 " "
               "indicates that it has free blocks, but doesn't appear to. "
               "Please run fsck on this volume!\n", bg);
    }

    /* Couldn't find a free block in the requested block group... Loop through
       all the block groups looking for a free block. */
    for(bg = 0; bg < fs->bg_count; ++bg) {
        if(fs->bg[bg].bg_free_blocks_count) {
            if(!(buf = ext2_block_read(fs, fs->bg[bg].bg_block_bitmap, err)))
                return NULL;

            index = ext2_bit_find_zero((uint32_t *)buf, 0,
                                       fs->sb.s_blocks_per_group - 1);
            if(index < fs->sb.s_blocks_per_group) {
                *bn = index + bg * fs->sb.s_blocks_per_group +
                    fs->sb.s_first_data_block;

                if(!(blk = ext2_block_read(fs, *bn, err)))
                    return NULL;

                ext2_bit_set((uint32_t *)buf, index);
                ext2_block_mark_dirty(fs, fs->bg[bg].bg_block_bitmap);
                --fs->bg[bg].bg_free_blocks_count;
                --fs->sb.s_free_blocks_count;
                fs->flags |= EXT2_FS_FLAG_SB_DIRTY;

                memset(blk, 0, fs->block_size);
                ext2_block_mark_dirty(fs, *bn);
                return blk;
            }

            /* We shouldn't get here... But, just in case, fall through. We
               should probably log an error and tell the user to fsck though. */
            dbglog(DBG_WARNING, "ext2_block_alloc: Block group %" PRIu32 " "
                   "indicates that it has free blocks, but doesn't appear to. "
                   "Please run fsck on this volume!\n", bg);
        }
    }

    /* Uh oh... We went through everything and didn't find any. That means the
       data in the superblock is wrong. */
    dbglog(DBG_WARNING, "ext2_block_alloc: Filesystem indicates that it has "
           "free blocks, but doesn't appear to. Please run fsck on this "
           "volume!\n");
    *err = ENOSPC;
    return NULL;
}

uint32_t ext2_block_size(const ext2_fs_t *fs) {
    return fs->block_size;
}

uint32_t ext2_log_block_size(const ext2_fs_t *fs) {
    return fs->sb.s_log_block_size + 10;
}

int ext2_init(void) {
    ext2_inode_init();
    initted = 1;

    return 0;
}

ext2_fs_t *ext2_fs_init(kos_blockdev_t *bd, uint32_t flags) {
    return ext2_fs_init_ex(bd, flags, EXT2_CACHE_BLOCKS);
}

ext2_fs_t *ext2_fs_init_ex(kos_blockdev_t *bd, uint32_t flags, int cache_sz) {
    ext2_fs_t *rv;
    uint32_t bc;
    int j;
    int block_size;

#ifdef EXT2FS_DEBUG
    uint32_t tmp;
    uint32_t p3 = 3, p5 = 5, p7 = 7, i;
#endif

    /* Make sure we've initialized any of the lower-level stuff. */
    if(!initted) {
        if(ext2_init())
            return NULL;
    }

    if(bd->init(bd))
        return NULL;

    if(!(rv = (ext2_fs_t *)malloc(sizeof(ext2_fs_t)))) {
        bd->shutdown(bd);
        return NULL;
    }

    rv->dev = bd;
    rv->mnt_flags = flags & EXT2FS_MNT_VALID_FLAGS_MASK;

    if(rv->mnt_flags != flags) {
        dbglog(DBG_WARNING, "ext2_fs_init: unknown mount flags: %08" PRIx32
               "\n", flags);
        dbglog(DBG_WARNING, "              mounting read-only\n");
        rv->mnt_flags = 0;
    }

    /* Read in the all-important superblock. */
    if(ext2_read_superblock(&rv->sb, bd)) {
        free(rv);
        bd->shutdown(bd);
        return NULL;
    }

    rv->block_size = block_size = 1024 << rv->sb.s_log_block_size;

#ifdef EXT2FS_DEBUG
    ext2_print_superblock(&rv->sb);
#endif

    /* Figure out how many block groups we have, based on the number of blocks
       and the blocks per group.
       Should we check this against the inodes too? */
    bc = rv->sb.s_blocks_count - rv->sb.s_first_data_block;
    rv->bg_count = bc / rv->sb.s_blocks_per_group;
    if(bc % rv->sb.s_blocks_per_group)
        ++rv->bg_count;

#ifdef EXT2FS_DEBUG
    dbglog(DBG_KDEBUG, "ext2fs has %" PRIu32 " block groups\n", rv->bg_count);

    dbglog(DBG_KDEBUG, "Superblocks are stored on the following blocks:\n");
    tmp = rv->sb.s_first_data_block;

    if(rv->sb.s_rev_level < EXT2_DYNAMIC_REV ||
       !(rv->sb.s_feature_ro_compat & EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)) {
        while(tmp < bc) {
            dbglog(DBG_KDEBUG, "%" PRIu32 "\n", tmp);
            tmp += rv->sb.s_blocks_per_group;
        }
    }
    else {
        dbglog(DBG_KDEBUG, "%" PRIu32 "\n", tmp);
        tmp += rv->sb.s_blocks_per_group;
        if(tmp < bc)
            dbglog(DBG_KDEBUG, "%" PRIu32 "\n", tmp);

        while(tmp < bc) {
            if(p3 < p5 && p3 < p7) {
                tmp = rv->sb.s_first_data_block +
                    rv->sb.s_blocks_per_group * p3;

                if(tmp < bc)
                    dbglog(DBG_KDEBUG, "%" PRIu32 "\n", tmp);
                p3 *= 3;
            }
            else if(p5 < p3 && p5 < p7) {
                tmp = rv->sb.s_first_data_block +
                    rv->sb.s_blocks_per_group * p5;

                if(tmp < bc)
                    dbglog(DBG_KDEBUG, "%" PRIu32 "\n", tmp);
                p5 *= 5;
            }
            else {
                tmp = rv->sb.s_first_data_block +
                    rv->sb.s_blocks_per_group * p7;

                if(tmp < bc)
                    dbglog(DBG_KDEBUG, "%" PRIu32 "\n", tmp);
                p7 *= 7;
            }
        }
    }
#endif /* EXT2FS_DEBUG */

    /* Allocate space for and read in the block group descriptors. */
    if(!(rv->bg = (ext2_bg_desc_t *)malloc(sizeof(ext2_bg_desc_t) *
                                           rv->bg_count))) {
        free(rv);
        bd->shutdown(bd);
        return NULL;
    }

    if(ext2_read_blockgroups(rv, rv->sb.s_first_data_block + 1)) {
        free(rv->bg);
        free(rv);
        bd->shutdown(bd);
        return NULL;
    }

#ifdef EXT2FS_DEBUG
    for(i = 0; i < rv->bg_count; ++i) {
        dbglog(DBG_KDEBUG, "Block Group %" PRIu32 " info:\n", i);
        dbglog(DBG_KDEBUG, "Block Bitmap @ %" PRIu32 "\n",
               rv->bg[i].bg_block_bitmap);
        dbglog(DBG_KDEBUG, "Inode Bitmap @ %" PRIu32 "\n",
               rv->bg[i].bg_inode_bitmap);
        dbglog(DBG_KDEBUG, "Inode Table @ %" PRIu32 "\n",
               rv->bg[i].bg_inode_table);
        dbglog(DBG_KDEBUG, "Free blocks: %" PRIu16 "\n",
               rv->bg[i].bg_free_blocks_count);
        dbglog(DBG_KDEBUG, "Free inodes: %" PRIu16 "\n",
               rv->bg[i].bg_free_inodes_count);
        dbglog(DBG_KDEBUG, "Directory inodes: %" PRIu16 "\n",
               rv->bg[i].bg_used_dirs_count);
    }
#endif /* EXT2FS_DEBUG */

    /* Make space for the block cache. */
    if(!(rv->bcache = (ext2_cache_t **)malloc(sizeof(ext2_cache_t *) *
                                              cache_sz))) {
        free(rv->bg);
        free(rv);
        bd->shutdown(bd);
        return NULL;
    }

    for(j = 0; j < cache_sz; ++j) {
        if(!(rv->bcache[j] = (ext2_cache_t *)malloc(sizeof(ext2_cache_t))))
            goto out_cache;
    }

    for(j = 0; j < cache_sz; ++j) {
        if(!(rv->bcache[j]->data = (uint8_t *)malloc(block_size)))
            goto out_bcache;

        rv->bcache[j]->flags = 0;
    }

    rv->cache_size = cache_sz;

    return rv;

out_bcache:
    for(; j >= 0; --j) {
        free(rv->bcache[j]->data);
    }

    j = cache_sz - 1;
out_cache:
    for(; j >= 0; --j) {
        free(rv->bcache[j]);
    }

    free(rv->bcache);
    free(rv->bg);
    free(rv);
    bd->shutdown(bd);
    return NULL;
}

int ext2_fs_sync(ext2_fs_t *fs) {
    int rv, frv = 0;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->mnt_flags & EXT2FS_MNT_FLAG_RW))
        return 0;

    /* Do a write-back on the inode cache first, pushing the changes out to the
       block cache. */
    if((rv = ext2_inode_cache_wb(fs))) {
        dbglog(DBG_ERROR, "ext2_fs_sync: Error writing back the inode cache: "
               "%s.\n", strerror(-rv));
        errno = -rv;
        frv = -1;
    }

    /* Do a write-back on the block cache next, which should take care of all
       the writes other than superblock(s) and block group descriptors. */
    if((rv = ext2_block_cache_wb(fs))) {
        dbglog(DBG_ERROR, "ext2_fs_sync: Error writing back the block cache: "
               "%s.\n", strerror(-rv));
        errno = -rv;
        frv = -1;
    }

    if((fs->flags & EXT2_FS_FLAG_SB_DIRTY)) {
        /* Write the main superblock and the block group descriptors. */
        if((rv = ext2_write_superblock(fs, 0))) {
            dbglog(DBG_ERROR, "ext2_fs_sync: Error writing back the main "
                   "superblock: %s.\n", strerror(-rv));
            dbglog(DBG_ERROR, "              Your filesystem is possibly toast "
                   "at this point... Run e2fsck ASAP.\n");
            errno = -rv;
            frv = -1;
        }

        if((rv = ext2_write_blockgroups(fs, 0))) {
            dbglog(DBG_ERROR, "ext2_fs_sync: Error writing back the main "
                   "block group descriptors: %s.\n", strerror(-rv));
            errno = -rv;
            frv = -1;
        }
    }

    return frv;
}

void ext2_fs_shutdown(ext2_fs_t *fs) {
    int i;

    /* Sync the filesystem back to the block device, if needed. */
    ext2_fs_sync(fs);

    for(i = 0; i < fs->cache_size; ++i) {
        free(fs->bcache[i]->data);
        free(fs->bcache[i]);
    }

    free(fs->bcache);
    fs->dev->shutdown(fs->dev);
    free(fs->bg);
    free(fs);
}
