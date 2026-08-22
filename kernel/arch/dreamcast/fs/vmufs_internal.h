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

#endif /* __DC_VMUFS_INTERNAL_H */
