/* KallistiOS ##version##

   flashrom_layout_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __FLASHROM_LAYOUT_INTERNAL_H
#define __FLASHROM_LAYOUT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct flashrom_partition_layout {
    size_t partition_bytes;
    size_t record_count;
    size_t bitmap_offset;
    size_t bitmap_bytes;
} flashrom_partition_layout_t;

int flashrom_partition_layout(size_t partition_bytes,
                              flashrom_partition_layout_t *layout);
int flashrom_bitmap_find_free(const uint8_t *bitmap, size_t record_count,
                              size_t *record_index);
void flashrom_bitmap_allocate(uint8_t *bitmap, size_t record_index);
void flashrom_record_build(uint16_t logical_id, const uint8_t *data,
                           uint8_t *record);
int flashrom_syscfg_payload_update(uint8_t *data, uint32_t settings_time,
                                   int language, int audio, int autostart);

#endif /* __FLASHROM_LAYOUT_INTERNAL_H */
