/* KallistiOS ##version##

   block.h
   Copyright (C) 2012 Lawrence Sebald
*/

#ifndef __EXT2_BLOCK_H
#define __EXT2_BLOCK_H

#include <sys/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>

#include "ext2fs.h"

/** \brief  ext2fs Block group descriptor structure.

    This structure represents a single descriptor for a block group in an ext2
    filesystem. There is one of these for each block group in the filesystem,
    stored right after the superblock (and each of its backups). The amount of
    space used for all of the block group descriptors is rounded up to the
    nearest block boundary.

    The block group descriptor describes the state of a block group, including
    where you can find the block bitmap and how many blocks are free in the
    group.

    Note that the names of the elements in this structure were taken from Dave
    Poirier's Second Extended File System documentation, which is available at
    http://www.nongnu.org/ext2-doc/ . The field names should match those that
    are used in the Linux kernel itself, so if you want better documentation of
    what is in here, either refer to Dave Poirier's document or the Linux kernel
    source code.

    \headerfile ext2/block.h
*/
typedef struct ext2_bg_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t bg_reserved[12];
} ext2_bg_desc_t;

int ext2_read_blockgroups(ext2_fs_t *fs, uint32_t start_block);
int ext2_write_blockgroups(ext2_fs_t *fs, uint32_t bg);

__END_DECLS

#endif /* !__EXT2_BLOCK_H */
