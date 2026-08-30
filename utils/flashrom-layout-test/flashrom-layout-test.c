/* KallistiOS ##version##

   flashrom-layout-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/flashrom.h>

#include "flashrom_layout_internal.h"

static int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

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

/* Independent bit-at-a-time form of the record CRC. */
static uint16_t crc_update(const uint8_t *data, size_t length, uint16_t crc) {
    size_t i;

    for(i = 0; i < length; ++i) {
        unsigned int bit;

        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0; bit < 8; ++bit) {
            uint32_t shifted = (uint32_t)crc << 1;

            crc = (uint16_t)((crc & UINT16_C(0x8000)) ?
                             shifted ^ UINT32_C(0x1021) : shifted);
        }
    }

    return crc;
}

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int record_valid(const uint8_t record[FLASHROM_BLOCK_SIZE]) {
    uint16_t crc = crc_update(record, FLASHROM_OFFSET_CRC, 0xffff);

    crc ^= 0xffff;
    return read_le16(record + FLASHROM_OFFSET_CRC) == crc;
}

static int image_find_latest(const uint8_t *image,
                             const flashrom_partition_layout_t *layout,
                             uint16_t logical_id, uint8_t *data_out) {
    const uint8_t *bitmap = image + layout->bitmap_offset;
    size_t i;

    for(i = layout->record_count; i > 0; --i) {
        size_t index = i - 1u;
        uint8_t mask = (uint8_t)(0x80u >> (index & 7u));
        const uint8_t *record;

        if(bitmap[index >> 3] & mask)
            continue;

        record = image + (index + 1u) * FLASHROM_BLOCK_SIZE;
        if(record_valid(record) && read_le16(record) == logical_id) {
            if(data_out)
                memcpy(data_out, record + 2, FLASHROM_BLOCK_DATA_SIZE);
            return FLASHROM_ERR_NONE;
        }
    }

    return FLASHROM_ERR_NOT_FOUND;
}

static void test_partition_layout(void) {
    flashrom_partition_layout_t layout;
    uint8_t bitmap[128];
    size_t index;

    CHECK(flashrom_partition_layout(16u * 1024u, &layout) ==
          FLASHROM_ERR_NONE);
    CHECK(layout.record_count == 254);
    CHECK(layout.bitmap_offset == 16320);
    CHECK(layout.bitmap_bytes == 64);

    CHECK(flashrom_partition_layout(32u * 1024u, &layout) ==
          FLASHROM_ERR_NONE);
    CHECK(layout.record_count == 510);
    CHECK(layout.bitmap_offset == 32704);
    CHECK(layout.bitmap_bytes == 64);

    CHECK(flashrom_partition_layout(64u * 1024u, &layout) ==
          FLASHROM_ERR_NONE);
    CHECK(layout.record_count == 1021);
    CHECK(layout.bitmap_offset == 65408);
    CHECK(layout.bitmap_bytes == 128);
    CHECK(flashrom_partition_layout(65, &layout) ==
          FLASHROM_ERR_BOGUS_PART);

    memset(bitmap, 0xff, sizeof(bitmap));
    CHECK(flashrom_bitmap_find_free(bitmap, 1021, &index) ==
          FLASHROM_ERR_NONE);
    CHECK(index == 0);
    flashrom_bitmap_allocate(bitmap, index);
    CHECK(bitmap[0] == 0x7f);
    CHECK(flashrom_bitmap_find_free(bitmap, 1021, &index) ==
          FLASHROM_ERR_NONE);
    CHECK(index == 1);
}

static void test_append_interruption(void) {
    static uint8_t base[16u * 1024u];
    static uint8_t image[16u * 1024u];
    flashrom_partition_layout_t layout;
    uint8_t old_data[FLASHROM_BLOCK_DATA_SIZE];
    uint8_t new_data[FLASHROM_BLOCK_DATA_SIZE];
    uint8_t found[FLASHROM_BLOCK_DATA_SIZE];
    uint8_t old_record[FLASHROM_BLOCK_SIZE];
    uint8_t new_record[FLASHROM_BLOCK_SIZE];
    uint8_t *bitmap;
    uint8_t *destination;
    uint16_t crc;
    size_t prefix;

    CHECK(flashrom_partition_layout(sizeof(base), &layout) ==
          FLASHROM_ERR_NONE);
    memset(base, 0xff, sizeof(base));
    memset(old_data, 0x11, sizeof(old_data));
    memset(new_data, 0x22, sizeof(new_data));
    flashrom_record_build(5, old_data, old_record);
    flashrom_record_build(5, new_data, new_record);

    memcpy(base + FLASHROM_BLOCK_SIZE, old_record, sizeof(old_record));
    bitmap = base + layout.bitmap_offset;
    flashrom_bitmap_allocate(bitmap, 0);

    crc = (uint16_t)(crc_update(new_record, FLASHROM_OFFSET_CRC, 0xffff) ^
                     0xffffu);
    CHECK(read_le16(new_record + FLASHROM_OFFSET_CRC) == crc);
    CHECK(crc != 0xffff);

    /* A complete but unallocated record must remain invisible. */
    memcpy(image, base, sizeof(image));
    memcpy(image + 2u * FLASHROM_BLOCK_SIZE, new_record,
           sizeof(new_record));
    CHECK(image_find_latest(image, &layout, 5, found) ==
          FLASHROM_ERR_NONE);
    CHECK(!memcmp(found, old_data, sizeof(found)));

    /* Allocation first makes every interrupted data prefix fall back safely. */
    for(prefix = 0; prefix <= FLASHROM_OFFSET_CRC; ++prefix) {
        memcpy(image, base, sizeof(image));
        bitmap = image + layout.bitmap_offset;
        flashrom_bitmap_allocate(bitmap, 1);
        destination = image + 2u * FLASHROM_BLOCK_SIZE;
        memcpy(destination, new_record, prefix);

        CHECK(image_find_latest(image, &layout, 5, found) ==
              FLASHROM_ERR_NONE);
        CHECK(!memcmp(found, old_data, sizeof(found)));
    }

    /* The CRC is programmed last; only the complete CRC publishes the copy. */
    for(prefix = 0; prefix <= 2; ++prefix) {
        memcpy(image, base, sizeof(image));
        bitmap = image + layout.bitmap_offset;
        flashrom_bitmap_allocate(bitmap, 1);
        destination = image + 2u * FLASHROM_BLOCK_SIZE;
        memcpy(destination, new_record, FLASHROM_OFFSET_CRC + prefix);

        CHECK(image_find_latest(image, &layout, 5, found) ==
              FLASHROM_ERR_NONE);
        CHECK(!memcmp(found, prefix == 2 ? new_data : old_data,
                      sizeof(found)));
    }
}

static void test_syscfg(void) {
    uint8_t data[FLASHROM_BLOCK_DATA_SIZE] = { 0 };
    uint8_t before[FLASHROM_BLOCK_DATA_SIZE];
    flashrom_syscfg_ex_t cfg;

    memset(data, 0xa5, sizeof(data));
    memcpy(before, data, sizeof(before));
    CHECK(flashrom_syscfg_payload_update(data, UINT32_C(0x12345678),
                                         FLASHROM_LANG_FRENCH, 0, 1) ==
          FLASHROM_ERR_NONE);
    CHECK(!memcmp(data + 8, before + 8, sizeof(data) - 8));
    CHECK(data[4] == before[4]);

    CHECK(flashrom_syscfg_decode(data, &cfg) == FLASHROM_ERR_NONE);
    CHECK(cfg.settings_time == UINT32_C(0x12345678));
    CHECK(cfg.language == FLASHROM_LANG_FRENCH);
    CHECK(cfg.audio == 0);
    CHECK(cfg.autostart == 1);

    data[5] = FLASHROM_LANG_ITALIAN + 1;
    CHECK(flashrom_syscfg_decode(data, &cfg) == FLASHROM_ERR_BAD_DATA);
    CHECK(flashrom_syscfg_payload_update(data, 0,
                                         FLASHROM_LANG_ITALIAN + 1, 0, 0) ==
          FLASHROM_ERR_BAD_DATA);
    CHECK(flashrom_syscfg_decode(NULL, &cfg) == FLASHROM_ERR_BAD_DATA);
}

static void test_play_history(void) {
    uint8_t packets[4][FLASHROM_BLOCK_DATA_SIZE] = { { 0 } };
    uint8_t encoded[4][FLASHROM_BLOCK_DATA_SIZE];
    flashrom_play_history_t history;
    uint16_t crc;
    unsigned int i;

    packets[0][0] = 2;
    packets[0][1] = 1;
    memcpy(packets[0] + 2, "T-12345N  ", 10);
    memcpy(packets[0] + 12, "Layout Test", 11);
    memcpy(packets[1], "Alternate Title", 15);
    write_be32(packets[1] + 44, UINT32_C(0x10203040));
    write_be32(packets[1] + 48, UINT32_C(0x50607080));
    for(i = 0; i < 6; ++i)
        packets[1][54 + i] = (uint8_t)(0xa0u + i);

    write_be32(packets[2], UINT32_C(0x11223344));
    write_be16(packets[2] + 4, 0x1234);
    for(i = 0; i < FLASHROM_PLAY_HISTORY_BUCKETS; ++i)
        write_be16(packets[2] + 6 + i * 2u, (uint16_t)(i * 3u));
    write_be16(packets[2] + 54, 0x4567);
    write_be32(packets[2] + 56, UINT32_C(0x13579bdf));

    write_be16(packets[3], 0x2345);
    packets[3][2] = 128;
    packets[3][3] = 254;
    write_be32(packets[3] + 4, UINT32_C(0x89abcdef));
    write_be32(packets[3] + 8, UINT32_C(0x76543210));
    write_be16(packets[3] + 12, 0x3456);
    write_be16(packets[3] + 14, 0x5678);
    for(i = 0; i < FLASHROM_PLAY_HISTORY_USER_BYTES; ++i)
        packets[3][16 + i] = (uint8_t)i;
    for(i = 0; i < 10; ++i)
        packets[3][48 + i] = (uint8_t)(0xf0u + i);
    write_be16(packets[3] + 58, 0x6789);

    crc = crc_update(packets[0] + 2, 58, 0xffff);
    crc = (uint16_t)(crc_update(packets[1], 52, crc) ^ 0xffffu);
    packets[1][52] = (uint8_t)crc;
    packets[1][53] = (uint8_t)(crc >> 8);

    CHECK(flashrom_play_history_decode(packets, &history) ==
          FLASHROM_ERR_NONE);
    CHECK(history.version == 2);
    CHECK(history.autosave == 1);
    CHECK(!memcmp(history.product_number, "T-12345N  ", 10));
    CHECK(history.product_number[10] == '\0');
    CHECK(history.kind == UINT32_C(0x10203040));
    CHECK(history.first_start_time == UINT32_C(0x50607080));
    CHECK(history.previous_start_time == UINT32_C(0x11223344));
    CHECK(history.start_count == 0x1234);
    CHECK(history.play_time[23] == 69);
    CHECK(history.load_count == 0x4567);
    CHECK(history.reserved_packet2 == UINT32_C(0x13579bdf));
    CHECK(history.save_count == 0x2345);
    CHECK(history.first_network_time == UINT32_C(0x89abcdef));
    CHECK(history.previous_network_time == UINT32_C(0x76543210));
    CHECK(history.network_count == 0x3456);
    CHECK(history.network_total_minutes == 0x5678);
    CHECK(history.user_data[31] == 31);
    CHECK(history.reserved_packet3[9] == 0xf9);
    CHECK(history.save_occurrences == 0x6789);
    CHECK(flashrom_play_history_encode(&history, encoded) ==
          FLASHROM_ERR_NONE);
    CHECK(!memcmp(encoded, packets, sizeof(encoded)));

    packets[0][12] ^= 1;
    CHECK(flashrom_play_history_decode(packets, &history) ==
          FLASHROM_ERR_BAD_DATA);
    CHECK(flashrom_play_history_decode(NULL, &history) ==
          FLASHROM_ERR_BAD_DATA);
    CHECK(flashrom_play_history_encode(NULL, encoded) ==
          FLASHROM_ERR_BAD_DATA);
}

int main(void) {
    test_partition_layout();
    test_append_interruption();
    test_syscfg();
    test_play_history();

    if(failures) {
        fprintf(stderr, "%d flash layout test(s) failed\n", failures);
        return 1;
    }

    puts("flashrom-layout-test: all checks passed");
    return 0;
}
