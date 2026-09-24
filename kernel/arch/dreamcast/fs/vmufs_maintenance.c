/* KallistiOS ##version##

   vmufs_maintenance.c
   Copyright (C) 2026 Joseph Black

*/

#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vmufs_internal.h"

#define FAT_ENTRIES (VMUFS_BLOCK_SIZE / sizeof(uint16_t))

static int valid_device(maple_device_t *dev) {
    if(!dev || !(dev->info.functions & MAPLE_FUNC_MEMCARD)) {
        errno = EINVAL;
        return 0;
    }

    return 1;
}

static int transaction_cancelled(
    const vmufs_transaction_observer_t *observer) {
    return observer && observer->cancelled &&
           observer->cancelled(observer->data);
}

static void transaction_update(
    const vmufs_transaction_observer_t *observer,
    vmufs_transaction_phase_t phase, size_t completed_blocks,
    size_t total_blocks, size_t data_blocks_completed, size_t data_blocks,
    bool committed) {
    if(observer && observer->update) {
        observer->update(observer->data, phase, completed_blocks,
                         total_blocks, data_blocks_completed, data_blocks,
                         committed);
    }
}

int vmufs_format_observed(maple_device_t *dev,
                          const vmufs_format_options_t *options,
                          vmufs_format_mode_t mode,
                          const vmufs_transaction_observer_t *observer) {
    vmu_root_t root;
    vmu_root_t verify_root;
    uint16_t fat[FAT_ENTRIES];
    uint16_t verify_fat[FAT_ENTRIES];
    uint8_t blank[VMUFS_BLOCK_SIZE] = {0};
    size_t data_blocks = mode == VMUFS_FORMAT_FULL ? 241u : 0u;
    size_t total_blocks = mode == VMUFS_FORMAT_FULL ? 257u : 16u;
    size_t completed = 0;
    size_t data_completed = 0;
    int rv = -1;

    if(!valid_device(dev) ||
       (mode != VMUFS_FORMAT_QUICK && mode != VMUFS_FORMAT_FULL) ||
       vmufs_format_build(options, &root, fat, FAT_ENTRIES) < 0)
        return -1;

    transaction_update(observer, VMUFS_TRANSACTION_PREPARING, 0,
                       total_blocks, 0, data_blocks, false);
    vmufs_mutex_lock();

    if(transaction_cancelled(observer)) {
        errno = ECANCELED;
        rv = VMUFS_TRANSACTION_CANCELLED;
        goto out;
    }

    /* The invalid root is the destructive barrier. Until the final root is
       published, no valid geometry describes the metadata being replaced. */
    if(vmu_block_write(dev, VMUFS_STANDARD_ROOT_BLOCK, blank) != 0)
        goto io_error;
    ++completed;

    if(mode == VMUFS_FORMAT_FULL) {
        transaction_update(observer, VMUFS_TRANSACTION_ERASING, completed,
                           total_blocks, data_completed, data_blocks, false);
        for(uint16_t block = 0; block <= 240; ++block) {
            if(vmu_block_write(dev, block, blank) != 0)
                goto io_error;
            ++completed;
            ++data_completed;
            transaction_update(observer, VMUFS_TRANSACTION_ERASING,
                               completed, total_blocks, data_completed,
                               data_blocks, false);
        }
    }

    transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY, completed,
                       total_blocks, data_completed, data_blocks, false);
    for(uint16_t block = VMUFS_STANDARD_DIR_BLOCK; block >= 241; --block) {
        if(vmu_block_write(dev, block, blank) != 0)
            goto io_error;
        ++completed;
        transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY, completed,
                           total_blocks, data_completed, data_blocks, false);
    }

    transaction_update(observer, VMUFS_TRANSACTION_FAT, completed,
                       total_blocks, data_completed, data_blocks, false);
    if(vmu_block_write(dev, VMUFS_STANDARD_FAT_BLOCK,
                       (const uint8_t *)fat) != 0)
        goto io_error;
    ++completed;
    transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY, completed,
                       total_blocks, data_completed, data_blocks, false);
    if(vmu_block_write(dev, VMUFS_STANDARD_ROOT_BLOCK,
                       (const uint8_t *)&root) != 0)
        goto io_error;
    ++completed;

    if(vmu_block_read(dev, VMUFS_STANDARD_FAT_BLOCK,
                      (uint8_t *)verify_fat) != 0 ||
       vmu_block_read(dev, VMUFS_STANDARD_ROOT_BLOCK,
                      (uint8_t *)&verify_root) != 0 ||
       memcmp(fat, verify_fat, sizeof(fat)) != 0 ||
       memcmp(&root, &verify_root, sizeof(root)) != 0)
        goto io_error;

    transaction_update(observer, VMUFS_TRANSACTION_FINISHED, completed,
                       total_blocks, data_completed, data_blocks, true);
    rv = 0;
    goto out;

io_error:
    errno = EIO;
out:
    vmufs_mutex_unlock();
    return rv;
}

int vmufs_format(maple_device_t *dev,
                 const vmufs_format_options_t *options,
                 vmufs_format_mode_t mode) {
    return vmufs_format_observed(dev, options, mode, NULL);
}

static int execute_move(maple_device_t *dev, vmu_root_t *root,
                        uint16_t *fat, vmu_dir_t *directory,
                        const vmufs_defrag_schedule_t *schedule,
                        const vmufs_defrag_step_t *step, uint8_t *shadow,
                        const vmufs_transaction_observer_t *observer,
                        size_t *completed, size_t total,
                        size_t *data_completed, size_t data_blocks) {
    const uint16_t *targets = &schedule->targets[step->target_offset];
    vmu_dir_t *entry = &directory[step->directory_index];
    uint16_t old_blocks[FAT_ENTRIES];
    uint16_t staging_fat[FAT_ENTRIES];
    uint8_t verify[VMUFS_BLOCK_SIZE];

    if(vmufs_chain_collect(root, fat, FAT_ENTRIES, entry, old_blocks,
                           FAT_ENTRIES) < 0)
        return -1;

    for(size_t i = 0; i < step->block_count; ++i) {
        if(targets[i] >= root->blk_cnt ||
           fat[targets[i]] != VMUFS_FAT_FREE) {
            errno = EILSEQ;
            return -1;
        }
        if(vmu_block_read(dev, old_blocks[i],
                          shadow + i * VMUFS_BLOCK_SIZE) != 0)
            goto io_error;
        if(transaction_cancelled(observer)) {
            errno = ECANCELED;
            return VMUFS_TRANSACTION_CANCELLED;
        }
    }

    transaction_update(observer, VMUFS_TRANSACTION_DATA, *completed, total,
                       *data_completed, data_blocks, false);
    for(size_t i = 0; i < step->block_count; ++i) {
        if(vmu_block_write(dev, targets[i],
                           shadow + i * VMUFS_BLOCK_SIZE) != 0)
            goto io_error;
        ++*completed;
        ++*data_completed;
        transaction_update(observer, VMUFS_TRANSACTION_DATA, *completed,
                           total, *data_completed, data_blocks, false);
        if(transaction_cancelled(observer)) {
            errno = ECANCELED;
            return VMUFS_TRANSACTION_CANCELLED;
        }
    }

    /* Do not publish a chain until every target block reads back exactly. */
    for(size_t i = 0; i < step->block_count; ++i) {
        if(vmu_block_read(dev, targets[i], verify) != 0 ||
           memcmp(verify, shadow + i * VMUFS_BLOCK_SIZE,
                  VMUFS_BLOCK_SIZE) != 0)
            goto io_error;
        if(transaction_cancelled(observer)) {
            errno = ECANCELED;
            return VMUFS_TRANSACTION_CANCELLED;
        }
    }

    /* The staging FAT is this move's commit barrier. Cancellation before it
       leaves only unallocated target data; after it, finish this move. */
    transaction_update(observer, VMUFS_TRANSACTION_FAT, *completed, total,
                       *data_completed, data_blocks, false);
    memcpy(staging_fat, fat, sizeof(staging_fat));
    for(size_t i = 0; i < step->block_count; ++i) {
        staging_fat[targets[i]] = i + 1u < step->block_count ?
            targets[i + 1u] : VMUFS_FAT_EOF;
    }
    if(vmufs_fat_write(dev, root, staging_fat) < 0)
        goto io_error;
    ++*completed;

    entry->firstblk = targets[0];
    entry->dirty = 1;
    if(vmufs_dir_write(dev, root, directory) < 0)
        goto io_error;
    ++*completed;
    transaction_update(observer, VMUFS_TRANSACTION_DIRECTORY, *completed,
                       total, *data_completed, data_blocks, false);

    memcpy(fat, staging_fat, sizeof(staging_fat));
    vmufs_chain_release(fat, old_blocks, step->block_count);
    if(vmufs_fat_write(dev, root, fat) < 0)
        goto io_error;
    ++*completed;
    transaction_update(observer, VMUFS_TRANSACTION_CLEANUP, *completed,
                       total, *data_completed, data_blocks, false);

    return 0;

io_error:
    errno = EIO;
    return -1;
}

int vmufs_defragment_observed(
    maple_device_t *dev, const vmufs_transaction_observer_t *observer) {
    vmu_root_t root;
    vmu_dir_t *directory = NULL;
    uint16_t *fat = NULL;
    uint8_t *shadow = NULL;
    vmufs_defrag_schedule_t *schedule = NULL;
    size_t directory_bytes;
    size_t maximum_blocks = 0;
    size_t total_blocks = 0;
    size_t data_blocks = 0;
    size_t completed = 0;
    size_t data_completed = 0;
    int rv = -1;

    if(!valid_device(dev))
        return -1;
    transaction_update(observer, VMUFS_TRANSACTION_PREPARING, 0, 0, 0, 0,
                       false);
    vmufs_mutex_lock();

    if(transaction_cancelled(observer)) {
        errno = ECANCELED;
        rv = VMUFS_TRANSACTION_CANCELLED;
        goto out;
    }

    if(vmufs_root_read(dev, &root) < 0)
        goto io_error;
    if(vmufs_root_validate(&root, VMUFS_STANDARD_CARD_BLOCKS) < 0)
        goto out;

    directory_bytes = root.dir_size * VMUFS_BLOCK_SIZE;
    directory = malloc(directory_bytes);
    fat = malloc(VMUFS_BLOCK_SIZE);
    schedule = malloc(sizeof(*schedule));
    if(!directory || !fat || !schedule) {
        errno = ENOMEM;
        goto out;
    }
    if(vmufs_dir_read(dev, &root, directory) < 0 ||
       vmufs_fat_read(dev, &root, fat) < 0)
        goto io_error;

    /* The dirty byte is an in-memory writeback marker, not trusted on-card
       state. Begin with a clean snapshot so each move writes one dir block. */
    for(size_t i = 0; i < directory_bytes / sizeof(*directory); ++i)
        directory[i].dirty = 0;

    if(vmufs_defrag_schedule_build(&root, fat, FAT_ENTRIES, directory,
                                    directory_bytes / sizeof(*directory),
                                    schedule) < 0)
        goto out;

    for(size_t i = 0; i < schedule->step_count; ++i) {
        if(schedule->steps[i].block_count > maximum_blocks)
            maximum_blocks = schedule->steps[i].block_count;
        data_blocks += schedule->steps[i].block_count;
    }
    total_blocks = data_blocks + schedule->step_count * 3u;
    transaction_update(observer, VMUFS_TRANSACTION_PREPARING, 0,
                       total_blocks, 0, data_blocks, false);
    if(maximum_blocks) {
        shadow = malloc(maximum_blocks * VMUFS_BLOCK_SIZE);
        if(!shadow) {
            errno = ENOMEM;
            goto out;
        }
    }

    for(size_t i = 0; i < schedule->step_count; ++i) {
        int move_result;

        if(transaction_cancelled(observer)) {
            errno = ECANCELED;
            rv = VMUFS_TRANSACTION_CANCELLED;
            goto out;
        }
        move_result = execute_move(dev, &root, fat, directory, schedule,
                                   &schedule->steps[i], shadow, observer,
                                   &completed, total_blocks,
                                   &data_completed, data_blocks);
        if(move_result == VMUFS_TRANSACTION_CANCELLED) {
            rv = move_result;
            goto out;
        }
        if(move_result < 0)
            goto out;
    }

    transaction_update(observer, VMUFS_TRANSACTION_FINISHED, completed,
                       total_blocks, data_completed, data_blocks, true);
    rv = 0;
    goto out;

io_error:
    errno = EIO;
out:
    free(shadow);
    free(schedule);
    free(fat);
    free(directory);
    vmufs_mutex_unlock();
    return rv;
}

int vmufs_defragment(maple_device_t *dev) {
    return vmufs_defragment_observed(dev, NULL);
}
