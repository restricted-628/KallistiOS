/* KallistiOS ##version##

   flashrom-mutation-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/flashrom.h>
#include <kos/mutex.h>

#include "flashrom_layout_internal.h"

#define TEST_BLOCK_BYTES (16u * 1024u)
#define TEST_SETTINGS_START TEST_BLOCK_BYTES
#define TEST_SETTINGS_BYTES (32u * 1024u)
#define TEST_IMAGE_BYTES (TEST_BLOCK_BYTES + TEST_SETTINGS_BYTES)
#define MAX_WRITE_CALLS 8

typedef struct write_call {
    size_t offset;
    size_t length;
} write_call_t;

static uint8_t image[TEST_IMAGE_BYTES];
static write_call_t write_calls[MAX_WRITE_CALLS];
static unsigned int write_call_count;
static unsigned int fail_call;
static size_t fail_prefix;
static int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

uint16_t net_crc16ccitt(const uint8_t *data, size_t length, uint16_t crc) {
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

int dbglog(int level, const char *format, ...) {
    (void)level;
    (void)format;
    return 0;
}

int irq_disable(void) {
    return 0;
}

void irq_restore(int old) {
    (void)old;
}

bool irq_inside_int(void) {
    return false;
}

int mutex_lock(mutex_t *mutex) {
    if(mutex->locked)
        return -1;
    mutex->locked = 1;
    return 0;
}

int mutex_lock_irqsafe(mutex_t *mutex) {
    return mutex_lock(mutex);
}

int mutex_unlock(mutex_t *mutex) {
    if(!mutex->locked)
        return -1;
    mutex->locked = 0;
    return 0;
}

int syscall_flashrom_info(uint32_t part, void *info) {
    uint32_t *values = info;

    if(part == FLASHROM_PT_BLOCK_1) {
        values[0] = 0;
        values[1] = TEST_BLOCK_BYTES;
    }
    else if(part == FLASHROM_PT_SETTINGS) {
        values[0] = TEST_SETTINGS_START;
        values[1] = TEST_SETTINGS_BYTES;
    }
    else {
        return -1;
    }

    return 0;
}

int syscall_flashrom_read(uint32_t pos, void *dest, size_t length) {
    if(pos > sizeof(image) || length > sizeof(image) - pos)
        return -1;
    memcpy(dest, image + pos, length);
    return (int)length;
}

int syscall_flashrom_write(uint32_t pos, const void *src, size_t length) {
    const uint8_t *bytes = src;
    size_t program = length;
    size_t i;
    bool fail;

    ++write_call_count;
    if(write_call_count <= MAX_WRITE_CALLS) {
        write_calls[write_call_count - 1].offset = pos;
        write_calls[write_call_count - 1].length = length;
    }

    if(pos > sizeof(image) || length > sizeof(image) - pos)
        return -1;

    fail = write_call_count == fail_call;
    if(fail && fail_prefix < program)
        program = fail_prefix;

    for(i = 0; i < program; ++i) {
        if((uint8_t)~image[pos + i] & bytes[i])
            return -1;
        image[pos + i] &= bytes[i];
    }

    return fail ? -1 : (int)length;
}

int syscall_flashrom_delete(uint32_t pos) {
    (void)pos;
    return -1;
}

static void reset_partition(uint16_t logical_id, const uint8_t *data) {
    flashrom_partition_layout_t layout;
    uint8_t record[FLASHROM_BLOCK_SIZE];

    memset(image, 0xff, sizeof(image));
    memcpy(image, "KATANA_FLASH____", 16);
    image[16] = FLASHROM_PT_BLOCK_1;
    image[17] = 0;
    CHECK(flashrom_partition_layout(TEST_BLOCK_BYTES, &layout) ==
          FLASHROM_ERR_NONE);
    flashrom_record_build(logical_id, data, record);
    memcpy(image + FLASHROM_BLOCK_SIZE, record, sizeof(record));
    flashrom_bitmap_allocate(image + layout.bitmap_offset, 0);

    memset(write_calls, 0, sizeof(write_calls));
    write_call_count = 0;
    fail_call = 0;
    fail_prefix = 0;
}

static void reset_settings_partition(void) {
    memset(image, 0xff, sizeof(image));
    memcpy(image + TEST_SETTINGS_START, "KATANA_FLASH____", 16);
    image[TEST_SETTINGS_START + 16] = FLASHROM_PT_SETTINGS;
    image[TEST_SETTINGS_START + 17] = 0;

    memset(write_calls, 0, sizeof(write_calls));
    write_call_count = 0;
    fail_call = 0;
    fail_prefix = 0;
}

static void check_value(uint16_t logical_id, const uint8_t *expected) {
    uint8_t actual[FLASHROM_BLOCK_DATA_SIZE];

    CHECK(flashrom_read_block(FLASHROM_PT_BLOCK_1, logical_id,
                              actual, NULL) == FLASHROM_ERR_NONE);
    CHECK(!memcmp(actual, expected, sizeof(actual)));
}

static void test_success(void) {
    uint8_t old_data[FLASHROM_BLOCK_DATA_SIZE];
    uint8_t new_data[FLASHROM_BLOCK_DATA_SIZE];
    flashrom_block_info_t info;

    memset(old_data, 0x11, sizeof(old_data));
    memset(new_data, 0x22, sizeof(new_data));
    reset_partition(7, old_data);

    CHECK(flashrom_append_block(FLASHROM_PT_BLOCK_1, 7, new_data, &info) ==
          FLASHROM_ERR_NONE);
    CHECK(info.logical_id == 7);
    CHECK(info.physical_block == 2);
    CHECK(write_call_count == 3);
    CHECK(write_calls[0].length == 64);
    CHECK(write_calls[1].length == FLASHROM_OFFSET_CRC);
    CHECK(write_calls[2].length == 2);
    check_value(7, new_data);
}

static void test_interrupted_record(void) {
    uint8_t old_data[FLASHROM_BLOCK_DATA_SIZE];
    uint8_t new_data[FLASHROM_BLOCK_DATA_SIZE];
    flashrom_block_info_t info;

    memset(old_data, 0x33, sizeof(old_data));
    memset(new_data, 0x44, sizeof(new_data));
    reset_partition(8, old_data);
    fail_call = 2;
    fail_prefix = 31;

    CHECK(flashrom_append_block(FLASHROM_PT_BLOCK_1, 8, new_data, NULL) ==
          FLASHROM_ERR_WRITE_BLOCK);
    check_value(8, old_data);

    fail_call = 0;
    write_call_count = 0;
    CHECK(flashrom_append_block(FLASHROM_PT_BLOCK_1, 8, new_data, &info) ==
          FLASHROM_ERR_NONE);
    CHECK(info.physical_block == 3);
    check_value(8, new_data);
}

static void test_interrupted_crc(void) {
    uint8_t old_data[FLASHROM_BLOCK_DATA_SIZE];
    uint8_t new_data[FLASHROM_BLOCK_DATA_SIZE];

    memset(old_data, 0x55, sizeof(old_data));
    memset(new_data, 0x66, sizeof(new_data));
    reset_partition(9, old_data);
    fail_call = 3;
    fail_prefix = 1;

    CHECK(flashrom_append_block(FLASHROM_PT_BLOCK_1, 9, new_data, NULL) ==
          FLASHROM_ERR_WRITE_BLOCK);
    check_value(9, old_data);
}

static void test_syscfg_update(void) {
    uint8_t data[FLASHROM_BLOCK_DATA_SIZE];
    uint8_t updated[FLASHROM_BLOCK_DATA_SIZE];
    flashrom_syscfg_ex_t settings = {
        .settings_time = UINT32_C(0x12345678),
        .language = FLASHROM_LANG_ITALIAN,
        .audio = 1,
        .autostart = 0
    };

    memset(data, 0xa5, sizeof(data));
    data[5] = FLASHROM_LANG_ENGLISH;
    data[6] = 1;
    data[7] = 0;
    reset_partition(FLASHROM_B1_SYSCFG, data);

    CHECK(flashrom_set_syscfg_ex(&settings) == FLASHROM_ERR_NONE);
    CHECK(flashrom_read_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_SYSCFG,
                              updated, NULL) == FLASHROM_ERR_NONE);
    CHECK(updated[0] == 0x78 && updated[1] == 0x56 &&
          updated[2] == 0x34 && updated[3] == 0x12);
    CHECK(updated[4] == data[4]);
    CHECK(updated[5] == FLASHROM_LANG_ITALIAN);
    CHECK(updated[6] == 0);
    CHECK(updated[7] == 1);
    CHECK(!memcmp(updated + 8, data + 8, sizeof(data) - 8));
}

static void make_history(flashrom_play_history_t *history) {
    unsigned int i;

    memset(history, 0, sizeof(*history));
    history->version = 2;
    history->autosave = 1;
    memcpy(history->product_number, "T-54321N  ", 10);
    memcpy(history->product_name, "Mutation Test", 13);
    memcpy(history->product_name_alt, "Alternate", 9);
    history->kind = UINT32_C(0x10203040);
    history->first_start_time = UINT32_C(0x50607080);
    history->previous_start_time = UINT32_C(0x11223344);
    history->start_count = 3;
    history->load_count = 4;
    history->save_count = 5;
    history->evaluation = 6;
    history->progress = 7;
    for(i = 0; i < FLASHROM_PLAY_HISTORY_BUCKETS; ++i)
        history->play_time[i] = (uint16_t)i;
    for(i = 0; i < FLASHROM_PLAY_HISTORY_USER_BYTES; ++i)
        history->user_data[i] = (uint8_t)(0x80u + i);
}

static void test_history_write(void) {
    flashrom_play_history_t history;
    flashrom_play_history_t actual;
    flashrom_play_history_write_result_t result;

    reset_settings_partition();
    make_history(&history);
    CHECK(flashrom_play_history_write(0, &history, &result) ==
          FLASHROM_ERR_NONE);
    CHECK(result.requested_mask == 0x0f);
    CHECK(result.committed_mask == 0x0f);
    CHECK(result.failed_packet == -1);
    CHECK(result.records[2].physical_block == 1);
    CHECK(result.records[3].physical_block == 2);
    CHECK(result.records[1].physical_block == 3);
    CHECK(result.records[0].physical_block == 4);
    CHECK(flashrom_play_history_read(0, &actual) == FLASHROM_ERR_NONE);
    CHECK(actual.save_count == history.save_count);
    CHECK(!memcmp(actual.product_number, history.product_number, 10));

    write_call_count = 0;
    CHECK(flashrom_play_history_write(0, &history, &result) ==
          FLASHROM_ERR_NONE);
    CHECK(result.requested_mask == 0);
    CHECK(result.committed_mask == 0);
    CHECK(write_call_count == 0);

    ++history.save_count;
    write_call_count = 0;
    CHECK(flashrom_play_history_write(0, &history, &result) ==
          FLASHROM_ERR_NONE);
    CHECK(result.requested_mask == (1u << 3));
    CHECK(result.committed_mask == (1u << 3));
    CHECK(write_call_count == 3);
    CHECK(flashrom_play_history_read(0, &actual) == FLASHROM_ERR_NONE);
    CHECK(actual.save_count == history.save_count);
}

static void test_history_identity_interruption(void) {
    flashrom_play_history_t history;
    flashrom_play_history_t actual;
    flashrom_play_history_write_result_t result;

    reset_settings_partition();
    make_history(&history);
    CHECK(flashrom_play_history_write(1, &history, NULL) ==
          FLASHROM_ERR_NONE);

    history.product_name[0] ^= 1;
    write_call_count = 0;
    fail_call = 5;
    fail_prefix = 20;
    CHECK(flashrom_play_history_write(1, &history, &result) ==
          FLASHROM_ERR_WRITE_BLOCK);
    CHECK(result.requested_mask == 0x03);
    CHECK(result.committed_mask == (1u << 1));
    CHECK(result.failed_packet == 0);
    CHECK(flashrom_play_history_read(1, &actual) == FLASHROM_ERR_BAD_DATA);

    fail_call = 0;
    write_call_count = 0;
    CHECK(flashrom_play_history_write(1, &history, &result) ==
          FLASHROM_ERR_NONE);
    CHECK(result.requested_mask == 0x01);
    CHECK(result.committed_mask == 0x01);
    CHECK(flashrom_play_history_read(1, &actual) == FLASHROM_ERR_NONE);
    CHECK(actual.product_name[0] == history.product_name[0]);
}

static void test_history_capacity_preflight(void) {
    flashrom_partition_layout_t layout;
    flashrom_play_history_t history;
    flashrom_play_history_write_result_t result;
    uint8_t *bitmap;

    reset_settings_partition();
    CHECK(flashrom_partition_layout(TEST_SETTINGS_BYTES, &layout) ==
          FLASHROM_ERR_NONE);
    bitmap = image + TEST_SETTINGS_START + layout.bitmap_offset;
    memset(bitmap, 0, layout.bitmap_bytes);
    bitmap[0] = 0xe0;
    make_history(&history);

    CHECK(flashrom_play_history_write(2, &history, &result) ==
          FLASHROM_ERR_NO_SPACE);
    CHECK(result.requested_mask == 0x0f);
    CHECK(result.committed_mask == 0);
    CHECK(write_call_count == 0);
}

int main(void) {
    test_success();
    test_interrupted_record();
    test_interrupted_crc();
    test_syscfg_update();
    test_history_write();
    test_history_identity_interruption();
    test_history_capacity_preflight();

    if(failures) {
        fprintf(stderr, "%d flash mutation test(s) failed\n", failures);
        return 1;
    }

    puts("flashrom-mutation-test: all checks passed");
    return 0;
}
