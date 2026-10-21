/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef KOS_OVERLAY_PACKAGE_H
#define KOS_OVERLAY_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#define OVL_HEADER_BYTES 64u
#define OVL_DESCRIPTOR_BYTES 32u
#define OVL_MAX_SEGMENTS 16u
#define OVL_PREFIX_BYTES (OVL_HEADER_BYTES + OVL_MAX_SEGMENTS * OVL_DESCRIPTOR_BYTES)
#define OVL_MAX_SLOT_BYTES (1024u * 1024u)
#define OVL_STAGING_BYTES (32u * 1024u)
#define OVL_PROFILE_P1 1u
#define OVL_PROFILE_VM 2u

typedef struct ovl_segment {
    uint32_t memory_offset, file_bytes, memory_bytes, file_offset;
    uint32_t permissions, alignment;
} ovl_segment_t;

typedef struct ovl_package {
    uint32_t count, base, slot_bytes, entry_offset, elf_offset, elf_bytes, profile;
    ovl_segment_t segments[OVL_MAX_SEGMENTS];
} ovl_package_t;

/* Header first, then exactly count descriptors. On error output is unspecified;
   neither routine reads beyond its fixed/header-derived bounded input. */
int ovl_package_header(const uint8_t header[OVL_HEADER_BYTES], uint32_t base,
                       uint32_t slot_bytes, uint32_t expected_bytes,
                       ovl_package_t *package);
/* Explicit backend selection. The legacy header entry point admits P1 only.
   This parser does not reserve, write or publish executable mappings. */
int ovl_package_header_profile(const uint8_t header[OVL_HEADER_BYTES], uint32_t base,
                               uint32_t slot_bytes, uint32_t expected_bytes,
                               uint32_t expected_profile, ovl_package_t *package);
int ovl_package_segments(const uint8_t *descriptors, ovl_package_t *package);

/* P1 writer only: fully admitted P1 plan and chunk wholly inside its ELF stream.
   Translated consumers must use vm_exec_write/zero, never cast a P0 destination. */
void ovl_package_scatter(const ovl_package_t *package, uint8_t *slot,
                         uint32_t elf_offset, const uint8_t *data, size_t bytes);
void ovl_package_zero(const ovl_package_t *package, uint8_t *slot);

#endif
