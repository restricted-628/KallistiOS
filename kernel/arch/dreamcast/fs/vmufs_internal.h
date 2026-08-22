/* KallistiOS ##version##

   vmufs_internal.h
   Copyright (C) 2026 Joseph Black

*/

#ifndef __DC_VMUFS_INTERNAL_H
#define __DC_VMUFS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dc/vmufs_meta.h>

typedef struct maple_device maple_device_t;

#define VMUFS_TRANSACTION_CANCELLED (-9)

typedef enum vmufs_transaction_phase {
    VMUFS_TRANSACTION_PREPARING,
    VMUFS_TRANSACTION_DATA,
    VMUFS_TRANSACTION_FAT,
    VMUFS_TRANSACTION_DIRECTORY,
    VMUFS_TRANSACTION_CLEANUP,
    VMUFS_TRANSACTION_ERASING,
    VMUFS_TRANSACTION_FINISHED
} vmufs_transaction_phase_t;

typedef struct vmufs_transaction_observer {
    bool (*cancelled)(void *data);
    void (*update)(void *data, vmufs_transaction_phase_t phase,
                   size_t completed_blocks, size_t total_blocks,
                   size_t data_blocks_completed, size_t data_blocks,
                   bool committed);
    void *data;
} vmufs_transaction_observer_t;

typedef struct vmufs_defrag_plan {
    uint16_t source[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t target[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    size_t live_blocks;
    size_t moved_blocks;
    size_t dirty_dir_blocks;
} vmufs_defrag_plan_t;

#define VMUFS_DEFRAG_MAX_STEPS \
    (2u * VMUFS_STANDARD_DIR_BLOCKS * VMUFS_BLOCK_SIZE / \
     sizeof(vmu_dir_t))
#define VMUFS_DEFRAG_MAX_TARGETS \
    (2u * VMUFS_BLOCK_SIZE / sizeof(uint16_t))

typedef struct vmufs_defrag_step {
    uint16_t directory_index;
    uint16_t target_offset;
    uint16_t block_count;
    bool final_target;
} vmufs_defrag_step_t;

typedef struct vmufs_defrag_schedule {
    vmufs_defrag_step_t steps[VMUFS_DEFRAG_MAX_STEPS];
    uint16_t targets[VMUFS_DEFRAG_MAX_TARGETS];
    size_t step_count;
    size_t target_count;
    size_t staged_steps;
} vmufs_defrag_schedule_t;

/* Host-side image tools use these variants when the root block is known from
   image geometry rather than assumed to occupy the standard physical block. */
int vmufs_root_validate_at(const vmu_root_t *root, size_t card_blocks,
                           size_t root_block);

int vmufs_validate_at(const vmu_root_t *root, size_t card_blocks,
                      size_t root_block, const uint16_t *fat,
                      size_t fat_entries, const vmu_dir_t *dir,
                      size_t dir_entries, vmufs_validation_t *result);

/* Orphan-only leakage is safe to preserve during a later mutation: no live
   directory chain owns those blocks. Every other error prevents mutation. */
bool vmufs_validation_allows_mutation(
    const vmufs_validation_t *result);

/* Resolve a directory entry into an exact, bounded physical block list before
   any caller performs card I/O or mutates a FAT copy. */
int vmufs_chain_collect(const vmu_root_t *root, const uint16_t *fat,
                        size_t fat_entries, const vmu_dir_t *entry,
                        uint16_t *blocks, size_t block_capacity);

int vmufs_chain_allocate(const vmu_root_t *root, uint16_t *fat,
                         size_t fat_entries, uint8_t filetype,
                         size_t block_count, uint16_t *blocks,
                         size_t block_capacity);

void vmufs_chain_release(uint16_t *fat, const uint16_t *blocks,
                         size_t block_count);

int vmufs_format_build(const vmufs_format_options_t *options,
                       vmu_root_t *root, uint16_t *fat,
                       size_t fat_entries);

int vmufs_defrag_plan_build(const vmu_root_t *root, const uint16_t *fat,
                            size_t fat_entries, vmu_dir_t *directory,
                            size_t directory_entries,
                            vmufs_defrag_plan_t *plan);

int vmufs_defrag_schedule_build(const vmu_root_t *root,
                                const uint16_t *fat, size_t fat_entries,
                                const vmu_dir_t *directory,
                                size_t directory_entries,
                                vmufs_defrag_schedule_t *schedule);

int vmufs_format_observed(maple_device_t *dev,
                          const vmufs_format_options_t *options,
                          vmufs_format_mode_t mode,
                          const vmufs_transaction_observer_t *observer);

int vmufs_read_blocks_observed(
    maple_device_t *dev, const char *fn, size_t first_block,
    void *outbuf, size_t block_count,
    const vmufs_transaction_observer_t *observer);

int vmufs_write_observed(maple_device_t *dev, const char *fn,
                         const void *inbuf, int insize, int flags,
                         const vmufs_transaction_observer_t *observer);

int vmufs_delete_observed(
    maple_device_t *dev, const char *fn,
    const vmufs_transaction_observer_t *observer);

int vmufs_rename_observed(
    maple_device_t *dev, const char *old_name, const char *new_name,
    const vmufs_transaction_observer_t *observer);

int vmufs_set_file_attributes_observed(
    maple_device_t *dev, const char *fn,
    const vmufs_file_attributes_t *attributes,
    const vmufs_transaction_observer_t *observer);

int vmufs_defragment_observed(
    maple_device_t *dev, const vmufs_transaction_observer_t *observer);

int vmufs_request_system_init(void);
void vmufs_request_system_shutdown(void);

#endif /* __DC_VMUFS_INTERNAL_H */
