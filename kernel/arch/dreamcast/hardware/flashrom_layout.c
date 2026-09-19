/* KallistiOS ##version##

   flashrom_layout.c
   Copyright (C) 2026 Joseph Black
*/

#include <stdint.h>
#include <string.h>

#include <dc/flashrom.h>

#include "flashrom_layout_internal.h"

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t read_be16(const uint8_t *data) {
    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static uint32_t read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void write_le16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_be16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

/* CRC-16/CCITT-FALSE with the final complement used by flash records. */
static uint16_t history_crc(const uint8_t *data, size_t length,
                            uint16_t crc) {
    size_t i;

    for(i = 0; i < length; ++i) {
        unsigned int bit;

        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0; bit < 8; ++bit) {
            uint16_t shifted = (uint16_t)(crc << 1);

            crc = (crc & 0x8000u) ?
                  (uint16_t)(shifted ^ UINT16_C(0x1021)) : shifted;
        }
    }

    return crc;
}

int flashrom_partition_layout(size_t partition_bytes,
                              flashrom_partition_layout_t *layout) {
    size_t total_blocks, bitmap_bytes, bitmap_blocks;

    if(!layout || partition_bytes < FLASHROM_BLOCK_SIZE * 3u ||
       partition_bytes % FLASHROM_BLOCK_SIZE)
        return FLASHROM_ERR_BOGUS_PART;

    total_blocks = partition_bytes / FLASHROM_BLOCK_SIZE;
    bitmap_bytes = ((total_blocks + 511u) & ~511u) / 8u;
    bitmap_blocks = bitmap_bytes / FLASHROM_BLOCK_SIZE;

    if(!bitmap_bytes || !bitmap_blocks ||
       total_blocks <= 1u + bitmap_blocks)
        return FLASHROM_ERR_BOGUS_PART;

    layout->partition_bytes = partition_bytes;
    layout->record_count = total_blocks - 1u - bitmap_blocks;
    layout->bitmap_offset = partition_bytes - bitmap_bytes;
    layout->bitmap_bytes = bitmap_bytes;
    return FLASHROM_ERR_NONE;
}

int flashrom_bitmap_find_free(const uint8_t *bitmap, size_t record_count,
                              size_t *record_index) {
    size_t i;

    if(!bitmap || !record_index)
        return FLASHROM_ERR_BAD_DATA;

    for(i = 0; i < record_count; ++i) {
        if(bitmap[i >> 3] & (uint8_t)(0x80u >> (i & 7u))) {
            *record_index = i;
            return FLASHROM_ERR_NONE;
        }
    }

    return FLASHROM_ERR_NO_SPACE;
}

void flashrom_bitmap_allocate(uint8_t *bitmap, size_t record_index) {
    bitmap[record_index >> 3] &=
        (uint8_t)~(uint8_t)(0x80u >> (record_index & 7u));
}

void flashrom_record_build(uint16_t logical_id, const uint8_t *data,
                           uint8_t *record) {
    uint16_t crc;

    record[0] = (uint8_t)logical_id;
    record[1] = (uint8_t)(logical_id >> 8);
    memcpy(record + 2, data, FLASHROM_BLOCK_DATA_SIZE);
    crc = (uint16_t)(history_crc(record, FLASHROM_OFFSET_CRC, 0xffff) ^
                     0xffffu);
    record[FLASHROM_OFFSET_CRC] = (uint8_t)crc;
    record[FLASHROM_OFFSET_CRC + 1] = (uint8_t)(crc >> 8);
}

int flashrom_syscfg_payload_update(uint8_t *data, uint32_t settings_time,
                                   int language, int audio, int autostart) {
    if(!data || language < FLASHROM_LANG_JAPANESE ||
       language > FLASHROM_LANG_ITALIAN || (audio != 0 && audio != 1) ||
       (autostart != 0 && autostart != 1))
        return FLASHROM_ERR_BAD_DATA;

    data[0] = (uint8_t)settings_time;
    data[1] = (uint8_t)(settings_time >> 8);
    data[2] = (uint8_t)(settings_time >> 16);
    data[3] = (uint8_t)(settings_time >> 24);
    data[5] = (uint8_t)language;
    data[6] = audio ? 0 : 1;
    data[7] = autostart ? 0 : 1;
    return FLASHROM_ERR_NONE;
}

static void copy_fixed_string(char *out, const uint8_t *in, size_t length) {
    memcpy(out, in, length);
    out[length] = '\0';
}

int flashrom_syscfg_decode(const uint8_t data[FLASHROM_BLOCK_DATA_SIZE],
                           flashrom_syscfg_ex_t *out) {
    if(!data || !out || data[5] > FLASHROM_LANG_ITALIAN || data[6] > 1 ||
       data[7] > 1)
        return FLASHROM_ERR_BAD_DATA;

    out->settings_time = read_le32(data);
    out->language = data[5];
    out->audio = data[6] == 1 ? 0 : 1;
    out->autostart = data[7] == 1 ? 0 : 1;
    return FLASHROM_ERR_NONE;
}

int flashrom_play_history_decode(
    const uint8_t packets[4][FLASHROM_BLOCK_DATA_SIZE],
    flashrom_play_history_t *out) {
    uint16_t stored_crc, calculated_crc;
    unsigned int i;

    if(!packets || !out)
        return FLASHROM_ERR_BAD_DATA;

    calculated_crc = history_crc(packets[0] + 2, 58, 0xffff);
    calculated_crc = history_crc(packets[1], 52, calculated_crc) ^ 0xffff;
    stored_crc = read_le16(packets[1] + 52);
    if(stored_crc != calculated_crc)
        return FLASHROM_ERR_BAD_DATA;

    memset(out, 0, sizeof(*out));
    out->version = packets[0][0];
    out->autosave = packets[0][1];
    copy_fixed_string(out->product_number, packets[0] + 2, 10);
    copy_fixed_string(out->product_name, packets[0] + 12, 48);
    copy_fixed_string(out->product_name_alt, packets[1], 44);
    out->kind = read_be32(packets[1] + 44);
    out->first_start_time = read_be32(packets[1] + 48);
    memcpy(out->peripheral_info, packets[1] + 54,
           sizeof(out->peripheral_info));
    out->previous_start_time = read_be32(packets[2]);
    out->start_count = read_be16(packets[2] + 4);
    for(i = 0; i < FLASHROM_PLAY_HISTORY_BUCKETS; ++i)
        out->play_time[i] = read_be16(packets[2] + 6 + i * 2u);
    out->load_count = read_be16(packets[2] + 54);
    out->reserved_packet2 = read_be32(packets[2] + 56);
    out->save_count = read_be16(packets[3]);
    out->evaluation = packets[3][2];
    out->progress = packets[3][3];
    out->first_network_time = read_be32(packets[3] + 4);
    out->previous_network_time = read_be32(packets[3] + 8);
    out->network_count = read_be16(packets[3] + 12);
    out->network_total_minutes = read_be16(packets[3] + 14);
    memcpy(out->user_data, packets[3] + 16, sizeof(out->user_data));
    memcpy(out->reserved_packet3, packets[3] + 48,
           sizeof(out->reserved_packet3));
    out->save_occurrences = read_be16(packets[3] + 58);
    return FLASHROM_ERR_NONE;
}

int flashrom_play_history_encode(
    const flashrom_play_history_t *history,
    uint8_t packets[4][FLASHROM_BLOCK_DATA_SIZE]) {
    uint16_t crc;
    unsigned int i;

    if(!history || !packets)
        return FLASHROM_ERR_BAD_DATA;

    packets[0][0] = history->version;
    packets[0][1] = history->autosave;
    memcpy(packets[0] + 2, history->product_number, 10);
    memcpy(packets[0] + 12, history->product_name, 48);

    memcpy(packets[1], history->product_name_alt, 44);
    write_be32(packets[1] + 44, history->kind);
    write_be32(packets[1] + 48, history->first_start_time);
    memcpy(packets[1] + 54, history->peripheral_info,
           sizeof(history->peripheral_info));

    write_be32(packets[2], history->previous_start_time);
    write_be16(packets[2] + 4, history->start_count);
    for(i = 0; i < FLASHROM_PLAY_HISTORY_BUCKETS; ++i)
        write_be16(packets[2] + 6 + i * 2u, history->play_time[i]);
    write_be16(packets[2] + 54, history->load_count);
    write_be32(packets[2] + 56, history->reserved_packet2);

    write_be16(packets[3], history->save_count);
    packets[3][2] = history->evaluation;
    packets[3][3] = history->progress;
    write_be32(packets[3] + 4, history->first_network_time);
    write_be32(packets[3] + 8, history->previous_network_time);
    write_be16(packets[3] + 12, history->network_count);
    write_be16(packets[3] + 14, history->network_total_minutes);
    memcpy(packets[3] + 16, history->user_data,
           sizeof(history->user_data));
    memcpy(packets[3] + 48, history->reserved_packet3,
           sizeof(history->reserved_packet3));
    write_be16(packets[3] + 58, history->save_occurrences);

    crc = history_crc(packets[0] + 2, 58, 0xffff);
    crc = (uint16_t)(history_crc(packets[1], 52, crc) ^ 0xffffu);
    write_le16(packets[1] + 52, crc);
    return FLASHROM_ERR_NONE;
}
