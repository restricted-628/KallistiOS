/* KallistiOS ##version##

   symlink.c
   Copyright (C) 2012 Lawrence Sebald
*/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ext2fs.h"
#include "ext2internal.h"
#include "inode.h"

int ext2_resolve_symlink(ext2_fs_t *fs, ext2_inode_t *inode, char *rv,
                         size_t *rv_len) {
    uint32_t xabl = inode->i_file_acl ? 1 << (fs->sb.s_log_block_size + 1) : 0;
    uint32_t len = *rv_len;
    uint32_t bs, i, bcnt;
    uint8_t *buf;
    int err;

    if((inode->i_mode & 0xF000) != EXT2_S_IFLNK)
        return -EINVAL;

    /* Is the link's destination stored in the inode itself? */
    if(inode->i_blocks == xabl) {
        /* Linux only stores the symlink in the inode itself if the whole thing,
           including a NUL terminator, can fit in the inode's i_block array.
           Since it actually guarantees the NUL terminator, we can just use
           strncpy here nicely. Note that the value in inode->i_size is actually
           just the strlen() of the link's destination and thus does not contain
           the NUL terminator in its count. */
        strncpy(rv, (const char *)inode->i_block, len);
    }
    else {
        /* The symlink's destination is stored in one or more data blocks... */
        bs = fs->block_size;
        bcnt = (inode->i_blocks - xabl) >> (fs->sb.s_log_block_size + 1);

        /* In Linux, symlinks should be limited to one page in length. That
           means that under normal circumstances, we should be limited to 4096
           bytes (which is, conveniently, our PATH_MAX on KOS). */
        for(i = 0; i < bcnt && len; ++i) {
            buf = ext2_inode_read_block(fs, inode, i, NULL, &err);

            if(len > bs) {
                memcpy(rv, buf, bs);
                rv += bs;
                len -= bs;
                *rv = 0;
            }
            else {
                memcpy(rv, buf, len);
                rv[len - 1] = 0;
                len = 0;
            }
        }
    }

    *rv_len = inode->i_size;
    return 0;
}
