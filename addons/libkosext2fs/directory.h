/* KallistiOS ##version##

   directory.h
   Copyright (C) 2012, 2013 Lawrence Sebald
*/

#ifndef __EXT2_DIRECTORY_H
#define __EXT2_DIRECTORY_H

#include <sys/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>

typedef struct ext2_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    uint8_t name[];
} ext2_dirent_t;

/* Values for file_type */
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

/* Forward declaration... */
struct ext2_inode;

/* Check if a directory is empty. */
int ext2_dir_is_empty(ext2_fs_t *fs, const struct ext2_inode *dir);

/* Find an entry in a directory. */
ext2_dirent_t *ext2_dir_entry(ext2_fs_t *fs, const struct ext2_inode *dir,
                              const char *fn);

/* Delete an entry from a directory. Note that this does nothing about cleaning
   up the inode, but it does tell you which inode you're going to need to clean
   up (or lower the reference count on). */
int ext2_dir_rm_entry(ext2_fs_t *fs, struct ext2_inode *dir, const char *fn,
                      uint32_t *inode);

/* Add an entry to a directory. */
int ext2_dir_add_entry(ext2_fs_t *fs, struct ext2_inode *dir, const char *fn,
                       uint32_t inode_num, const struct ext2_inode *ent,
                       ext2_dirent_t **rv);

/* Create the structure of an empty directory. The inode for the directory
   must already be allocated. */
int ext2_dir_create_empty(ext2_fs_t *fs, struct ext2_inode *dir,
                          uint32_t inode_num, uint32_t parent_inode);

/* Redirect an entry in a directory to a different inode. */
int ext2_dir_redir_entry(ext2_fs_t *fs, struct ext2_inode *dir, const char *fn,
                         uint32_t inode_num, ext2_dirent_t **rv);

__END_DECLS
#endif /* !__EXT2_DIRECTORY_H */
