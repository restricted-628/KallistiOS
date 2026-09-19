/* KallistiOS ##version##

   directory.c
   Copyright (C) 2013 Lawrence Sebald
*/

#include <string.h>
#include <errno.h>

#include "ext2fs.h"
#include "ext2internal.h"
#include "directory.h"
#include "inode.h"

/* Calculate the minimum size of a directory entry based on the length of the
   filename. This takes care of making sure that everything aligns nicely on a
   4-byte boundary as well. */
#define DENT_SZ(n) (((n) + sizeof(ext2_dirent_t) + 4) & 0x01FC)

int ext2_dir_is_empty(ext2_fs_t *fs, const struct ext2_inode *dir) {
    uint32_t off, i, blocks;
    ext2_dirent_t *dent;
    uint8_t *buf;
    int err;

    blocks = dir->i_blocks / (2 << fs->sb.s_log_block_size);

    for(i = 0; i < blocks; ++i) {
        off = 0;

        if(!(buf = ext2_inode_read_block(fs, dir, i, NULL, &err)))
            return -err;

        while(off < fs->block_size) {
            dent = (ext2_dirent_t *)(buf + off);

            /* Make sure we don't trip and fall on a malformed entry. */
            if(!dent->rec_len)
                return -1;

            if(dent->inode) {
                /* Check if this is one of the few things we allow in an empty
                   directory (namely '.' and '..'). */
                if(dent->name_len > 2)
                    return 0;
                if(dent->name[0] != '.')
                    return 0;
                if(dent->name_len == 2 && dent->name[1] != '.')
                    return 0;
            }

            off += dent->rec_len;
        }
    }

    return 1;
}

ext2_dirent_t *ext2_dir_entry(ext2_fs_t *fs, const struct ext2_inode *dir,
                              const char *fn) {
    uint32_t off, i, blocks;
    ext2_dirent_t *dent;
    uint8_t *buf;
    size_t len = strlen(fn);
    int err;

    blocks = dir->i_blocks / (2 << fs->sb.s_log_block_size);

    for(i = 0; i < blocks; ++i) {
        off = 0;

        if(!(buf = ext2_inode_read_block(fs, dir, i, NULL, &err)))
            return NULL;

        while(off < fs->block_size) {
            dent = (ext2_dirent_t *)(buf + off);

            /* Make sure we don't trip and fall on a malformed entry. */
            if(!dent->rec_len)
                return NULL;

            if(dent->inode) {
                /* Check if this what we're looking for. */
                if(dent->name_len == len && !memcmp(dent->name, fn, len))
                    return dent;
            }

            off += dent->rec_len;
        }
    }

    /* Didn't find it, oh well. */
    return NULL;
}

int ext2_dir_rm_entry(ext2_fs_t *fs, struct ext2_inode *dir, const char *fn,
                      uint32_t *inode) {
    uint32_t off, i, blocks, bn;
    ext2_dirent_t *dent, *prev;
    uint8_t *buf;
    size_t len = strlen(fn);
    int err;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->mnt_flags & EXT2FS_MNT_FLAG_RW))
        return -EROFS;

    blocks = dir->i_blocks / (2 << fs->sb.s_log_block_size);

    for(i = 0; i < blocks; ++i) {
        off = 0;
        prev = NULL;
        dent = NULL;

        if(!(buf = ext2_inode_read_block(fs, dir, i, &bn, &err)))
            return -err;

        while(off < fs->block_size) {
            prev = dent;
            dent = (ext2_dirent_t *)(buf + off);

            /* Make sure we don't trip and fall on a malformed entry. */
            if(!dent->rec_len)
                return -EIO;

            if(dent->inode) {
                /* Check if this what we're looking for. */
                if(dent->name_len == len && !memcmp(dent->name, fn, len)) {
                    /* Return the inode number to the calling function. */
                    *inode = dent->inode;

                    if(prev) {
                        /* Remove it from the chain and clear the entry. */
                        prev->rec_len += dent->rec_len;
                        memset(dent, 0, dent->rec_len);
                    }
                    else {
                        /* This is the first entry in a block, so simply mark
                           the entry as invalid, and clear the filename and such
                           from it. */
                        dent->inode = 0;
                        memset(dent->name, 0, dent->name_len);
                        dent->name_len = dent->file_type = 0;
                    }

                    /* Mark the block as dirty so that it gets rewritten to the
                       block device. */
                    ext2_block_mark_dirty(fs, bn);

                    /* Since we may well have trashed the tree if we're using a
                       btree directory structure, make sure that we note that
                       by setting that the directory is no longer indexed. */
                    dir->i_flags &= ~EXT2_BTREE_FL;
                    ext2_inode_mark_dirty(dir);
                    return 0;
                }
            }

            off += dent->rec_len;
        }
    }

    /* Didn't find it, oh well. */
    return -ENOENT;
}

static const uint8_t inodetype_to_dirtype[16] = {
    EXT2_FT_UNKNOWN, EXT2_FT_FIFO, EXT2_FT_CHRDEV, EXT2_FT_UNKNOWN,
    EXT2_FT_DIR, EXT2_FT_UNKNOWN, EXT2_FT_BLKDEV, EXT2_FT_UNKNOWN,
    EXT2_FT_REG_FILE, EXT2_FT_UNKNOWN, EXT2_FT_SYMLINK, EXT2_FT_UNKNOWN,
    EXT2_FT_SOCK, EXT2_FT_UNKNOWN, EXT2_FT_UNKNOWN, EXT2_FT_UNKNOWN
};

int ext2_dir_add_entry(ext2_fs_t *fs, struct ext2_inode *dir, const char *fn,
                       uint32_t inode_num, const struct ext2_inode *ent,
                       ext2_dirent_t **rv) {
    uint32_t off, i, blocks, bn;
    ext2_dirent_t *dent;
    uint8_t *buf;
    size_t nlen = strlen(fn);
    uint16_t rlen = DENT_SZ(nlen), tmp;
    int err;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->mnt_flags & EXT2FS_MNT_FLAG_RW))
        return -EROFS;

    blocks = dir->i_blocks / (2 << fs->sb.s_log_block_size);

    for(i = 0; i < blocks; ++i) {
        off = 0;
        dent = NULL;

        if(!(buf = ext2_inode_read_block(fs, dir, i, &bn, &err)))
            return -err;

        while(off < fs->block_size) {
            dent = (ext2_dirent_t *)(buf + off);

            /* Make sure we don't trip and fall on a malformed entry. */
            if(!dent->rec_len)
                return -EIO;

            /* If the entry is filled in, check to make sure it doesn't match
               the name of the entry we're trying to add. */
            if(dent->inode) {
                if(dent->name_len == nlen && !memcmp(dent->name, fn, nlen)) {
                    return -EEXIST;
                }
                else if(dent->rec_len >= rlen + DENT_SZ(dent->name_len)) {
                    /* We have space at the end of this entry... Cut off the
                       empty space*/
                    rlen = dent->rec_len;
                    tmp = dent->rec_len = DENT_SZ(dent->name_len);
                    dent = (ext2_dirent_t *)(buf + off + tmp);
                    dent->rec_len = rlen - tmp;
                    goto fill_it_in;
                }
            }
            /* If it isn't filled in, is there enough space to stick our new
               entry here? */
            else if(dent->rec_len >= rlen) {
                goto fill_it_in;
            }

            off += dent->rec_len;
        }
    }

    /* No space in the existing blocks... Guess we'll have to allocate a new
       block to store this in. */
    if(!(buf = ext2_inode_alloc_block(fs, dir, blocks, &err)))
        return -err;

    dent = (ext2_dirent_t *)buf;
    dent->rec_len = fs->block_size;

    /* Update the directory's size in the inode. */
    dir->i_size += fs->block_size;

    /* Fall through... */
fill_it_in:
    dent->inode = inode_num;
    dent->name_len = (uint8_t)nlen;
    memcpy(dent->name, fn, nlen);

    /* Fill in the file type if applicable to this fs. */
    if(fs->sb.s_rev_level >= EXT2_DYNAMIC_REV &&
       (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE))
        dent->file_type = inodetype_to_dirtype[ent->i_mode >> 12];

    if(rv)
        *rv = dent;

    /* Mark the directory's block as dirty. */
    ext2_block_mark_dirty(fs, bn);

    /* Since we may well have trashed the tree if we're using a btree directory
       structure, make sure that we note that by setting that the directory is
       no longer indexed. */
    dir->i_flags &= ~EXT2_BTREE_FL;
    ext2_inode_mark_dirty(dir);

    return 0;
}

int ext2_dir_create_empty(ext2_fs_t *fs, struct ext2_inode *dir,
                          uint32_t inode_num, uint32_t parent_inode) {
    uint8_t *dir_buf;
    ext2_dirent_t *ent;
    int err;
    uint32_t bg;

    /* Allocate a block for the directory structure. */
    if(!(dir_buf = ext2_inode_alloc_block(fs, dir, 0, &err)))
        return -err;

    /* Fill in "." */
    ent = (ext2_dirent_t *)dir_buf;
    ent->inode = inode_num;
    ent->rec_len = 12;
    ent->name_len = 1;
    ent->file_type = 0;
    ent->name[0] = '.';
    ent->name[1] = ent->name[2] = ent->name[3] = '\0';

    if(fs->sb.s_rev_level >= EXT2_DYNAMIC_REV &&
       (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE))
        ent->file_type = EXT2_FT_DIR;

    /* Fill in ".." */
    ent = (ext2_dirent_t *)(dir_buf + 12);
    ent->inode = parent_inode;
    ent->rec_len = fs->block_size - 12;
    ent->name_len = 2;
    ent->file_type = 0;
    ent->name[0] = ent->name[1] = '.';
    ent->name[2] = ent->name[3] = '\0';
    
    if(fs->sb.s_rev_level >= EXT2_DYNAMIC_REV &&
       (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE))
        ent->file_type = EXT2_FT_DIR;

    /* Fix up some stuff in the inode. */
    dir->i_size = fs->block_size;
    dir->i_links_count = 2;

    /* And update the block group's directory counter. */
    bg = (inode_num - 1) / fs->sb.s_inodes_per_group;
    ++fs->bg[bg].bg_used_dirs_count;
    fs->flags |= EXT2_FS_FLAG_SB_DIRTY;

    /* And, we're done. */
    return 0;
}

int ext2_dir_redir_entry(ext2_fs_t *fs, struct ext2_inode *dir, const char *fn,
                         uint32_t inode_num, ext2_dirent_t **rv) {
    uint32_t off, i, blocks, bn;
    ext2_dirent_t *dent;
    uint8_t *buf;
    size_t nlen = strlen(fn);
    int err;

    /* Don't even bother if we're mounted read-only. */
    if(!(fs->mnt_flags & EXT2FS_MNT_FLAG_RW))
        return -EROFS;

    blocks = dir->i_blocks / (2 << fs->sb.s_log_block_size);

    for(i = 0; i < blocks; ++i) {
        off = 0;
        dent = NULL;

        if(!(buf = ext2_inode_read_block(fs, dir, i, &bn, &err)))
            return -err;

        while(off < fs->block_size) {
            dent = (ext2_dirent_t *)(buf + off);

            /* Make sure we don't trip and fall on a malformed entry. */
            if(!dent->rec_len)
                return -EIO;

            /* If the entry is filled in, check if it is the entry we're trying
               to modify. */
            if(dent->inode) {
                if(dent->name_len == nlen && !memcmp(dent->name, fn, nlen)) {
                    dent->inode = inode_num;
                    ext2_block_mark_dirty(fs, bn);

                    if(rv)
                        *rv = dent;

                    return 0;
                }
            }

            off += dent->rec_len;
        }
    }

    /* Didn't find it... */
    return -ENOENT;
}
