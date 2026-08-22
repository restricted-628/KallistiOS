/* KallistiOS ##version##

   vmufs.c
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <kos/dbglog.h>
#include <kos/mutex.h>
#include <dc/vmufs.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>

#include "vmufs_internal.h"

/*

This is a whole new module that sits between the fs_vmu module and the maple
VMU driver. It's based loosely on the stuff in the old fs_vmu, but it's been
rewritten and reworked to be clearer, more clean, use threads better, etc.

Unlike the fs_vmu module, this code is stateless. You make a call and you get
back data (or have written it). There are no handles involved or anything
else like that. The new fs_vmu sits on top of this and provides a (mostly)
nice VFS interface similar to the old fs_vmu.

This module tends to do more work than it really needs to for some
functions (like reading a named file) but it does it that way to have very
clear, concise code that can be audited for bugs more easily. It's not
like you load and save on the VMU every frame or something. ;) But
the user may never give your program another frame of time if it corrupts
their save games! If you want better control to save loading and saving
stuff for a big batch of changes, then use the low-level funcs.

Function comments located in vmufs.h.

*/


/* ****************** Low level functions ******************** */


/* We need some sort of access control here for threads. This is somewhat
   less than optimal (one mutex for all VMUs) but I doubt it'll really
   be much of an issue :) */
static mutex_t mutex;

static int mutation_preflight(const vmu_root_t *root,
                              const uint16_t *fat, int fatsize,
                              const vmu_dir_t *dir, int dirsize);

/* Convert a decimal number to BCD; max of two digits */
static uint8_t __pure dec_to_bcd(int dec) {
    uint8_t rv = 0;

    rv = dec % 10;
    rv |= ((dec / 10) % 10) << 4;

    return rv;
}

void vmufs_dir_fill_time(vmu_dir_t *d) {
    struct tm tm;
    int year;

    /* Get the time */
    time_t t = time(NULL);
    localtime_r(&t, &tm);
    year = tm.tm_year + 1900;

    /* Fill in the struct, converting to BCD */
    d->timestamp.cent = dec_to_bcd(year / 100);
    d->timestamp.year = dec_to_bcd(year % 100);
    d->timestamp.month = dec_to_bcd(tm.tm_mon + 1);
    d->timestamp.day = dec_to_bcd(tm.tm_mday);
    d->timestamp.hour = dec_to_bcd(tm.tm_hour);
    d->timestamp.min = dec_to_bcd(tm.tm_min);
    d->timestamp.sec = dec_to_bcd(tm.tm_sec);
    d->timestamp.dow = dec_to_bcd((tm.tm_wday + 6) % 7);
}

int vmufs_root_read(maple_device_t *dev, vmu_root_t *root_buf) {
    /* XXX: Assume root is at 255.. is there some way to figure this out dynamically? */
    if(vmu_block_read(dev, 255, (uint8_t *)root_buf) != 0) {
        dbglog(DBG_ERROR, "vmufs_root_read: can't read block %d on device %c%c\n",
               255, dev->port + 'A', dev->unit + '0');
        return -1;
    }

    return 0;
}

int vmufs_root_write(maple_device_t *dev, vmu_root_t *root_buf) {
    /* XXX: Assume root is at 255.. is there some way to figure this out dynamically? */
    if(vmu_block_write(dev, 255, (uint8_t *)root_buf) != 0) {
        dbglog(DBG_ERROR, "vmufs_root_write: can't write block %d on device %c%c\n",
               255, dev->port + 'A', dev->unit + '0');
        return -1;
    }
    else
        return 0;
}

int vmufs_dir_blocks(vmu_root_t *root_buf) {
    return root_buf->dir_size * 512;
}

int vmufs_fat_blocks(vmu_root_t *root_buf) {
    return root_buf->fat_size * 512;
}

/* Common code for both dir_read and dir_write */
static int vmufs_dir_ops(maple_device_t *dev, vmu_root_t *root, vmu_dir_t *dir_buf, bool write) {
    int rv;
    uint32_t write_buf[VMUFS_BLOCK_SIZE / sizeof(uint32_t)];

    /* Find the directory starting block and length */
    uint16_t dir_block = root->dir_loc;
    uint16_t dir_size = root->dir_size;

    /* The dir is stored backwards, so we start at the end and go back. */
    while(dir_size > 0) {
        bool needsop = false;

        if(write) {
            /* Scan this block for changes */
            for(size_t i = 0;
                i < VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t); ++i) {
                if(dir_buf[i].dirty)
                    needsop = true;
            }
        }
        else
            needsop = true;

        if(needsop) {
            if(!write) {
                rv = vmu_block_read(dev, dir_block, (uint8_t *)dir_buf);
            }
            else {
                vmu_dir_t *write_dir = (vmu_dir_t *)write_buf;

                /* Dirty is an in-memory retry marker, not on-card metadata.
                   Write a sanitized copy so a failure preserves the markers. */
                memcpy(write_buf, dir_buf, sizeof(write_buf));
                for(size_t i = 0;
                    i < VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t); ++i) {
                    write_dir[i].dirty = 0;
                }

                rv = vmu_block_write(dev, dir_block, (uint8_t *)write_buf);
            }

            if(rv != 0) {
                dbglog(DBG_ERROR, "vmufs_dir_%s: can't %s block %d on device %c%c\n",
                       write ? "write" : "read",
                       write ? "write" : "read",
                       (int)dir_block, dev->port + 'A', dev->unit + '0');
                return -1;
            }

            if(write) {
                for(size_t i = 0;
                    i < VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t); ++i) {
                    dir_buf[i].dirty = 0;
                }
            }
        }

        dir_block--;
        dir_size--;
        dir_buf += VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t); /* == 16 */
    }

    return 0;
}

int vmufs_dir_read(maple_device_t *dev, vmu_root_t *root, vmu_dir_t *dir_buf) {
    return vmufs_dir_ops(dev, root, dir_buf, false);
}

int vmufs_dir_write(maple_device_t *dev, vmu_root_t *root, vmu_dir_t *dir_buf) {
    return vmufs_dir_ops(dev, root, dir_buf, true);
}

/* Common code for both fat_read and fat_write */
static int vmufs_fat_ops(maple_device_t *dev, vmu_root_t *root, uint16_t *fat_buf, bool write) {
    int rv;

    /* Find the FAT starting block and length */
    uint16_t fat_block = root->fat_loc;
    uint16_t fat_size = root->fat_size;

    /* We can't reliably handle VMUs with a larger FAT... */
    if(fat_size > 1) {
        dbglog(DBG_ERROR, "vmufs_fat_%s: VMU has >1 (%d) FAT blocks on device %c%c\n",
               write ? "write" : "read",
               (int)fat_size, dev->port + 'A', dev->unit + '0');
        return -1;
    }

    if(!write)
        rv = vmu_block_read(dev, fat_block, (uint8_t *)fat_buf);
    else
        rv = vmu_block_write(dev, fat_block, (uint8_t *)fat_buf);

    if(rv != 0) {
        dbglog(DBG_ERROR, "vmufs_fat_%s: can't %s block %d on device %c%c (error %d)\n",
               write ? "write" : "read",
               write ? "write" : "read",
               (int)fat_block, dev->port + 'A', dev->unit + '0', rv);
        return -2;
    }

    return 0;
}

int vmufs_fat_read(maple_device_t *dev, vmu_root_t *root, uint16_t *fat_buf) {
    return vmufs_fat_ops(dev, root, fat_buf, false);
}

int vmufs_fat_write(maple_device_t *dev, vmu_root_t *root, uint16_t *fat_buf) {
    return vmufs_fat_ops(dev, root, fat_buf, true);
}

int vmufs_dir_find(vmu_root_t *root, vmu_dir_t *dir, const char *fn) {
    int dcnt = root->dir_size * 512 / sizeof(vmu_dir_t);

    for(int i = 0; i < dcnt; i++) {
        /* Not a file -> skip it */
        if(dir[i].filetype == 0)
            continue;

        /* Check the filename */
        if(!strncmp(fn, dir[i].filename, 12))
            return i;
    }

    /* Didn't find anything */
    return -1;
}

int vmufs_dir_add(vmu_root_t *root, vmu_dir_t *dir, vmu_dir_t *newdirent) {
    size_t dcnt = root->dir_size * 512 / sizeof(vmu_dir_t);

    for(size_t i = 0; i < dcnt; i++) {
        /* A file -> skip it */
        if(dir[i].filetype != 0)
            continue;

        /* Copy in the entry */
        memcpy(dir + i, newdirent, sizeof(vmu_dir_t));

        /* Set this entry dirty so its dir block will get written out */
        dir[i].dirty = 1;

        return 0;
    }

    /* Didn't find any open spaces */
    return -1;
}

static int vmufs_file_read_bounded(maple_device_t *dev,
                                   const vmu_root_t *root,
                                   const uint16_t *fat, size_t fat_entries,
                                   const vmu_dir_t *dirent, void *outbuf) {
    uint16_t blocks[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint8_t *out = outbuf;

    if(!dev || !root || !fat || !dirent || !outbuf) {
        errno = EINVAL;
        return -1;
    }

    /* Resolve the complete chain before the first device access. Corrupt
       metadata therefore cannot select a metadata block, index beyond the FAT,
       loop over one block, or leave a partially filled output buffer. */
    if(vmufs_chain_collect(root, fat, fat_entries, dirent, blocks,
                           sizeof(blocks) / sizeof(blocks[0])) < 0) {
        char fn[13] = {0};

        memcpy(fn, dirent->filename, sizeof(dirent->filename));
        dbglog(DBG_ERROR,
               "vmufs_file_read: file '%s' has an invalid FAT chain on "
               "device %c%c\n",
               fn, dev->port + 'A', dev->unit + '0');
        return -1;
    }

    for(size_t i = 0; i < dirent->filesize; ++i) {
        int rv = vmu_block_read(dev, blocks[i], out);

        if(rv != 0) {
            dbglog(DBG_ERROR,
                   "vmufs_file_read: can't read block %u on device %c%c "
                   "(error %d)\n",
                   blocks[i], dev->port + 'A', dev->unit + '0', rv);
            errno = EIO;
            return -2;
        }

        out += VMUFS_BLOCK_SIZE;
    }

    return 0;
}

int vmufs_file_read(maple_device_t *dev, uint16_t *fat,
                    vmu_dir_t *dirent, void *outbuf) {
    vmu_root_t root = {
        .blk_cnt = VMUFS_BLOCK_SIZE / sizeof(uint16_t)
    };

    return vmufs_file_read_bounded(dev, &root, fat,
                                   VMUFS_BLOCK_SIZE / sizeof(*fat),
                                   dirent, outbuf);
}

int vmufs_file_read_ex(maple_device_t *dev, const vmu_root_t *root,
                       const uint16_t *fat, const vmu_dir_t *dirent,
                       void *outbuf) {
    if(vmufs_root_validate(root, VMUFS_STANDARD_CARD_BLOCKS) < 0)
        return -1;

    return vmufs_file_read_bounded(dev, root, fat, root->blk_cnt,
                                   dirent, outbuf);
}

int vmufs_file_write(maple_device_t *dev, vmu_root_t *root, uint16_t *fat,
                     vmu_dir_t *dir, vmu_dir_t *newdirent, void *filebuf, int size) {
    uint16_t blocks[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint8_t *out = (uint8_t *)filebuf;

    if(!dev || !root || !fat || !dir || !newdirent || !filebuf || size <= 0) {
        char fn[13] = {0};
        if(newdirent)
            memcpy(fn, newdirent->filename, sizeof(newdirent->filename));
        dbglog(DBG_ERROR, "vmufs_file_write: file '%s' is too short (%d blocks)\n", fn, size);
        errno = EINVAL;
        return -3;
    }

    if(mutation_preflight(root, fat, VMUFS_BLOCK_SIZE, dir,
                          root->dir_size * VMUFS_BLOCK_SIZE) < 0)
        return -3;

    /* Make sure this file isn't already in the directory */
    if(vmufs_dir_find(root, dir, newdirent->filename) >= 0) {
        char fn[13] = {0};
        memcpy(fn, newdirent->filename, 12);
        dbglog(DBG_ERROR, "vmufs_file_write: file '%s' is already in the dir on device %c%c\n",
               fn, dev->port + 'A', dev->unit + '0');
        return -4;
    }

    if(vmufs_dir_free(root, dir) <= 0) {
        errno = ENOSPC;
        return -6;
    }

    if(vmufs_chain_allocate(root, fat,
                            VMUFS_BLOCK_SIZE / sizeof(*fat),
                            newdirent->filetype, (size_t)size,
                            blocks, sizeof(blocks) / sizeof(blocks[0])) < 0)
        return errno == ENOSPC ? -2 : -3;

    newdirent->firstblk = blocks[0];
    newdirent->filesize = (uint16_t)size;

    for(size_t i = 0; i < (size_t)size; ++i) {
        int rv = vmu_block_write(dev, blocks[i], out);

        if(rv != 0) {
            dbglog(DBG_ERROR, "vmufs_file_write: can't write block %d on device %c%c (error %d)\n",
                   blocks[i], dev->port + 'A', dev->unit + '0', rv);
            vmufs_chain_release(fat, blocks, (size_t)size);
            errno = EIO;
            return -5;
        }

        out += VMUFS_BLOCK_SIZE;
    }

    /* Add the entry to the directory */
    if(vmufs_dir_add(root, dir, newdirent) < 0) {
        dbglog(DBG_ERROR, "vmufs_file_write: can't find an open dirent on device %c%c\n",
               dev->port + 'A', dev->unit + '0');
        vmufs_chain_release(fat, blocks, (size_t)size);
        return -6;
    }

    return 0;
}

int vmufs_file_delete(vmu_root_t *root, uint16_t *fat, vmu_dir_t *dir, const char *fn) {
    uint16_t blocks[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];

    if(!root || !fat || !dir || !fn) {
        errno = EINVAL;
        return -2;
    }

    if(mutation_preflight(root, fat, VMUFS_BLOCK_SIZE, dir,
                          root->dir_size * VMUFS_BLOCK_SIZE) < 0)
        return -2;

    /* Find the file */
    int idx = vmufs_dir_find(root, dir, fn);

    if(idx < 0) {
        dbglog(DBG_ERROR, "vmufs_file_delete: can't find file '%s'\n", fn);
        return -1;
    }

    if(vmufs_chain_collect(root, fat,
                           VMUFS_BLOCK_SIZE / sizeof(*fat),
                           &dir[idx], blocks,
                           sizeof(blocks) / sizeof(blocks[0])) < 0) {
        dbglog(DBG_ERROR,
               "vmufs_file_delete: inconsistency -- corrupt FAT or dir\n");
        return -2;
    }

    vmufs_chain_release(fat, blocks, dir[idx].filesize);

    /* Now clear out its dirent also */
    memset(dir + idx, 0, sizeof(vmu_dir_t));

    /* Set it dirty so it'll be flushed out */
    dir[idx].dirty = 1;

    return 0;
}

/* hee hee :) */
int vmufs_fat_free(vmu_root_t *root, uint16_t *fat) {
    int freeblocks = 0;

    for(size_t i = 0; i < root->blk_cnt; i++) {
        /* only count user blocks */
        if(fat[i] == VMUFS_FAT_FREE)
            freeblocks++;
    }

    return freeblocks;
}

int vmufs_dir_free(vmu_root_t *root, vmu_dir_t *dir) {
    int freeblocks = 0;

    for(size_t i = 0; i < root->dir_size * 512 / sizeof(vmu_dir_t); i++) {
        if(dir[i].filetype == 0)
            freeblocks++;
    }

    return freeblocks;
}

int vmufs_mutex_lock(void) {
    return mutex_lock(&mutex);
}

int vmufs_mutex_unlock(void) {
    return mutex_unlock(&mutex);
}

/* ****************** Higher level functions ******************** */

/* Internal function gets everything setup for you */
static int vmufs_setup(maple_device_t *dev, vmu_root_t *root, vmu_dir_t **dir, int *dirsize,
                       uint16_t **fat, int *fatsize) {
    /* Check to make sure this is a valid device right now */
    if(!dev || !(dev->info.functions & MAPLE_FUNC_MEMCARD)) {
        if(!dev)
            dbglog(DBG_ERROR, "vmufs_setup: device is invalid\n");
        else
            dbglog(DBG_ERROR, "vmufs_setup: device %c%c is not a memory card\n",
                   dev->port + 'A', dev->unit + '0');

        return -1;
    }

    vmufs_mutex_lock();

    /* Read its root block */
    if(!root || vmufs_root_read(dev, root) < 0)
        goto dead;

    /* Reject corrupt or unsupported geometry before it can influence an
       allocation size or physical metadata block number. */
    if(vmufs_root_validate(root, VMUFS_STANDARD_CARD_BLOCKS) < 0) {
        dbglog(DBG_ERROR,
               "vmufs_setup: invalid or unsupported filesystem geometry "
               "on device %c%c\n",
               dev->port + 'A', dev->unit + '0');
        goto dead;
    }

    if(dir) {
        /* Alloc enough space for the whole dir */
        *dirsize = vmufs_dir_blocks(root);
        *dir = (vmu_dir_t *)malloc(*dirsize);

        if(!*dir) {
            dbglog(DBG_ERROR, "vmufs_setup: can't alloc %d bytes for dir on device %c%c\n",
                   *dirsize, dev->port + 'A', dev->unit + '0');
            goto dead;
        }

        /* Ensure that the dir is 0'd to avoid possible uninitialized reads */
        memset(*dir, 0, *dirsize);

        /* Read it */
        if(vmufs_dir_read(dev, root, *dir) < 0) {
            free(*dir);
            *dir = NULL;
            goto dead;
        }
    }

    if(fat) {
        /* Alloc enough space for the fat */
        *fatsize = vmufs_fat_blocks(root);
        *fat = (uint16_t *)malloc(*fatsize);

        if(!*fat) {
            dbglog(DBG_ERROR, "vmufs_setup: can't alloc %d bytes for FAT on device %c%c\n",
                   *fatsize, dev->port + 'A', dev->unit + '0');
            if(dir)
                free(*dir);
            goto dead;
        }

        /* Read it */
        if(vmufs_fat_read(dev, root, *fat) < 0) {
            free(*fat);
            if(dir)
                free(*dir);
            goto dead;
        }
    }

    /* Ok, everything's cool */
    return 0;

dead:
    vmufs_mutex_unlock();
    return -1;
}

/* Internal function to tear everything down for you */
static void vmufs_teardown(vmu_dir_t *dir, uint16_t *fat) {
    if(dir)
        free(dir);

    if(fat)
        free(fat);

    vmufs_mutex_unlock();
}

int vmufs_readdir(maple_device_t *dev, vmu_dir_t **outbuf, int *outcnt) {
    vmu_root_t root;
    vmu_dir_t *dir;
    int dircnt = 0, dirsize, rv = 0;

    *outbuf = NULL;
    *outcnt = 0;

    /* Init everything */
    if(vmufs_setup(dev, &root, &dir, &dirsize, NULL, NULL) < 0)
        return -1;

    /* Go through and move all entries to the lowest-numbered spots. */
    for(size_t i = 0; i < dirsize / sizeof(vmu_dir_t); i++) {
        /* Skip blanks */
        if(dir[i].filetype == 0)
            continue;

        /* Not a blank -- look for an earlier slot that's empty. If
           we don't find one, just leave it alone. */
        for(size_t j = 0; j < i; j++) {
            if(dir[j].filetype == 0) {
                memcpy(dir + j, dir + i, sizeof(vmu_dir_t));
                dir[i].filetype = 0;
                break;
            }
        }

        /* Update the entry count */
        dircnt++;
    }

    /* Resize the buffer to match the number of entries */
    *outcnt = dircnt;
    *outbuf = (vmu_dir_t *)realloc(dir, dircnt * sizeof(vmu_dir_t));

    if(!*outbuf && dircnt) {
        dbglog(DBG_ERROR, "vmufs_readdir: can't realloc %d bytes for dir on device %c%c\n",
               dircnt * sizeof(vmu_dir_t), dev->port + 'A', dev->unit + '0');
        free(dir);
        rv = -2;
        goto ex;
    }

ex:
    vmufs_teardown(NULL, NULL);
    return rv;
}

/* Shared code between read/read_dirent */
static int vmufs_read_common(maple_device_t *dev, const vmu_root_t *root,
                             vmu_dir_t *dirent, uint16_t *fat,
                             void **outbuf, int *outsize) {
    /* Allocate the output space */
    *outsize = dirent->filesize * 512;
    *outbuf = malloc(*outsize);

    if(!*outbuf) {
        dbglog(DBG_ERROR, "vmufs_read: can't alloc %d bytes for reading a file  on device %c%c\n",
               *outsize, dev->port + 'A', dev->unit + '0');
        return -1;
    }

    /* Ok, go ahead and read it */
    if(vmufs_file_read_ex(dev, root, fat, dirent, *outbuf) < 0) {
        free(*outbuf);
        *outbuf = NULL;
        *outsize = 0;
        return -1;
    }

    return 0;
}

int vmufs_read(maple_device_t *dev, const char *fn, void **outbuf, int *outsize) {
    vmu_root_t  root;
    vmu_dir_t   *dir = NULL;
    uint16_t    *fat = NULL;
    int     fatsize, dirsize, idx, rv = 0;

    *outbuf = NULL;
    *outsize = 0;

    /* Init everything */
    if(vmufs_setup(dev, &root, &dir, &dirsize, &fat, &fatsize) < 0)
        return -1;

    /* Look for the file we want */
    idx = vmufs_dir_find(&root, dir, fn);

    if(idx < 0) {
        //dbglog(DBG_ERROR, "vmufs_read: can't find file '%s' on device %c%c\n",
        //  fn, dev->port+'A', dev->unit+'0');
        rv = -2;
        goto ex;
    }

    if(vmufs_read_common(dev, &root, dir + idx, fat, outbuf, outsize) < 0) {
        rv = -3;
        goto ex;
    }

ex:
    vmufs_teardown(dir, fat);
    return rv;
}

int vmufs_read_dirent(maple_device_t *dev, vmu_dir_t *dirent, void **outbuf, int *outsize) {
    vmu_root_t  root;
    uint16_t      *fat = NULL;
    int     fatsize, rv = 0;

    *outbuf = NULL;
    *outsize = 0;

    /* Init everything */
    if(vmufs_setup(dev, &root, NULL, NULL, &fat, &fatsize) < 0)
        return -1;

    if(vmufs_read_common(dev, &root, dirent, fat, outbuf, outsize) < 0)
        rv = -2;

    vmufs_teardown(NULL, fat);
    return rv;
}

/* Returns 0 for success, -7 for 'not enough space', and other values for other errors. :-)  */
static int mutation_preflight(const vmu_root_t *root,
                              const uint16_t *fat, int fatsize,
                              const vmu_dir_t *dir, int dirsize) {
    vmufs_validation_t validation;
    int saved_errno = errno;
    int validation_errno;

    if(vmufs_validate(root, VMUFS_STANDARD_CARD_BLOCKS, fat,
                      (size_t)fatsize / sizeof(*fat), dir,
                      (size_t)dirsize / sizeof(*dir), &validation) == 0)
        return 0;

    validation_errno = errno;
    if(validation_errno == EILSEQ &&
       vmufs_validation_allows_mutation(&validation)) {
        /* Orphans reduce capacity but cannot alias a live file. Preserve them
           until an explicit repair rather than guessing their former owner. */
        dbglog(DBG_WARNING,
               "vmufs: preserving %zu orphan block(s) during mutation\n",
               validation.orphan_blocks);
        errno = saved_errno;
        return 0;
    }

    dbglog(DBG_ERROR,
           "vmufs: refusing mutation of corrupt metadata "
           "(error %d, entry %zu, block %u)\n",
           validation.first_error, validation.first_dir_index,
           validation.first_block);
    errno = validation_errno;
    return -1;
}

static size_t filename_length(const char *filename, size_t maximum) {
    size_t length = 0;

    while(length < maximum && filename[length])
        ++length;

    return length;
}

static bool transaction_cancelled(
    const vmufs_transaction_observer_t *observer) {
    return observer && observer->cancelled &&
           observer->cancelled(observer->data);
}

static void transaction_update(
    const vmufs_transaction_observer_t *observer,
    vmufs_transaction_phase_t phase, size_t completed_blocks,
    size_t total_blocks, size_t data_blocks_completed,
    size_t data_blocks, bool committed) {
    if(observer && observer->update) {
        observer->update(observer->data, phase, completed_blocks,
                         total_blocks, data_blocks_completed,
                         data_blocks, committed);
    }
}

/* Returns 0 for success, -7 for insufficient space, and another negative
   value for validation, media, or commit failure. */
int vmufs_write_observed(maple_device_t *dev, const char *fn,
                         const void *inbuf, int insize, int flags,
                         const vmufs_transaction_observer_t *observer) {
    uint16_t old_blocks[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t new_blocks[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint8_t verify[VMUFS_BLOCK_SIZE];
    vmu_root_t root;
    vmu_dir_t *dir = NULL, nd;
    uint16_t *fat = NULL;
    uint8_t *padded = NULL;
    const uint8_t *file_data;
    size_t fn_length, padded_size, block_count;
    size_t completed_blocks = 0, data_blocks_completed = 0;
    size_t total_blocks, dir_entries;
    bool committed = false;
    int fatsize, dirsize, idx, rv = 0;

    if(!dev || !fn || insize < 0 || (insize && !inbuf) ||
       (flags & ~(VMUFS_OVERWRITE | VMUFS_VMUGAME | VMUFS_NOCOPY))) {
        errno = EINVAL;
        return -1;
    }

    fn_length = filename_length(fn, sizeof(nd.filename) + 1u);
    if(fn_length == 0 || fn_length > sizeof(nd.filename) ||
       (size_t)insize > SIZE_MAX - (VMUFS_BLOCK_SIZE - 1u)) {
        errno = EINVAL;
        return -1;
    }

    padded_size = insize ?
        ((size_t)insize + VMUFS_BLOCK_SIZE - 1u) &
            ~(VMUFS_BLOCK_SIZE - 1u) :
        VMUFS_BLOCK_SIZE;
    block_count = padded_size / VMUFS_BLOCK_SIZE;
    if(block_count > VMUFS_BLOCK_SIZE / sizeof(uint16_t)) {
        errno = ENOSPC;
        return -7;
    }

    if(padded_size != (size_t)insize) {
        padded = calloc(1, padded_size);
        if(!padded)
            return -1;

        if(insize)
            memcpy(padded, inbuf, (size_t)insize);
        file_data = padded;
    }
    else {
        file_data = inbuf;
    }

    if(vmufs_setup(dev, &root, &dir, &dirsize, &fat, &fatsize) < 0) {
        free(padded);
        if(errno == 0)
            errno = EIO;
        return -1;
    }

    /* The on-card dirty byte is not trusted as an instruction to rewrite an
       unrelated directory block. Only this transaction marks its edits. */
    dir_entries = (size_t)dirsize / sizeof(*dir);
    for(size_t i = 0; i < dir_entries; ++i)
        dir[i].dirty = 0;

    if(mutation_preflight(&root, fat, fatsize, dir, dirsize) < 0) {
        rv = -3;
        goto ex;
    }

    idx = vmufs_dir_find(&root, dir, fn);
    if(idx >= 0 && !(flags & VMUFS_OVERWRITE)) {
        errno = EEXIST;
        rv = -2;
        goto ex;
    }

    memset(&nd, 0, sizeof(nd));
    nd.filetype = (flags & VMUFS_VMUGAME) ?
        VMUFS_FILETYPE_GAME : VMUFS_FILETYPE_DATA;
    nd.copyprotect = (flags & VMUFS_NOCOPY) ? 0xff : 0x00;
    memcpy(nd.filename, fn, fn_length);
    vmufs_dir_fill_time(&nd);
    nd.filesize = (uint16_t)block_count;
    nd.hdroff = (flags & VMUFS_VMUGAME) ? 1 : 0;
    nd.dirty = 1;

    if(idx < 0) {
        total_blocks = block_count + 2u;
        transaction_update(observer, VMUFS_TRANSACTION_DATA,
                           0, total_blocks, 0, block_count, false);

        if(vmufs_dir_free(&root, dir) <= 0) {
            errno = ENOSPC;
            rv = -4;
            goto ex;
        }

        if(vmufs_chain_allocate(&root, fat,
                                VMUFS_BLOCK_SIZE / sizeof(*fat),
                                nd.filetype, block_count, new_blocks,
                                sizeof(new_blocks) /
                                    sizeof(new_blocks[0])) < 0) {
            rv = errno == ENOSPC ? -7 : -4;
            goto ex;
        }

        nd.firstblk = new_blocks[0];
        for(size_t i = 0; i < block_count; ++i) {
            if(transaction_cancelled(observer)) {
                vmufs_chain_release(fat, new_blocks, block_count);
                errno = ECANCELED;
                rv = VMUFS_TRANSACTION_CANCELLED;
                goto ex;
            }

            if(vmu_block_write(dev, new_blocks[i],
                               file_data + i * VMUFS_BLOCK_SIZE) != 0) {
                vmufs_chain_release(fat, new_blocks, block_count);
                errno = EIO;
                rv = -4;
                goto ex;
            }

            ++completed_blocks;
            ++data_blocks_completed;
            transaction_update(observer, VMUFS_TRANSACTION_DATA,
                               completed_blocks, total_blocks,
                               data_blocks_completed, block_count, false);
        }

        /* Do not publish allocation metadata until every new data block can
           be read back byte-for-byte. Failure leaves only unreferenced data. */
        for(size_t i = 0; i < block_count; ++i) {
            if(transaction_cancelled(observer)) {
                vmufs_chain_release(fat, new_blocks, block_count);
                errno = ECANCELED;
                rv = VMUFS_TRANSACTION_CANCELLED;
                goto ex;
            }
            if(vmu_block_read(dev, new_blocks[i], verify) != 0 ||
               memcmp(verify, file_data + i * VMUFS_BLOCK_SIZE,
                      VMUFS_BLOCK_SIZE) != 0) {
                vmufs_chain_release(fat, new_blocks, block_count);
                errno = EIO;
                rv = -4;
                goto ex;
            }
        }

        if(transaction_cancelled(observer)) {
            vmufs_chain_release(fat, new_blocks, block_count);
            errno = ECANCELED;
            rv = VMUFS_TRANSACTION_CANCELLED;
            goto ex;
        }

        if(vmufs_dir_add(&root, dir, &nd) < 0) {
            vmufs_chain_release(fat, new_blocks, block_count);
            errno = ENOSPC;
            rv = -4;
            goto ex;
        }

        /* Data first, allocation metadata second, directory last: no visible
           entry can name a chain whose data or FAT has not been committed. */
        transaction_update(observer, VMUFS_TRANSACTION_FAT,
                           completed_blocks, total_blocks,
                           data_blocks_completed, block_count, false);
        if(vmufs_fat_write(dev, &root, fat) < 0) {
            errno = EIO;
            rv = -5;
            goto ex;
        }

        ++completed_blocks;
        transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY,
                           completed_blocks, total_blocks,
                           data_blocks_completed, block_count, false);
        if(vmufs_dir_write(dev, &root, dir) < 0) {
            errno = EIO;
            rv = -6;
            goto ex;
        }

        ++completed_blocks;
        committed = true;
    }
    else {
        vmu_dir_t old_entry = dir[idx];

        total_blocks = block_count + 3u;
        transaction_update(observer, VMUFS_TRANSACTION_DATA,
                           0, total_blocks, 0, block_count, false);

        if(vmufs_chain_collect(&root, fat,
                               VMUFS_BLOCK_SIZE / sizeof(*fat),
                               &old_entry, old_blocks,
                               sizeof(old_blocks) /
                                   sizeof(old_blocks[0])) < 0) {
            errno = EILSEQ;
            rv = -3;
            goto ex;
        }

        /* Keep the old chain allocated and authoritative until a complete new
           chain has been written and its staging FAT is durable. */
        if(vmufs_chain_allocate(&root, fat,
                                VMUFS_BLOCK_SIZE / sizeof(*fat),
                                nd.filetype, block_count, new_blocks,
                                sizeof(new_blocks) /
                                    sizeof(new_blocks[0])) < 0) {
            rv = errno == ENOSPC ? -7 : -4;
            goto ex;
        }

        nd.firstblk = new_blocks[0];
        for(size_t i = 0; i < block_count; ++i) {
            if(transaction_cancelled(observer)) {
                vmufs_chain_release(fat, new_blocks, block_count);
                errno = ECANCELED;
                rv = VMUFS_TRANSACTION_CANCELLED;
                goto ex;
            }

            if(vmu_block_write(dev, new_blocks[i],
                               file_data + i * VMUFS_BLOCK_SIZE) != 0) {
                vmufs_chain_release(fat, new_blocks, block_count);
                errno = EIO;
                rv = -4;
                goto ex;
            }

            ++completed_blocks;
            ++data_blocks_completed;
            transaction_update(observer, VMUFS_TRANSACTION_DATA,
                               completed_blocks, total_blocks,
                               data_blocks_completed, block_count, false);
        }

        for(size_t i = 0; i < block_count; ++i) {
            if(transaction_cancelled(observer)) {
                vmufs_chain_release(fat, new_blocks, block_count);
                errno = ECANCELED;
                rv = VMUFS_TRANSACTION_CANCELLED;
                goto ex;
            }
            if(vmu_block_read(dev, new_blocks[i], verify) != 0 ||
               memcmp(verify, file_data + i * VMUFS_BLOCK_SIZE,
                      VMUFS_BLOCK_SIZE) != 0) {
                vmufs_chain_release(fat, new_blocks, block_count);
                errno = EIO;
                rv = -4;
                goto ex;
            }
        }

        if(transaction_cancelled(observer)) {
            vmufs_chain_release(fat, new_blocks, block_count);
            errno = ECANCELED;
            rv = VMUFS_TRANSACTION_CANCELLED;
            goto ex;
        }

        transaction_update(observer, VMUFS_TRANSACTION_FAT,
                           completed_blocks, total_blocks,
                           data_blocks_completed, block_count, false);
        if(vmufs_fat_write(dev, &root, fat) < 0) {
            errno = EIO;
            rv = -5;
            goto ex;
        }

        ++completed_blocks;
        transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY,
                           completed_blocks, total_blocks,
                           data_blocks_completed, block_count, false);
        memcpy(&dir[idx], &nd, sizeof(nd));
        dir[idx].dirty = 1;
        if(vmufs_dir_write(dev, &root, dir) < 0) {
            errno = EIO;
            rv = -6;
            goto ex;
        }

        ++completed_blocks;
        committed = true;
        transaction_update(observer, VMUFS_TRANSACTION_CLEANUP,
                           completed_blocks, total_blocks,
                           data_blocks_completed, block_count, true);

        /* The new entry is authoritative. Failure here leaks the old blocks
           but cannot damage either the replacement or another live file. */
        vmufs_chain_release(fat, old_blocks, old_entry.filesize);
        if(vmufs_fat_write(dev, &root, fat) < 0) {
            errno = EIO;
            rv = -8;
            goto ex;
        }

        ++completed_blocks;
    }

    transaction_update(observer, VMUFS_TRANSACTION_FINISHED,
                       completed_blocks, total_blocks,
                       data_blocks_completed, block_count, committed);
ex:
    vmufs_teardown(dir, fat);
    free(padded);
    return rv;
}

int vmufs_write(maple_device_t *dev, const char *fn, void *inbuf,
                int insize, int flags) {
    return vmufs_write_observed(dev, fn, inbuf, insize, flags, NULL);
}

int vmufs_delete_observed(
    maple_device_t *dev, const char *fn,
    const vmufs_transaction_observer_t *observer) {
    vmu_root_t root;
    vmu_dir_t *dir = NULL;
    uint16_t *fat = NULL;
    size_t completed_blocks = 0, dir_entries;
    int fatsize, dirsize, rv = 0;

    if(!dev || !fn) {
        errno = EINVAL;
        return -2;
    }

    if(vmufs_setup(dev, &root, &dir, &dirsize, &fat, &fatsize) < 0) {
        if(errno == 0)
            errno = EIO;
        return -2;
    }

    dir_entries = (size_t)dirsize / sizeof(*dir);
    for(size_t i = 0; i < dir_entries; ++i)
        dir[i].dirty = 0;

    if(mutation_preflight(&root, fat, fatsize, dir, dirsize) < 0) {
        rv = -2;
        goto ex;
    }

    rv = vmufs_file_delete(&root, fat, dir, fn);
    if(rv < 0) {
        errno = rv == -1 ? ENOENT : EILSEQ;
        goto ex;
    }

    transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY,
                       0, 2, 0, 0, false);
    if(transaction_cancelled(observer)) {
        errno = ECANCELED;
        rv = VMUFS_TRANSACTION_CANCELLED;
        goto ex;
    }

    /* Removing the directory entry first makes deletion authoritative while
       the old blocks remain allocated. Failed FAT cleanup can only leak space. */
    if(vmufs_dir_write(dev, &root, dir) < 0) {
        errno = EIO;
        rv = -2;
        goto ex;
    }

    ++completed_blocks;
    transaction_update(observer, VMUFS_TRANSACTION_CLEANUP,
                       completed_blocks, 2, 0, 0, true);
    if(vmufs_fat_write(dev, &root, fat) < 0) {
        errno = EIO;
        rv = -2;
        goto ex;
    }

    ++completed_blocks;
    transaction_update(observer, VMUFS_TRANSACTION_FINISHED,
                       completed_blocks, 2, 0, 0, true);
ex:
    vmufs_teardown(dir, fat);
    return rv;
}

int vmufs_delete(maple_device_t *dev, const char *fn) {
    return vmufs_delete_observed(dev, fn, NULL);
}

int vmufs_rename_observed(
    maple_device_t *dev, const char *old_name, const char *new_name,
    const vmufs_transaction_observer_t *observer) {
    uint16_t replaced_blocks[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    vmu_root_t root;
    vmu_dir_t *dir = NULL;
    uint16_t *fat = NULL;
    size_t old_length, new_length, dir_entries;
    size_t completed = 0, total;
    uint16_t replaced_size = 0;
    int fatsize, dirsize, old_index, new_index, rv = -1;
    bool same_directory_block = false;

    if(!dev || !old_name || !new_name) {
        errno = EINVAL;
        return -1;
    }
    old_length = filename_length(old_name, 13u);
    new_length = filename_length(new_name, 13u);
    if(old_length == 0 || old_length > 12u ||
       new_length == 0 || new_length > 12u) {
        errno = EINVAL;
        return -1;
    }

    if(vmufs_setup(dev, &root, &dir, &dirsize, &fat, &fatsize) < 0) {
        if(errno == 0)
            errno = EIO;
        return -1;
    }

    dir_entries = (size_t)dirsize / sizeof(*dir);
    for(size_t i = 0; i < dir_entries; ++i)
        dir[i].dirty = 0;

    if(mutation_preflight(&root, fat, fatsize, dir, dirsize) < 0)
        goto ex;

    old_index = vmufs_dir_find(&root, dir, old_name);
    if(old_index < 0) {
        errno = ENOENT;
        goto ex;
    }
    if(strcmp(old_name, new_name) == 0) {
        transaction_update(observer, VMUFS_TRANSACTION_FINISHED,
                           0, 0, 0, 0, true);
        rv = 0;
        goto ex;
    }

    new_index = vmufs_dir_find(&root, dir, new_name);
    if(new_index >= 0) {
        replaced_size = dir[new_index].filesize;
        if(vmufs_chain_collect(&root, fat,
                               VMUFS_BLOCK_SIZE / sizeof(*fat),
                               &dir[new_index], replaced_blocks,
                               sizeof(replaced_blocks) /
                                   sizeof(replaced_blocks[0])) < 0) {
            errno = EILSEQ;
            goto ex;
        }
        same_directory_block =
            (size_t)old_index /
                (VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t)) ==
            (size_t)new_index /
                (VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t));
        total = same_directory_block ? 2u : 3u;
    }
    else {
        total = 1u;
    }

    transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY,
                       0, total, 0, 0, false);
    if(transaction_cancelled(observer)) {
        errno = ECANCELED;
        rv = VMUFS_TRANSACTION_CANCELLED;
        goto ex;
    }

    if(new_index >= 0) {
        /* Remove the replaced name before releasing its blocks. When the two
           entries occupy different directory blocks this intermediate state
           preserves the source and can only orphan the replaced chain. */
        memset(&dir[new_index], 0, sizeof(dir[new_index]));
        dir[new_index].dirty = 1;
        if(!same_directory_block) {
            if(vmufs_dir_write(dev, &root, dir) < 0) {
                errno = EIO;
                goto ex;
            }
            ++completed;
            transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY,
                               completed, total, 0, 0, false);
        }
    }

    memset(dir[old_index].filename, 0, sizeof(dir[old_index].filename));
    memcpy(dir[old_index].filename, new_name, new_length);
    dir[old_index].dirty = 1;
    if(vmufs_dir_write(dev, &root, dir) < 0) {
        errno = EIO;
        goto ex;
    }

    ++completed;
    if(new_index < 0) {
        transaction_update(observer, VMUFS_TRANSACTION_FINISHED,
                           completed, total, 0, 0, true);
        rv = 0;
        goto ex;
    }

    transaction_update(observer, VMUFS_TRANSACTION_CLEANUP,
                       completed, total, 0, 0, true);
    vmufs_chain_release(fat, replaced_blocks, replaced_size);
    if(vmufs_fat_write(dev, &root, fat) < 0) {
        /* The rename is already visible. The replaced chain is merely
           orphaned if allocation cleanup cannot be written. */
        errno = EIO;
        rv = -2;
        goto ex;
    }

    ++completed;
    transaction_update(observer, VMUFS_TRANSACTION_FINISHED,
                       completed, total, 0, 0, true);
    rv = 0;

ex:
    vmufs_teardown(dir, fat);
    return rv;
}

int vmufs_rename(maple_device_t *dev, const char *old_name,
                 const char *new_name) {
    return vmufs_rename_observed(dev, old_name, new_name, NULL);
}

int vmufs_free_blocks(maple_device_t *dev) {
    vmu_root_t  root;
    uint16_t      *fat = NULL;
    int     fatsize, rv;

    /* Init everything */
    if(vmufs_setup(dev, &root, NULL, NULL, &fat, &fatsize) < 0)
        return -1;

    rv = vmufs_fat_free(&root, fat);

    vmufs_teardown(NULL, fat);
    return rv;
}

int vmufs_free_executable_blocks(maple_device_t *dev) {
    vmu_root_t root;
    uint16_t *fat = NULL;
    size_t free_blocks;
    int fatsize;

    if(vmufs_setup(dev, &root, NULL, NULL, &fat, &fatsize) < 0)
        return -1;

    free_blocks = vmufs_fat_free_executable(
        &root, fat, (size_t)fatsize / sizeof(*fat));
    vmufs_teardown(NULL, fat);
    return (int)free_blocks;
}

int vmufs_init(void) {
    mutex_init(&mutex, MUTEX_TYPE_NORMAL);
    (void)vmufs_request_system_init();
    return 0;
}

int vmufs_shutdown(void) {
    vmufs_request_system_shutdown();
    mutex_destroy(&mutex);
    return 0;
}
