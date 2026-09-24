/* KallistiOS ##version##

   vmufs_defrag_schedule.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vmufs_internal.h"

#define FAT_ENTRIES (VMUFS_BLOCK_SIZE / sizeof(uint16_t))
#define DIR_ENTRIES (VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t) * \
                     VMUFS_STANDARD_DIR_BLOCKS)

static bool active_entry(const vmu_dir_t *entry) {
    return entry->filetype == VMUFS_FILETYPE_DATA ||
           entry->filetype == VMUFS_FILETYPE_GAME;
}

static int append_step(vmufs_defrag_schedule_t *schedule,
                       size_t directory_index, const uint16_t *targets,
                       size_t block_count, bool final_target) {
    vmufs_defrag_step_t *step;

    if(directory_index > UINT16_MAX || block_count > UINT16_MAX ||
       schedule->step_count >= VMUFS_DEFRAG_MAX_STEPS ||
       schedule->target_count > VMUFS_DEFRAG_MAX_TARGETS - block_count) {
        errno = EOVERFLOW;
        return -1;
    }

    step = &schedule->steps[schedule->step_count++];
    step->directory_index = (uint16_t)directory_index;
    step->target_offset = (uint16_t)schedule->target_count;
    step->block_count = (uint16_t)block_count;
    step->final_target = final_target;
    memcpy(&schedule->targets[schedule->target_count], targets,
           block_count * sizeof(*targets));
    schedule->target_count += block_count;
    if(!final_target)
        ++schedule->staged_steps;

    return 0;
}

static int apply_move(const vmu_root_t *root, uint16_t *fat,
                      vmu_dir_t *entry, const uint16_t *targets,
                      size_t block_count) {
    uint16_t old_blocks[FAT_ENTRIES];

    if(vmufs_chain_collect(root, fat, FAT_ENTRIES, entry, old_blocks,
                           sizeof(old_blocks) / sizeof(old_blocks[0])) < 0)
        return -1;

    for(size_t i = 0; i < block_count; ++i) {
        if(targets[i] >= root->blk_cnt ||
           fat[targets[i]] != VMUFS_FAT_FREE) {
            errno = EILSEQ;
            return -1;
        }
    }

    vmufs_chain_release(fat, old_blocks, block_count);
    for(size_t i = 0; i < block_count; ++i) {
        fat[targets[i]] = i + 1u < block_count ?
            targets[i + 1u] : VMUFS_FAT_EOF;
    }
    entry->firstblk = targets[0];

    return 0;
}

static int build_owners(const vmu_root_t *root, const uint16_t *fat,
                        const vmu_dir_t *directory, size_t entry_count,
                        size_t *owners) {
    uint16_t blocks[FAT_ENTRIES];

    for(size_t block = 0; block < root->blk_cnt; ++block)
        owners[block] = SIZE_MAX;

    for(size_t entry = 0; entry < entry_count; ++entry) {
        if(!active_entry(&directory[entry]))
            continue;
        if(vmufs_chain_collect(root, fat, FAT_ENTRIES, &directory[entry],
                               blocks, FAT_ENTRIES) < 0)
            return -1;
        for(size_t i = 0; i < directory[entry].filesize; ++i)
            owners[blocks[i]] = entry;
    }

    return 0;
}

static bool same_chain(const vmu_root_t *root, const uint16_t *current_fat,
                       const vmu_dir_t *current_entry,
                       const uint16_t *final_fat,
                       const vmu_dir_t *final_entry) {
    uint16_t current[FAT_ENTRIES];
    uint16_t final[FAT_ENTRIES];

    if(current_entry->filesize != final_entry->filesize ||
       vmufs_chain_collect(root, current_fat, FAT_ENTRIES, current_entry,
                           current, FAT_ENTRIES) < 0 ||
       vmufs_chain_collect(root, final_fat, FAT_ENTRIES, final_entry,
                           final, FAT_ENTRIES) < 0)
        return false;

    return memcmp(current, final,
                  current_entry->filesize * sizeof(current[0])) == 0;
}

static int first_blocker(const vmu_root_t *root,
                         const uint16_t *current_fat,
                         const size_t *owners, const uint16_t *final_fat,
                         const vmu_dir_t *final_entry) {
    uint16_t targets[FAT_ENTRIES];

    if(vmufs_chain_collect(root, final_fat, FAT_ENTRIES, final_entry,
                           targets, FAT_ENTRIES) < 0)
        return -1;

    for(size_t i = 0; i < final_entry->filesize; ++i) {
        if(current_fat[targets[i]] != VMUFS_FAT_FREE) {
            if(owners[targets[i]] == SIZE_MAX ||
               owners[targets[i]] > INT_MAX) {
                errno = EILSEQ;
                return -1;
            }
            return (int)owners[targets[i]];
        }
    }

    return -2;
}

static int choose_cycle_file(const vmu_root_t *root,
                             const uint16_t *current_fat,
                             const size_t *owners,
                             const uint16_t *final_fat,
                             const vmu_dir_t *final_directory,
                             const bool *done, const bool *staged,
                             size_t entry_count, size_t scratch_count,
                             size_t *selected) {
    size_t path[DIR_ENTRIES];
    size_t position[DIR_ENTRIES];
    size_t best = SIZE_MAX;
    size_t best_blocks = SIZE_MAX;

    for(size_t start = 0; start < entry_count; ++start) {
        size_t path_count = 0;
        size_t current = start;

        if(!active_entry(&final_directory[start]) || done[start])
            continue;
        for(size_t i = 0; i < entry_count; ++i)
            position[i] = SIZE_MAX;

        while(current < entry_count && !done[current] &&
              active_entry(&final_directory[current]) &&
              position[current] == SIZE_MAX) {
            int blocker;

            position[current] = path_count;
            path[path_count++] = current;
            blocker = first_blocker(root, current_fat, owners, final_fat,
                                    &final_directory[current]);
            if(blocker < 0)
                break;
            current = (size_t)blocker;
        }

        if(current >= entry_count || position[current] == SIZE_MAX)
            continue;

        for(size_t i = position[current]; i < path_count; ++i) {
            size_t candidate = path[i];
            size_t blocks = final_directory[candidate].filesize;

            if(!staged[candidate] && blocks <= scratch_count &&
               blocks < best_blocks) {
                best = candidate;
                best_blocks = blocks;
            }
        }
    }

    if(best == SIZE_MAX) {
        errno = ENOSPC;
        return -1;
    }

    *selected = best;
    return 0;
}

int vmufs_defrag_schedule_build(const vmu_root_t *root,
                                const uint16_t *fat, size_t fat_entries,
                                const vmu_dir_t *directory,
                                size_t directory_entries,
                                vmufs_defrag_schedule_t *schedule) {
    vmufs_defrag_plan_t *plan = NULL;
    vmu_dir_t *final_directory = NULL;
    vmu_dir_t *current_directory = NULL;
    uint16_t current_fat[FAT_ENTRIES];
    size_t owners[FAT_ENTRIES];
    bool done[DIR_ENTRIES] = {false};
    bool staged[DIR_ENTRIES] = {false};
    size_t entry_count;
    size_t remaining = 0;
    int rv = -1;

    if(!root || !fat || !directory || !schedule ||
       fat_entries < FAT_ENTRIES || root->dir_size !=
       VMUFS_STANDARD_DIR_BLOCKS) {
        errno = EINVAL;
        return -1;
    }

    entry_count = root->dir_size * VMUFS_BLOCK_SIZE / sizeof(vmu_dir_t);
    if(entry_count > DIR_ENTRIES || directory_entries < entry_count) {
        errno = EINVAL;
        return -1;
    }

    plan = malloc(sizeof(*plan));
    final_directory = malloc(entry_count * sizeof(*final_directory));
    current_directory = malloc(entry_count * sizeof(*current_directory));
    if(!plan || !final_directory || !current_directory) {
        errno = ENOMEM;
        goto out;
    }

    memcpy(final_directory, directory,
           entry_count * sizeof(*final_directory));
    memcpy(current_directory, directory,
           entry_count * sizeof(*current_directory));
    memcpy(current_fat, fat, sizeof(current_fat));
    if(vmufs_defrag_plan_build(root, fat, fat_entries, final_directory,
                               entry_count, plan) < 0)
        goto out;

    memset(schedule, 0, sizeof(*schedule));
    for(size_t entry = 0; entry < entry_count; ++entry) {
        if(!active_entry(&final_directory[entry]))
            continue;
        done[entry] = same_chain(root, current_fat,
                                 &current_directory[entry], plan->fat,
                                 &final_directory[entry]);
        if(!done[entry])
            ++remaining;
    }

    while(remaining) {
        bool progressed = false;

        if(build_owners(root, current_fat, current_directory, entry_count,
                        owners) < 0)
            goto out;

        for(size_t entry = 0; entry < entry_count; ++entry) {
            uint16_t targets[FAT_ENTRIES];
            bool available = true;

            if(!active_entry(&final_directory[entry]) || done[entry])
                continue;
            if(vmufs_chain_collect(root, plan->fat, FAT_ENTRIES,
                                   &final_directory[entry], targets,
                                   FAT_ENTRIES) < 0)
                goto out;
            for(size_t i = 0; i < final_directory[entry].filesize; ++i)
                available &= current_fat[targets[i]] == VMUFS_FAT_FREE;
            if(!available)
                continue;

            if(append_step(schedule, entry, targets,
                           final_directory[entry].filesize, true) < 0 ||
               apply_move(root, current_fat, &current_directory[entry],
                          targets, final_directory[entry].filesize) < 0)
                goto out;
            done[entry] = true;
            --remaining;
            progressed = true;
        }

        if(progressed)
            continue;

        {
            uint16_t scratch[FAT_ENTRIES];
            size_t scratch_count = 0;
            size_t selected;

            for(size_t block = 0; block < root->blk_cnt; ++block) {
                if(current_fat[block] == VMUFS_FAT_FREE &&
                   plan->fat[block] == VMUFS_FAT_FREE)
                    scratch[scratch_count++] = (uint16_t)block;
            }

            if(choose_cycle_file(root, current_fat, owners, plan->fat,
                                 final_directory, done, staged, entry_count,
                                 scratch_count, &selected) < 0)
                goto out;
            if(append_step(schedule, selected, scratch,
                           final_directory[selected].filesize, false) < 0 ||
               apply_move(root, current_fat,
                          &current_directory[selected], scratch,
                          final_directory[selected].filesize) < 0)
                goto out;
            staged[selected] = true;
        }
    }

    if(memcmp(current_fat, plan->fat, sizeof(current_fat)) != 0) {
        errno = EILSEQ;
        goto out;
    }
    for(size_t entry = 0; entry < entry_count; ++entry) {
        if(active_entry(&final_directory[entry]) &&
           !same_chain(root, current_fat, &current_directory[entry],
                       plan->fat, &final_directory[entry])) {
            errno = EILSEQ;
            goto out;
        }
    }

    rv = 0;

out:
    free(current_directory);
    free(final_directory);
    free(plan);
    return rv;
}
