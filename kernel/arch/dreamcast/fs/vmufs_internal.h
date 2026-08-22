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

/* These image-only structures remain private until the corresponding device
   operations have independently proven interruption-safe commit protocols. */
typedef struct vmufs_format_options {
    uint8_t use_custom_color;
    uint8_t custom_color[4];
    vmu_timestamp_t timestamp;
    uint16_t icon_shape;
} vmufs_format_options_t;

typedef struct vmufs_defrag_plan {
    uint16_t source[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t target[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t fat[VMUFS_BLOCK_SIZE / sizeof(uint16_t)];
    size_t live_blocks;
    size_t moved_blocks;
    size_t dirty_dir_blocks;
} vmufs_defrag_plan_t;

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

#endif /* __DC_VMUFS_INTERNAL_H */
