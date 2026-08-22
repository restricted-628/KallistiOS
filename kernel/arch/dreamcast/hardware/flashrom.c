/* KallistiOS ##version##

   flashrom.c
   Copyright (c) 2003 Megan Potter
   Copyright (C) 2008 Lawrence Sebald
   Copyright (C) 2024 Andy Barajas
   Copyright (C) 2026 Joseph Black
*/

/*

  This module implements the stuff enumerated in flashrom.h.

  Thanks to Marcus Comstedt for the info about the flashrom and syscalls.

 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dc/flashrom.h>
#include <dc/syscalls.h>
#include <kos/dbglog.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <kos/net.h>

#include "flashrom_layout_internal.h"

/* Serializes operations which program or erase the shared flash device. */
static mutex_t flashrom_program_mutex = MUTEX_INITIALIZER;

static void strcpy_no_term(char *dest, const char *src, size_t destsize) {
    size_t srclength;

    srclength = strlen(src);
    srclength = srclength > destsize ? destsize : srclength;
    memcpy(dest, src, srclength);
    if(srclength < destsize) {
        memset(dest + srclength, '\0', destsize - srclength);
    }
}

static void strcpy_with_term(char *dest, const char *src, size_t destsize) {
    size_t srclength;

    srclength = strlen(src);
    srclength = srclength > destsize ? destsize : srclength;
    memcpy(dest, src, srclength);
    dest[srclength] = '\0';
}

int flashrom_info(int part, int *start_out, int *size_out) {
    uint32_t  ptrs[2];
    int old, rv;

    old = irq_disable();

    if(!(rv = syscall_flashrom_info(part, ptrs))) {
        *start_out = ptrs[0];
        *size_out = ptrs[1];
    }

    irq_restore(old);

    return rv;
}

int flashrom_read(int offset, void *buffer_out, int bytes) {
    int old, rv;

    old = irq_disable();
    rv = syscall_flashrom_read(offset, buffer_out, bytes);
    irq_restore(old);
    return rv;
}

static int flashrom_write_unlocked(int offset, const void *buffer, int bytes) {
    int old, rv;

    old = irq_disable();
    rv = syscall_flashrom_write(offset, buffer, bytes);
    irq_restore(old);
    return rv;
}

int flashrom_write(int offset, void *buffer, int bytes) {
    int rv;

    if(mutex_lock_irqsafe(&flashrom_program_mutex) < 0)
        return -1;

    rv = flashrom_write_unlocked(offset, buffer, bytes);
    mutex_unlock(&flashrom_program_mutex);
    return rv;
}

static int flashrom_delete_unlocked(int offset) {
    int old, rv;

    old = irq_disable();
    rv = syscall_flashrom_delete(offset);
    irq_restore(old);
    return rv;
}

int flashrom_delete(int offset) {
    int rv;

    if(mutex_lock_irqsafe(&flashrom_program_mutex) < 0)
        return -1;

    rv = flashrom_delete_unlocked(offset);
    mutex_unlock(&flashrom_program_mutex);
    return rv;
}

/* Higher level stuff follows */

static uint16_t flashrom_calc_crc(const uint8_t *buffer) {
    return net_crc16ccitt(buffer, FLASHROM_OFFSET_CRC, 0xffff) ^ 0xffff;
}

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

typedef bool (*flashrom_record_visitor_t)(
    const uint8_t record[FLASHROM_BLOCK_SIZE], uint16_t physical, void *data);

/*
   Block-allocated partitions contain a 64-byte header, append-only 64-byte
   records, and a bitmap rounded up to whole 64-byte records at the end. A zero
   bitmap bit marks an allocated record. Visit valid records newest-first so
   holes left by maintenance do not hide a newer valid copy.
*/
static int flashrom_visit_blocks(int partid, flashrom_record_visitor_t visitor,
                                 void *data) {
    int start, size;
    size_t total_blocks, bitmap_bytes, bitmap_blocks, record_blocks;
    uint8_t magic[18];
    uint8_t *bitmap;
    uint8_t record[FLASHROM_BLOCK_SIZE];
    size_t i;
    bool allocated = false;

    if(partid < FLASHROM_PT_SYSTEM || partid > FLASHROM_PT_BLOCK_2)
        return FLASHROM_ERR_NO_PARTITION;

    if(flashrom_info(partid, &start, &size) || start < 0 || size < 128 ||
       (size % FLASHROM_BLOCK_SIZE) != 0)
        return FLASHROM_ERR_NO_PARTITION;

    if(flashrom_read(start, magic, sizeof(magic)) < 0)
        return FLASHROM_ERR_READ_PART;

    /* The fixed partition signature is the existing on-flash format. */
    if(memcmp(magic, "KATANA_FLASH____", 16) ||
       read_le16(magic + 16) != (uint16_t)partid)
        return FLASHROM_ERR_BAD_MAGIC;

    total_blocks = (size_t)size / FLASHROM_BLOCK_SIZE;
    bitmap_bytes = (total_blocks + 511u) & ~511u;
    bitmap_bytes /= 8u;
    bitmap_blocks = bitmap_bytes / FLASHROM_BLOCK_SIZE;

    if(bitmap_bytes == 0 || bitmap_blocks == 0 ||
       total_blocks <= 1u + bitmap_blocks || bitmap_bytes > (size_t)size)
        return FLASHROM_ERR_BOGUS_PART;

    record_blocks = total_blocks - 1u - bitmap_blocks;
    bitmap = malloc(bitmap_bytes);
    if(!bitmap)
        return FLASHROM_ERR_NOMEM;

    if(flashrom_read(start + size - (int)bitmap_bytes, bitmap,
                     (int)bitmap_bytes) < 0) {
        free(bitmap);
        return FLASHROM_ERR_READ_BITMAP;
    }

    for(i = record_blocks; i > 0; --i) {
        size_t index = i - 1u;
        uint8_t mask = (uint8_t)(0x80u >> (index & 7u));

        if(bitmap[index >> 3] & mask)
            continue;

        allocated = true;

        if(flashrom_read(start + (int)(i * FLASHROM_BLOCK_SIZE), record,
                         FLASHROM_BLOCK_SIZE) < 0) {
            free(bitmap);
            return FLASHROM_ERR_READ_BLOCK;
        }

        if(read_le16(record + FLASHROM_OFFSET_CRC) !=
           flashrom_calc_crc(record)) {
            dbglog(DBG_WARNING,
                   "flashrom: partition %d record %u has an invalid CRC\n",
                   partid, (unsigned int)i);
            continue;
        }

        if(visitor(record, (uint16_t)i, data)) {
            free(bitmap);
            return FLASHROM_ERR_NONE;
        }
    }

    free(bitmap);
    return allocated ? FLASHROM_ERR_NOT_FOUND : FLASHROM_ERR_EMPTY_PART;
}

typedef struct flashrom_find_block_data {
    uint16_t blockid;
    uint8_t *raw_out;
    uint16_t *physical_out;
} flashrom_find_block_data_t;

static bool flashrom_find_block_visitor(
    const uint8_t record[FLASHROM_BLOCK_SIZE], uint16_t physical, void *data) {
    flashrom_find_block_data_t *find = data;

    if(read_le16(record) != find->blockid)
        return false;

    if(find->raw_out)
        memcpy(find->raw_out, record, FLASHROM_BLOCK_SIZE);
    if(find->physical_out)
        *find->physical_out = physical;
    return true;
}

static int flashrom_find_block(int partid, uint16_t blockid,
                               uint8_t raw_out[FLASHROM_BLOCK_SIZE],
                               uint16_t *physical_out) {
    flashrom_find_block_data_t data = {
        .blockid = blockid,
        .raw_out = raw_out,
        .physical_out = physical_out
    };

    return flashrom_visit_blocks(partid, flashrom_find_block_visitor, &data);
}

int flashrom_get_block(int partid, int blockid, uint8_t *buffer_out) {
    if(!buffer_out || blockid < 0 || blockid > UINT16_MAX)
        return FLASHROM_ERR_BAD_DATA;

    return flashrom_find_block(partid, (uint16_t)blockid, buffer_out, NULL);
}

int flashrom_read_block(int partid, uint16_t blockid, void *data_out,
                        flashrom_block_info_t *info_out) {
    uint8_t raw[FLASHROM_BLOCK_SIZE];
    uint16_t physical;
    int rv;

    if(!data_out && !info_out)
        return FLASHROM_ERR_BAD_DATA;

    rv = flashrom_find_block(partid, blockid, raw, &physical);
    if(rv < 0)
        return rv;

    if(data_out)
        memcpy(data_out, raw + 2, FLASHROM_BLOCK_DATA_SIZE);
    if(info_out) {
        info_out->logical_id = blockid;
        info_out->physical_block = physical;
    }

    return FLASHROM_ERR_NONE;
}

static bool flashrom_all_erased(const uint8_t *data, size_t length) {
    size_t i;

    for(i = 0; i < length; ++i) {
        if(data[i] != 0xff)
            return false;
    }

    return true;
}

static int flashrom_require_erased_records_locked(int partid,
                                                  size_t required) {
    flashrom_partition_layout_t layout;
    uint8_t magic[18];
    uint8_t record[FLASHROM_BLOCK_SIZE];
    uint8_t *bitmap = NULL;
    size_t record_index;
    size_t available = 0;
    int start, size;
    int rv;
    bool dirty = false;

    if(!required)
        return FLASHROM_ERR_NONE;
    if(flashrom_info(partid, &start, &size) || start < 0 || size < 0)
        return FLASHROM_ERR_NO_PARTITION;

    rv = flashrom_partition_layout((size_t)size, &layout);
    if(rv < 0)
        return rv;
    if(flashrom_read(start, magic, sizeof(magic)) != (int)sizeof(magic))
        return FLASHROM_ERR_READ_PART;
    if(memcmp(magic, "KATANA_FLASH____", 16) ||
       read_le16(magic + 16) != (uint16_t)partid)
        return FLASHROM_ERR_BAD_MAGIC;

    bitmap = malloc(layout.bitmap_bytes);
    if(!bitmap)
        return FLASHROM_ERR_NOMEM;
    if(flashrom_read(start + (int)layout.bitmap_offset, bitmap,
                     (int)layout.bitmap_bytes) !=
       (int)layout.bitmap_bytes) {
        rv = FLASHROM_ERR_READ_BITMAP;
        goto out;
    }

    for(record_index = 0; record_index < layout.record_count;
        ++record_index) {
        uint8_t mask = (uint8_t)(0x80u >> (record_index & 7u));
        size_t physical_offset;

        if(!(bitmap[record_index >> 3] & mask))
            continue;

        physical_offset = (record_index + 1u) * FLASHROM_BLOCK_SIZE;
        if(flashrom_read(start + (int)physical_offset, record,
                         sizeof(record)) != (int)sizeof(record)) {
            rv = FLASHROM_ERR_READ_BLOCK;
            goto out;
        }

        if(!flashrom_all_erased(record, sizeof(record))) {
            dirty = true;
            continue;
        }

        if(++available >= required) {
            rv = FLASHROM_ERR_NONE;
            goto out;
        }
    }

    rv = dirty ? FLASHROM_ERR_BAD_DATA : FLASHROM_ERR_NO_SPACE;

out:
    free(bitmap);
    return rv;
}

static int flashrom_append_block_locked(int partid, uint16_t blockid,
                                        const void *data,
                                        flashrom_block_info_t *info_out) {
    flashrom_partition_layout_t layout;
    uint8_t magic[18];
    uint8_t record[FLASHROM_BLOCK_SIZE];
    uint8_t verify[FLASHROM_BLOCK_SIZE];
    uint8_t *bitmap = NULL;
    uint8_t *bitmap_verify = NULL;
    size_t record_index;
    size_t physical_offset = 0;
    int start, size;
    int rv = FLASHROM_ERR_BAD_DATA;
    bool free_bit = false;

    if(!data || partid < FLASHROM_PT_BLOCK_1 ||
       partid > FLASHROM_PT_BLOCK_2)
        return FLASHROM_ERR_BAD_DATA;

    if(flashrom_info(partid, &start, &size) || start < 0 || size < 0)
        return FLASHROM_ERR_NO_PARTITION;

    rv = flashrom_partition_layout((size_t)size, &layout);
    if(rv < 0)
        return rv;

    if(flashrom_read(start, magic, sizeof(magic)) != (int)sizeof(magic))
        return FLASHROM_ERR_READ_PART;
    if(memcmp(magic, "KATANA_FLASH____", 16) ||
       read_le16(magic + 16) != (uint16_t)partid)
        return FLASHROM_ERR_BAD_MAGIC;

    bitmap = malloc(layout.bitmap_bytes);
    bitmap_verify = malloc(layout.bitmap_bytes);
    if(!bitmap || !bitmap_verify) {
        rv = FLASHROM_ERR_NOMEM;
        goto out;
    }

    if(flashrom_read(start + (int)layout.bitmap_offset, bitmap,
                     (int)layout.bitmap_bytes) !=
       (int)layout.bitmap_bytes) {
        rv = FLASHROM_ERR_READ_BITMAP;
        goto out;
    }

    /* A free record must still be erased; programmed free space is unsafe. */
    for(record_index = 0; record_index < layout.record_count;
        ++record_index) {
        uint8_t mask = (uint8_t)(0x80u >> (record_index & 7u));

        if(!(bitmap[record_index >> 3] & mask))
            continue;

        free_bit = true;
        physical_offset = (record_index + 1u) * FLASHROM_BLOCK_SIZE;
        if(flashrom_read(start + (int)physical_offset, verify,
                         FLASHROM_BLOCK_SIZE) != FLASHROM_BLOCK_SIZE) {
            rv = FLASHROM_ERR_READ_BLOCK;
            goto out;
        }

        if(flashrom_all_erased(verify, sizeof(verify)))
            break;
    }

    if(record_index == layout.record_count) {
        rv = free_bit ? FLASHROM_ERR_BAD_DATA : FLASHROM_ERR_NO_SPACE;
        goto out;
    }

    flashrom_record_build(blockid, data, record);

    /*
       Allocate first. If power is lost later, the CRC-invalid record is
       ignored and the previous valid copy remains visible. This may consume
       one record, but it never leaves programmed storage marked reusable.
    */
    flashrom_bitmap_allocate(bitmap, record_index);
    if(flashrom_write_unlocked(start + (int)layout.bitmap_offset, bitmap,
                               (int)layout.bitmap_bytes) !=
       (int)layout.bitmap_bytes) {
        rv = FLASHROM_ERR_WRITE_BITMAP;
        goto out;
    }
    if(flashrom_read(start + (int)layout.bitmap_offset, bitmap_verify,
                     (int)layout.bitmap_bytes) !=
       (int)layout.bitmap_bytes ||
       memcmp(bitmap, bitmap_verify, layout.bitmap_bytes)) {
        rv = FLASHROM_ERR_VERIFY;
        goto out;
    }

    /* Keep the erased CRC as the final publication barrier. */
    if(flashrom_write_unlocked(start + (int)physical_offset, record,
                               FLASHROM_OFFSET_CRC) != FLASHROM_OFFSET_CRC) {
        rv = FLASHROM_ERR_WRITE_BLOCK;
        goto out;
    }
    if(flashrom_read(start + (int)physical_offset, verify,
                     FLASHROM_OFFSET_CRC) != FLASHROM_OFFSET_CRC ||
       memcmp(record, verify, FLASHROM_OFFSET_CRC)) {
        rv = FLASHROM_ERR_VERIFY;
        goto out;
    }

    if(flashrom_write_unlocked(start + (int)physical_offset +
                               FLASHROM_OFFSET_CRC,
                               record + FLASHROM_OFFSET_CRC, 2) != 2) {
        rv = FLASHROM_ERR_WRITE_BLOCK;
        goto out;
    }
    if(flashrom_read(start + (int)physical_offset, verify,
                     sizeof(verify)) != (int)sizeof(verify) ||
       memcmp(record, verify, sizeof(record)) ||
       read_le16(verify + FLASHROM_OFFSET_CRC) !=
       flashrom_calc_crc(verify)) {
        rv = FLASHROM_ERR_VERIFY;
        goto out;
    }

    if(info_out) {
        info_out->logical_id = blockid;
        info_out->physical_block = (uint16_t)(record_index + 1u);
    }
    rv = FLASHROM_ERR_NONE;

out:
    free(bitmap_verify);
    free(bitmap);
    return rv;
}

int flashrom_append_block(int partid, uint16_t blockid, const void *data,
                          flashrom_block_info_t *info_out) {
    int rv;

    if(!data || irq_inside_int())
        return FLASHROM_ERR_BAD_DATA;
    if(mutex_lock(&flashrom_program_mutex) < 0)
        return FLASHROM_ERR_BAD_DATA;

    rv = flashrom_append_block_locked(partid, blockid, data, info_out);
    mutex_unlock(&flashrom_program_mutex);
    return rv;
}

/* This internal function returns the system config block. As far as I
   can determine, this is always partition 2, logical block 5. */
static int flashrom_load_syscfg(uint8_t *buffer) {
    return flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_SYSCFG, buffer);
}

/* Structure of the system config block (as much as we know anyway). */
typedef struct {
    uint16_t  block_id;   /* Should be 5 */
    uint8_t   date[4];    /* Last set time (secs since 1/1/1950 in LE) */
    uint8_t   unk1;       /* Unknown */
    uint8_t   lang;       /* Language ID */
    uint8_t   mono;       /* Mono/stereo setting */
    uint8_t   autostart;  /* Auto-start setting */
    uint8_t   unk2[4];    /* Unknown */
    uint8_t   padding[50];    /* Should generally be all 0xff */
} syscfg_t;

int flashrom_get_syscfg(flashrom_syscfg_t *out) {
    uint8_t buffer[64];
    int rv;
    syscfg_t *sc = (syscfg_t *)buffer;

    if(!out)
        return FLASHROM_ERR_BAD_DATA;

    /* Get the system config block */
    rv = flashrom_load_syscfg(buffer);
    if(rv < 0)  return rv;

    /* Fill in values from it */
    out->language = sc->lang;
    out->audio = sc->mono == 1 ? 0 : 1;
    out->autostart = sc->autostart == 1 ? 0 : 1;

    return FLASHROM_ERR_NONE;
}

int flashrom_get_syscfg_ex(flashrom_syscfg_ex_t *out) {
    uint8_t data[FLASHROM_BLOCK_DATA_SIZE];
    int rv;

    if(!out)
        return FLASHROM_ERR_BAD_DATA;

    rv = flashrom_read_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_SYSCFG,
                             data, NULL);
    if(rv < 0)
        return rv;

    return flashrom_syscfg_decode(data, out);
}

int flashrom_set_syscfg_ex(const flashrom_syscfg_ex_t *settings) {
    uint8_t data[FLASHROM_BLOCK_DATA_SIZE];
    int rv;

    if(!settings || settings->language < FLASHROM_LANG_JAPANESE ||
       settings->language > FLASHROM_LANG_ITALIAN ||
       (settings->audio != 0 && settings->audio != 1) ||
       (settings->autostart != 0 && settings->autostart != 1) ||
       irq_inside_int())
        return FLASHROM_ERR_BAD_DATA;
    if(mutex_lock(&flashrom_program_mutex) < 0)
        return FLASHROM_ERR_BAD_DATA;

    rv = flashrom_read_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_SYSCFG,
                             data, NULL);
    if(rv < 0)
        goto out;

    rv = flashrom_syscfg_payload_update(data, settings->settings_time,
                                        settings->language, settings->audio,
                                        settings->autostart);
    if(rv < 0)
        goto out;

    rv = flashrom_append_block_locked(FLASHROM_PT_BLOCK_1,
                                      FLASHROM_B1_SYSCFG, data, NULL);

out:
    mutex_unlock(&flashrom_program_mutex);
    return rv;
}

typedef struct flashrom_history_packets {
    uint16_t base_id;
    uint8_t packets[4][FLASHROM_BLOCK_DATA_SIZE];
    bool found[4];
    unsigned int remaining;
} flashrom_history_packets_t;

static bool flashrom_history_packet_visitor(
    const uint8_t record[FLASHROM_BLOCK_SIZE], uint16_t physical, void *data) {
    flashrom_history_packets_t *history = data;
    uint16_t blockid = read_le16(record);
    unsigned int packet;

    (void)physical;

    if(blockid < history->base_id || blockid > history->base_id + 3u)
        return false;

    packet = blockid - history->base_id;
    if(history->found[packet])
        return false;

    memcpy(history->packets[packet], record + 2, FLASHROM_BLOCK_DATA_SIZE);
    history->found[packet] = true;
    --history->remaining;
    return history->remaining == 0;
}

int flashrom_play_history_read(unsigned int slot,
                               flashrom_play_history_t *out) {
    flashrom_history_packets_t history = {
        .base_id = (uint16_t)(24u + slot * 4u),
        .remaining = 4
    };
    int rv;

    if(!out || slot >= FLASHROM_PLAY_HISTORY_SLOTS)
        return FLASHROM_ERR_BAD_DATA;

    rv = flashrom_visit_blocks(FLASHROM_PT_SETTINGS,
                               flashrom_history_packet_visitor, &history);
    if(rv < 0 || history.remaining != 0)
        return rv == FLASHROM_ERR_EMPTY_PART ? FLASHROM_ERR_NOT_FOUND : rv;

    return flashrom_play_history_decode(history.packets, out);
}

typedef struct flashrom_history_match {
    const char *product_number;
    bool seen[FLASHROM_PLAY_HISTORY_SLOTS];
    unsigned int slot;
} flashrom_history_match_t;

static bool flashrom_history_match_visitor(
    const uint8_t record[FLASHROM_BLOCK_SIZE], uint16_t physical, void *data) {
    flashrom_history_match_t *match = data;
    uint16_t blockid = read_le16(record);
    unsigned int slot;

    (void)physical;

    if(blockid < 24u || (blockid - 24u) % 4u != 0)
        return false;

    slot = (blockid - 24u) / 4u;
    if(slot >= FLASHROM_PLAY_HISTORY_SLOTS || match->seen[slot])
        return false;

    match->seen[slot] = true;
    if(memcmp(record + 4, match->product_number, 10))
        return false;

    match->slot = slot;
    return true;
}

int flashrom_play_history_find(const char product_number[10],
                               flashrom_play_history_t *out,
                               unsigned int *slot_out) {
    flashrom_history_match_t match = {
        .product_number = product_number
    };
    flashrom_play_history_t history;
    int rv;

    if(!product_number || (!out && !slot_out))
        return FLASHROM_ERR_BAD_DATA;

    rv = flashrom_visit_blocks(FLASHROM_PT_SETTINGS,
                               flashrom_history_match_visitor, &match);
    if(rv < 0)
        return rv == FLASHROM_ERR_EMPTY_PART ? FLASHROM_ERR_NOT_FOUND : rv;

    rv = flashrom_play_history_read(match.slot, &history);
    if(rv < 0)
        return rv;

    if(out)
        *out = history;
    if(slot_out)
        *slot_out = match.slot;
    return FLASHROM_ERR_NONE;
}

int flashrom_play_history_write(
    unsigned int slot, const flashrom_play_history_t *history,
    flashrom_play_history_write_result_t *result_out) {
    static const unsigned int write_order[4] = { 2, 3, 1, 0 };
    flashrom_play_history_write_result_t result = {
        .failed_packet = -1
    };
    flashrom_history_packets_t current;
    uint8_t packets[4][FLASHROM_BLOCK_DATA_SIZE];
    unsigned int changed = 0;
    unsigned int i;
    int rv;

    if(result_out)
        *result_out = result;
    if(!history || slot >= FLASHROM_PLAY_HISTORY_SLOTS || irq_inside_int())
        return FLASHROM_ERR_BAD_DATA;

    rv = flashrom_play_history_encode(history, packets);
    if(rv < 0)
        return rv;

    memset(&current, 0, sizeof(current));
    current.base_id = (uint16_t)(24u + slot * 4u);
    current.remaining = 4;

    if(mutex_lock(&flashrom_program_mutex) < 0)
        return FLASHROM_ERR_BAD_DATA;

    rv = flashrom_visit_blocks(FLASHROM_PT_SETTINGS,
                               flashrom_history_packet_visitor, &current);
    if(rv != FLASHROM_ERR_NONE && rv != FLASHROM_ERR_NOT_FOUND &&
       rv != FLASHROM_ERR_EMPTY_PART)
        goto out;

    for(i = 0; i < 4; ++i) {
        if(!current.found[i] ||
           memcmp(current.packets[i], packets[i],
                  FLASHROM_BLOCK_DATA_SIZE)) {
            result.requested_mask |= (uint8_t)(1u << i);
            ++changed;
        }
    }

    rv = flashrom_require_erased_records_locked(FLASHROM_PT_SETTINGS,
                                                changed);
    if(rv < 0)
        goto out;

    /*
       Volatile counters are written first. Packet 1 precedes packet 0 so a
       changed identity is not accepted until its cross-packet CRC agrees.
    */
    for(i = 0; i < 4; ++i) {
        unsigned int packet = write_order[i];

        if(!(result.requested_mask & (uint8_t)(1u << packet)))
            continue;

        result.failed_packet = (int)packet;
        rv = flashrom_append_block_locked(
            FLASHROM_PT_SETTINGS, current.base_id + packet,
            packets[packet], &result.records[packet]);
        if(rv < 0)
            goto out;

        result.committed_mask |= (uint8_t)(1u << packet);
        result.failed_packet = -1;
    }

    rv = FLASHROM_ERR_NONE;

out:
    mutex_unlock(&flashrom_program_mutex);
    if(result_out)
        *result_out = result;
    return rv;
}

int flashrom_get_region(void) {
    int start, size;
    char region[6] = { 0 };

    /* Find the partition */
    if(flashrom_info(FLASHROM_PT_SYSTEM, &start, &size)) {
        dbglog(DBG_ERROR, "flashrom_get_region: can't find partition 0\n");
        return FLASHROM_ERR_NO_PARTITION;
    }

    /* Read the first 5 characters of that partition */
    if(flashrom_read(start, region, 5) < 0) {
        dbglog(DBG_ERROR, "flashrom_get_region: can't read partition 0\n");
        return FLASHROM_ERR_READ_PART;
    }

    /* Now compare against known codes */
    if(!strcmp(region, "00000"))
        return FLASHROM_REGION_JAPAN;
    else if(!strcmp(region, "00110"))
        return FLASHROM_REGION_US;
    else if(!strcmp(region, "00211"))
        return FLASHROM_REGION_EUROPE;
    else {
        dbglog(DBG_WARNING, "flashrom_get_region: unknown code '%s'\n", region);
        return FLASHROM_REGION_UNKNOWN;
    }
}

/* Structure of the ISP config blocks (as much as we know anyway).
   Thanks to Sam Steele for this info. */
typedef struct {
    union {
        struct {
            /* Block 0xE0 */
            uint16_t  blockid;        /* Should be 0xE0 */
            uint8_t   prodname[4];    /* SEGA */
            uint8_t   method;
            uint8_t   unk1;           /* 0x00 */
            uint8_t   unk2[2];        /* 0x00 0x00 */
            uint8_t   ip[4];          /* These are all in big-endian notation */
            uint8_t   nm[4];
            uint8_t   bc[4];
            uint8_t   dns1[4];
            uint8_t   dns2[4];
            uint8_t   gw[4];
            uint8_t   unk3[4];        /* All zeros */
            char      hostname[24];   /* Host name */
            uint16_t  crc;
        } e0;

        struct {
            /* Block E2 */
            uint16_t  blockid;    /* Should be 0xE2 */
            uint8_t   unk[12];
            char      email[48];
            uint16_t  crc;
        } e2;

        struct {
            /* Block E4 */
            uint16_t  blockid;    /* Should be 0xE4 */
            uint8_t   unk[32];
            char      smtp[28];
            uint16_t  crc;
        } e4;

        struct {
            /* Block E5 */
            uint16_t  blockid;    /* Should be 0xE5 */
            uint8_t   unk[36];
            char      pop3[24];
            uint16_t  crc;
        } e5;

        struct {
            /* Block E6 */
            uint16_t  blockid;    /* Should be 0xE6 */
            uint8_t   unk[40];
            char      pop3_login[20];
            uint16_t  crc;
        } e6;

        struct {
            /* Block E7 */
            uint16_t  blockid;    /* Should be 0xE7 */
            uint8_t   unk[12];
            char      pop3_passwd[32];
            char      proxy_host[16];
            uint16_t  crc;
        } e7;

        struct {
            /* Block E8 */
            uint16_t  blockid;    /* Should be 0xE8 */
            uint8_t   unk1[48];
            uint16_t  proxy_port;
            uint16_t  unk2;
            char      ppp_login[8];
            uint16_t  crc;
        } e8;

        struct {
            /* Block E9 */
            uint16_t  blockid;    /* Should be 0xE9 */
            uint8_t   unk[40];
            char      ppp_passwd[20];
            uint16_t  crc;
        } e9;

        struct {
            /* Block 0xC6 */
            uint16_t  blockid;
            char      prodname[4];
            char      ppp_login[28];
            char      ppp_passwd[16];
            char      phone1_pt1[12];
            uint16_t  crc;
        } c6;

        struct {
            /* Block 0xC7 */
            uint16_t  blockid;
            char      phone1_pt2[15];
            char      unk1[13];
            char      phone2[27];
            char      unk2[5];
            uint16_t  crc;
        } c7;

        struct {
            /* Block 0xC8 */
            uint16_t  blockid;
            char      unk1[8];
            char      phone3[27];
            char      unk4[13];
            uint8_t   dns1[4];
            uint8_t   dns2[4];
            char      unk5[4];
            uint16_t  crc;
        } c8;

        struct {
            /* Block 0xEB */
            uint16_t  blockid;
            char      unk1[12];
            char      atx[48];
            uint16_t  crc;
        } eb;
    };
} isp_settings_t;

int flashrom_get_ispcfg(flashrom_ispcfg_t *out) {
    uint8_t buffer[sizeof(isp_settings_t)];
    isp_settings_t *isp = (isp_settings_t *)buffer;
    int found = 0;

    /* Clean out the output config buffer. */
    memset(out, 0, sizeof(flashrom_ispcfg_t));

    /* Get the E0 config block */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_IP_SETTINGS, buffer) >= 0) {
        /* Fill in values from it */
        out->method = isp->e0.method;
        memcpy(out->ip, isp->e0.ip, 4);
        memcpy(out->nm, isp->e0.nm, 4);
        memcpy(out->bc, isp->e0.bc, 4);
        memcpy(out->gw, isp->e0.gw, 4);
        memcpy(out->dns[0], isp->e0.dns1, 4);
        memcpy(out->dns[1], isp->e0.dns2, 4);
        memcpy(out->hostname, isp->e0.hostname, 24);

        out->valid_fields |= FLASHROM_ISP_IP | FLASHROM_ISP_NETMASK |
                             FLASHROM_ISP_BROADCAST | FLASHROM_ISP_GATEWAY | FLASHROM_ISP_DNS |
                             FLASHROM_ISP_HOSTNAME;
        found++;
    }

    /* Get the email config block */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_EMAIL, buffer) >= 0) {
        /* Fill in the values from it */
        memcpy(out->email, isp->e2.email, 48);

        out->valid_fields |= FLASHROM_ISP_EMAIL;
        found++;
    }

    /* Get the smtp config block */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_SMTP, buffer) >= 0) {
        /* Fill in the values from it */
        memcpy(out->smtp, isp->e4.smtp, 28);

        out->valid_fields |= FLASHROM_ISP_SMTP;
        found++;
    }

    /* Get the pop3 config block */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_POP3, buffer) >= 0) {
        /* Fill in the values from it */
        memcpy(out->pop3, isp->e5.pop3, 24);

        out->valid_fields |= FLASHROM_ISP_POP3;
        found++;
    }

    /* Get the pop3 login config block */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_POP3LOGIN, buffer) >= 0) {
        /* Fill in the values from it */
        memcpy(out->pop3_login, isp->e6.pop3_login, 20);

        out->valid_fields |= FLASHROM_ISP_POP3_USER;
        found++;
    }

    /* Get the pop3 passwd config block */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_POP3PASSWD, buffer) >= 0) {
        /* Fill in the values from it */
        memcpy(out->pop3_passwd, isp->e7.pop3_passwd, 32);
        memcpy(out->proxy_host, isp->e7.proxy_host, 16);

        out->valid_fields |= FLASHROM_ISP_POP3_PASS | FLASHROM_ISP_PROXY_HOST;
        found++;
    }

    /* Get the PPP login config block */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PPPLOGIN, buffer) >= 0) {
        /* Fill in the values from it */
        out->proxy_port = isp->e8.proxy_port;
        memcpy(out->ppp_login, isp->e8.ppp_login, 8);

        out->valid_fields |= FLASHROM_ISP_PROXY_PORT | FLASHROM_ISP_PPP_USER;
        found++;
    }

    /* Get the PPP passwd config block */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PPPPASSWD, buffer) >= 0) {
        /* Fill in the values from it */
        memcpy(out->ppp_passwd, isp->e9.ppp_passwd, 20);

        out->valid_fields |= FLASHROM_ISP_PPP_PASS;
        found++;
    }

    /* Grab block 0xC6 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_DK_PPP1, buffer) >= 0) {
        if(!(out->valid_fields & FLASHROM_ISP_PPP_USER)) {
            /* Grab the PPP Username. */
            strncpy(out->ppp_login, isp->c6.ppp_login, 28);
            out->ppp_login[28] = '\0';
            out->valid_fields |= FLASHROM_ISP_PPP_USER;
        }

        if(!(out->valid_fields & FLASHROM_ISP_PPP_PASS)) {
            /* Grab the PPP Password. */
            strncpy(out->ppp_passwd, isp->c6.ppp_passwd, 16);
            out->ppp_passwd[16] = '\0';
            out->valid_fields |= FLASHROM_ISP_PPP_PASS;
        }

        found++;
    }

    /* Grab block 0xC7 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_DK_PPP2, buffer) >= 0) {
        if(!(out->valid_fields & FLASHROM_ISP_PHONE1)) {
            /* The full number is 27 digits in C6-C8,
            so we truncate it to fit the phone1 field */
            strcpy_no_term(out->phone1, isp->c6.phone1_pt1, 12);
            strcpy_no_term(out->phone1 + 12, isp->c7.phone1_pt2, 25 - 12);
            out->phone1[25] = '\0';
            out->valid_fields |= FLASHROM_ISP_PHONE1;
        }
        found++;
    }

    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_DK_DNS, buffer) >= 0) {
        /* Only read if we didn't find it already */
        if(!(out->valid_fields & FLASHROM_ISP_DNS)) {
            /* Grab the two DNS addresses. */
            memcpy(out->dns[0], isp->c8.dns1, 4);
            memcpy(out->dns[1], isp->c8.dns2, 4);
            out->valid_fields |= FLASHROM_ISP_DNS;
        }

        found++;
    }

    return found > 0 ? 0 : -1;
}

/* Structure of the ISP configuration blocks created by PlanetWeb (confirmed on
   version 1.0 and 2.1; some fields are longer on 2.1, but they always extend
   into what would be padding in 1.0). */
typedef struct {
    union {
        struct {
            /* Block 0x80 */
            uint16_t  blockid;        /* Should be 0x80 */
            char      prodname[9];    /* Should be 'PWBrowser' */
            uint8_t   unk1[2];        /* Unknown: 00 16 (1.0), 00 1C (2.1) */
            uint8_t   dial_areacode;  /* 1 = Dial area code, 0 = don't */
            char      out_prefix[8];  /* Outside dial prefix */
            uint8_t   padding1[8];
            char      email_pt2[16];  /* Second? part of email address (2.1) */
            char      cw_prefix[8];   /* Call waiting prefix */
            uint8_t   padding2[8];
            uint16_t  crc;
        } b80;

        struct {
            /* Block 0x81 */
            uint16_t  blockid;        /* Should be 0x81 */
            char      email_pt3[14];  /* Third? part of email address (2.1)*/
            uint8_t   padding1[2];
            char      real_name[30];  /* The "Real Name" (21 bytes on 1.0) */
            uint8_t   padding2[14];
            uint16_t  crc;
        } b81;

        struct {
            /* Block 0x82 */
            uint16_t  blockid;        /* Should be 0x82 */
            uint8_t   padding1[30];
            char      modem_str[30];  /* Modem init string (confirmed on 2.1) */
            uint16_t  crc;
        } b82;

        struct {
            /* Block 0x83 */
            uint16_t  blockid;        /* Should be 0x83 */
            uint8_t   modem_str2[2];  /* Modem init string continued */
            char      area_code[3];
            uint8_t   padding2[29];
            char      ld_prefix[20];  /* Long-distance prefix */
            uint8_t   padding3[6];
            uint16_t  crc;
        } b83;

        struct {
            /* Block 0x84 -- This one is pretty much mostly a mystery. */
            uint16_t  blockid;        /* Should be 0x84 */
            uint8_t   unk1[6];        /* Might be padding, all 0x00s */
            uint8_t   use_proxy;      /* 1 = use proxy, 0 = don't */
            uint8_t   unk2[53];       /* No idea on this stuff... */
            uint16_t  crc;
        } b84;

        /* Other 0x80 range blocks might be used, but I don't really know what
           would be in them. */

        struct {
            /* Block 0xC0 */
            uint16_t  blockid;        /* Should be 0xC0 */
            uint8_t   unk1;           /* Might be padding? (0x00) */
            uint8_t   settings;       /* Bitfield:
                                       bit 0 = pulse dial (1) or tone dial (0),
                                       bit 7 = blind dial (1) or not (0) */
            uint8_t   unk2[2];        /* Might be padding (0x00 0x00) */
            char      prodname[4];    /* Should be 'SEGA' */
            char      ppp_login[28];
            char      ppp_passwd[16];
            char      ac1[5];         /* Area code for phone 1, in parenthesis */
            char      phone1_pt1[3];  /* First three digits of phone 1 */
            uint16_t  crc;
        } c0;

        struct {
            /* Block 0xC1 */
            uint16_t  blockid;        /* Should be 0xC1 */
            char      phone1_pt2[22]; /* Rest of phone 1 */
            uint8_t   padding[10];
            char      ac2[5];         /* Area code for phone 2, in parenthesis */
            char      phone2_pt1[23]; /* First 23 digits of phone 2 */
            uint16_t  crc;
        } c1;

        struct {
            /* Block 0xC2 */
            uint16_t  blockid;        /* Should be 0xC2 */
            char      phone2_pt2[2];  /* Last two digits of phone 2 */
            uint8_t   padding[50];
            uint8_t   dns1[4];        /* DNS 1, big endian notation */
            uint8_t   dns2[4];        /* DNS 2, big endian notation */
            uint16_t  crc;
        } c2;

        struct {
            /* Block 0xC3 */
            uint16_t  blockid;        /* Should be 0xC3 */
            char      email_p1[32];   /* First? part of the email address
                                       (This is the only part on 1.0) */
            uint8_t   padding[16];
            char      out_srv_p1[12]; /* Outgoing email server, first 12 chars */
            uint16_t  crc;
        } c3;

        struct {
            /* Block 0xC4 */
            uint16_t  blockid;        /* Should be 0xC4 */
            char      out_srv_p2[18]; /* Rest of outgoing email server */
            uint8_t   padding1[2];
            char      in_srv[30];     /* Incoming email server */
            uint8_t   padding2[2];
            char      em_login_p1[8]; /* Email login, first 8 chars */
            uint16_t  crc;
        } c4;

        struct {
            /* Block 0xC5 */
            uint16_t  blockid;        /* Should be 0xC5 */
            char      em_login_p2[8]; /* Rest of email login */
            char      em_passwd[16];  /* Email password */
            char      proxy_srv[30];  /* Proxy Server */
            uint8_t   padding1[2];
            uint16_t  proxy_port;     /* Proxy port, little endian notation */
            uint8_t   padding2[2];
            uint16_t  crc;
        } c5;

        /* Blocks 0xC7 - 0xCB also appear to be used by PlanetWeb, but are
           always blank in my tests. My only guess is that they were storage
           for a potential second ISP setting set. */
    };
} pw_isp_settings_t;

int flashrom_get_pw_ispcfg(flashrom_ispcfg_t *out) {
    uint8_t buffer[64];
    pw_isp_settings_t *isp = (pw_isp_settings_t *)buffer;

    /* Clear our output buffer completely.  */
    memset(out, 0, sizeof(flashrom_ispcfg_t));

    /* Get the 0x80 block first, and check if its valid. */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_SETTINGS_1, buffer) >= 0) {
        /* Make sure the product name is 'PWBrowser' */
        if(strncmp(isp->b80.prodname, "PWBrowser", 9)) {
            return -1;
        }

        /* Determine if the dial area code option is set or not. */
        if(isp->b80.dial_areacode) {
            out->flags |= FLASHROM_ISP_DIAL_AREACODE;
        }

        /* Copy out the outside dial prefix. */
        strcpy_with_term(out->out_prefix, isp->b80.out_prefix, 8);
        out->valid_fields |= FLASHROM_ISP_OUT_PREFIX;

        /* Copy out the call waiting prefix. */
        strcpy_with_term(out->cw_prefix, isp->b80.cw_prefix, 8);
        out->valid_fields |= FLASHROM_ISP_CW_PREFIX;

        /* Copy the second part of the email address (if it exists). We don't
           set the email as valid here, since that really depends on the first
           part being found (PW 1.0 doesn't store anything in this place). */
        strcpy_no_term(out->email + 32, isp->b80.email_pt2, 16);
    }
    else {
        /* If we couldn't find the PWBrowser block, punt, the PlanetWeb settings
           most likely do not exist. */
        return -1;
    }

    /* Grab block 0x81 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_SETTINGS_2, buffer) >= 0) {
        /* Copy the third part of the email address to the appropriate place.
           Note that PlanetWeb 1.0 doesn't store anything here, thus we'll just
           copy a null terminator. */
        strcpy_no_term(out->email + 32 + 16, isp->b81.email_pt3, 14);

        /* Copy out the "Real Name" field. */
        strcpy_with_term(out->real_name, isp->b81.real_name, 30);
        out->valid_fields |= FLASHROM_ISP_REAL_NAME;
    }

    /* Grab block 0x82 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_SETTINGS_3, buffer) >= 0) {
        /* The only thing in this block is the modem init string, go ahead and
           copy it to our destination. */
        strcpy_with_term(out->modem_init, isp->b82.modem_str, 30);
        out->valid_fields |= FLASHROM_ISP_MODEM_INIT;
    }

    /* Grab block 0x83 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_SETTINGS_4, buffer) >= 0) {
        /* The modem init string continues at the start of this block. */
        strcpy_no_term(out->modem_init + 30, (char *)isp->b83.modem_str2, 2);
        out->modem_init[32] = '\0';

        /* Copy out the area code next. */
        strcpy_with_term(out->area_code, isp->b83.area_code, 3);
        out->valid_fields |= FLASHROM_ISP_AREA_CODE;

        /* Copy the long-distance dial prefix */
        strcpy_with_term(out->ld_prefix, isp->b83.ld_prefix, 20);
        out->valid_fields |= FLASHROM_ISP_LD_PREFIX;
    }

    /* Grab block 0x84 -- Most of this block is currently unknown */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_SETTINGS_5, buffer) >= 0) {
        /* The only thing currently known in here is the use proxy flag. */
        if(isp->b84.use_proxy) {
            out->flags |= FLASHROM_ISP_USE_PROXY;
        }
    }

    /* Other 0x85-0x8F blocks might be used, but I have no ideas on their use. */

    /* Grab block 0xC0 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_PPP1, buffer) >= 0) {
        /* Make sure the product id is "SEGA". */
        if(strncmp(isp->c0.prodname, "SEGA", 4)) {
            return -1;
        }

        /* Check the settings first. */
        if(isp->c0.settings & 0x01) {
            out->flags |= FLASHROM_ISP_PULSE_DIAL;
        }

        if(isp->c0.settings & 0x80) {
            out->flags |= FLASHROM_ISP_BLIND_DIAL;
        }

        /* Grab the PPP Username. */
        strcpy_with_term(out->ppp_login, isp->c0.ppp_login, 28);
        out->valid_fields |= FLASHROM_ISP_PPP_USER;

        /* Grab the PPP Password. */
        strcpy_with_term(out->ppp_passwd, isp->c0.ppp_passwd, 16);
        out->valid_fields |= FLASHROM_ISP_PPP_PASS;

        /* Grab the area code for phone 1, stripping away the parenthesis. */
        strcpy_with_term(out->p1_areacode, isp->c0.ac1 + 1, 3);

        /* Grab the start of phone number 1. */
        strcpy_with_term(out->phone1, isp->c0.phone1_pt1, 3);
    }

    /* Grab block 0xC1 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_PPP2, buffer) >= 0) {
        /* Grab the rest of phone number 1. */
        strcpy_no_term(out->phone1 + 3, isp->c1.phone1_pt2, 22);
        out->phone1[25] = '\0';
        out->valid_fields |= FLASHROM_ISP_PHONE1;

        /* Grab the area code for phone 2, stripping away the parenthesis. */
        strcpy_with_term(out->p2_areacode, isp->c1.ac2 + 1, 3);

        /* Grab the start of phone number 2. */
        strcpy_with_term(out->phone2, isp->c1.phone2_pt1, 23);
    }

    /* Grab block 0xC2 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_DNS, buffer) >= 0) {
        /* Grab the last two digits of phone number 2. */
        out->phone2[23] = isp->c2.phone2_pt2[0];
        out->phone2[24] = isp->c2.phone2_pt2[1];
        out->phone2[25] = '\0';
        out->valid_fields |= FLASHROM_ISP_PHONE2;

        /* Grab the two DNS addresses. */
        memcpy(out->dns[0], isp->c2.dns1, 4);
        memcpy(out->dns[1], isp->c2.dns2, 4);
        out->valid_fields |= FLASHROM_ISP_DNS;
    }

    /* Grab block 0xC3 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_EMAIL1, buffer) >= 0) {
        /* Grab the beginning of the email address (or all of it in PW 1.0). */
        strcpy_no_term(out->email, isp->c3.email_p1, 32);
        out->valid_fields |= FLASHROM_ISP_EMAIL;

        /* Grab the beginning of the SMTP server. */
        strcpy_with_term(out->smtp, isp->c3.out_srv_p1, 12);
    }

    /* Grab block 0xC4 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_EMAIL2, buffer) >= 0) {
        /* Grab the end of the SMTP server. */
        strcpy_no_term(out->smtp + 12, isp->c4.out_srv_p2, 18);
        out->smtp[30] = '\0';
        out->valid_fields |= FLASHROM_ISP_SMTP;

        /* Grab the POP3 server. */
        strcpy_with_term(out->pop3, isp->c4.in_srv, 30);
        out->valid_fields |= FLASHROM_ISP_POP3;

        /* Grab the beginning of the POP3 login. */
        strcpy_with_term(out->pop3_login, isp->c4.em_login_p1, 8);
    }

    /* Grab block 0xC5 */
    if(flashrom_get_block(FLASHROM_PT_BLOCK_1, FLASHROM_B1_PW_EMAIL_PROXY, buffer) >= 0) {
        /* Grab the end of the POP3 login. */
        strcpy_no_term(out->pop3_login + 8, isp->c5.em_login_p2, 8);
        out->pop3_login[16] = '\0';
        out->valid_fields |= FLASHROM_ISP_POP3_USER;

        /* Grab the POP3 password. */
        strcpy_with_term(out->pop3_passwd, isp->c5.em_passwd, 16);
        out->valid_fields |= FLASHROM_ISP_POP3_PASS;

        /* Grab the proxy server. */
        strcpy_with_term(out->proxy_host, isp->c5.proxy_srv, 30);
        out->valid_fields |= FLASHROM_ISP_PROXY_HOST;

        /* Grab the proxy port. */
        out->proxy_port = isp->c5.proxy_port;
        out->valid_fields |= FLASHROM_ISP_PROXY_PORT;
    }

    out->method = FLASHROM_ISP_DIALUP;

    return out->valid_fields == 0 ? -2 : 0;
}
