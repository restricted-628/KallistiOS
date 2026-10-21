/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include "package.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

static uint32_t little_endian(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static bool zero_bytes(const uint8_t *p, size_t bytes) {
    for(size_t i = 0; i < bytes; ++i)
        if(p[i]) return false;
    return true;
}

static int invalid(void) {
    errno = ENOEXEC;
    return -1;
}

int ovl_package_header(const uint8_t h[OVL_HEADER_BYTES], uint32_t base, uint32_t slot_bytes,
                       uint32_t expected_bytes, ovl_package_t *p) {
    if(memcmp(h, "DCo@", 4) || little_endian(h + 4) != 0x00400001 ||
       little_endian(h + 8) || little_endian(h + 28) != 1 ||
       little_endian(h + 44) != OVL_DESCRIPTOR_BYTES || !zero_bytes(h + 48, 16))
        return invalid();
    p->count = little_endian(h + 12);
    p->base = little_endian(h + 16);
    p->slot_bytes = little_endian(h + 20);
    p->entry_offset = little_endian(h + 24);
    p->elf_offset = little_endian(h + 32);
    p->elf_bytes = little_endian(h + 36);
    if(!p->count || p->count > OVL_MAX_SEGMENTS ||
       p->base != base || p->slot_bytes != slot_bytes ||
       base < 0x8c000000u || base >= 0x8e000000u || (base & 31) ||
       !slot_bytes || (slot_bytes & 31) || slot_bytes > OVL_MAX_SLOT_BYTES ||
       slot_bytes > 0x8e000000u - base ||
       p->entry_offset >= slot_bytes || (p->entry_offset & 1) ||
       p->elf_offset != OVL_HEADER_BYTES + p->count * OVL_DESCRIPTOR_BYTES ||
       p->elf_bytes < 52 || expected_bytes > INT32_MAX ||
       p->elf_offset > expected_bytes ||
       p->elf_bytes != expected_bytes - p->elf_offset ||
       little_endian(h + 40) != expected_bytes)
        return invalid();
    return 0;
}

int ovl_package_segments(const uint8_t *descriptors, ovl_package_t *p) {
    uint32_t previous_end = 0, previous_page_end = 0, previous_flags = 0;
    bool entry_found = false;

    for(uint32_t i = 0; i < p->count; ++i) {
        const uint8_t *d = descriptors + i * OVL_DESCRIPTOR_BYTES;
        ovl_segment_t *s = &p->segments[i];
        s->memory_offset = little_endian(d);
        s->file_bytes = little_endian(d + 4);
        s->memory_bytes = little_endian(d + 8);
        s->file_offset = little_endian(d + 12);
        s->permissions = little_endian(d + 16);
        s->alignment = little_endian(d + 20);
        if(!zero_bytes(d + 24, 8) || !s->memory_bytes ||
           s->file_bytes > s->memory_bytes || s->memory_offset < previous_end ||
           s->memory_offset > p->slot_bytes ||
           s->memory_bytes > p->slot_bytes - s->memory_offset ||
           s->file_offset > p->elf_bytes ||
           s->file_bytes > p->elf_bytes - s->file_offset ||
           (s->permissions & ~7u) || !(s->permissions & 4) ||
           (s->permissions & 3) == 3 ||
           (s->alignment > 1 &&
            ((s->alignment & (s->alignment - 1)) ||
             (p->base + s->memory_offset) % s->alignment !=
             s->file_offset % s->alignment)))
            return invalid();
        /* Same 32-byte coverage rule as the host plan, not MMU protection. */
        if(i && (s->memory_offset & ~31u) < previous_page_end &&
           s->permissions != previous_flags)
            return invalid();
        previous_end = s->memory_offset + s->memory_bytes;
        previous_page_end = (previous_end + 31) & ~31u;
        previous_flags = s->permissions;
        if((s->permissions & 1) && p->entry_offset >= s->memory_offset &&
           p->entry_offset - s->memory_offset < s->file_bytes &&
           s->file_bytes - (p->entry_offset - s->memory_offset) >= 2)
            entry_found = true;
    }
    return entry_found ? 0 : invalid();
}

void ovl_package_scatter(const ovl_package_t *p, uint8_t *slot,
                         uint32_t offset, const uint8_t *data, size_t bytes) {
    uint32_t end = offset + (uint32_t)bytes;
    for(uint32_t i = 0; i < p->count; ++i) {
        const ovl_segment_t *s = &p->segments[i];
        uint32_t first = offset > s->file_offset ? offset : s->file_offset;
        uint32_t last = s->file_offset + s->file_bytes;
        if(last > end) last = end;
        if(first < last)
            memcpy(slot + s->memory_offset + first - s->file_offset,
                   data + first - offset, last - first);
    }
}

void ovl_package_zero(const ovl_package_t *p, uint8_t *slot) {
    for(uint32_t i = 0; i < p->count; ++i) {
        const ovl_segment_t *s = &p->segments[i];
        memset(slot + s->memory_offset + s->file_bytes, 0,
               s->memory_bytes - s->file_bytes);
    }
}
